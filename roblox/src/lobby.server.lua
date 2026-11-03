-- src/lobby.server.lua -- the glue: tickets minted by IDUNA, seats taken here, pairs read by the world.
--
-- Everything a test can reach is in the modules beside this file. What is here needs a running server: Players,
-- HttpService, RemoteEvents, a loop.
--
-- The 460 flow, end to end:
--   1. a body signs in: IDUNA's generic game account (guest-register) makes a REAL player row and token --
--      no password, no OAuth inside the engine. login460 does the reading and the validation; a secret is
--      persisted per Roblox UserId so the next session is the same account (guest-login), not a new one.
--   2. the server asks IDUNA for a connect ticket with that token as a Bearer, and reads IDUNA's answer through
--      ticket460.from_mint -- which refuses a ticket whose JSON fields disagree with the bytes IDUNA signed
--   3. the body takes a seat in casual or ranked (queue460), keyed by Roblox UserId
--   4. a tick pairs bodies by the widening window; each side is handed ITS OWN ticket to present to the C server
--   5. the published ladder is re-read and pushed to the screens (net460)
--
-- HONEST LIMITS, named where they bite:
--   * Roblox cannot reach 127.0.0.1 -- NetUrl/TicketUrl must be public endpoints.
--   * The login is an IDUNA GUEST account: real player_id, real ES256 token, no email and no recovery -- the
--     secret is the account. A player who wants to keep it across places needs IDUNA's own link-email flow,
--     which is a WOTAN page today and not wired to this lobby.
--   * Identity persistence uses DataStore keyed by Roblox UserId (player_id + guest_secret, never the token).
--     It is best-effort and pcall-guarded: a place with DataStore disabled still works, and simply forgets.
--   * There is NO binding yet between a Roblox UserId and a SHANKPIT pid, so a player queues at the default
--     rating (1000) and the board is read-only. Inventing a binding would put a made-up number on a screen.
--   * The arena is the C server. A pairing hands out a ticket; it does not host an FPS.

local Players = game:GetService("Players")
local HttpService = game:GetService("HttpService")
local ReplicatedStorage = game:GetService("ReplicatedStorage")

local queue460 = require(script.Parent.queue460)
local ticket460 = require(script.Parent.ticket460)
local net460 = require(script.Parent.net460)
local login460 = require(script.Parent.login460)
local stats460 = require(script.Parent.stats460)
local arena460 = require(script.Parent.arena460)

-- The three hosts used to be three hardcoded strings. They are now one switch: set the attributes
-- `IdunaBase` / `WotanBase` on this Script (e.g. `https://<space>.hf.space/api/v1` for a dev backend) and the
-- place points somewhere else without editing code. Empty/unset = exactly the production hosts it had before.
local function baseAttr(name, default)
    local v = script:GetAttribute(name)
    if type(v) == "string" and v ~= "" then return v end
    return default
end

local IDUNA_BASE = baseAttr("IdunaBase", "https://iam.okemily.com/api/v1")
local WOTAN_BASE = baseAttr("WotanBase", "https://okemily.com/api/v1")

local CONFIG = {
    NetUrl = WOTAN_BASE .. "/shankpit-net/deadweight",
    -- IDUNA's connect-ticket mint (IDUNA/internal/http/handlers/shankpit_ticket.go): 72 hex chars, 5 min TTL,
    -- presented once at PACKET_CONNECT and verified by the C server's own shared secret.
    TicketUrl = IDUNA_BASE .. "/shankpit/ticket",
    -- IDUNA's generic game accounts, scoped to the game the ticket endpoint is scoped to ("shankpit").
    -- A game player token's subject is "guest:<uuid>"; its player_id claim is what the ticket reads.
    GameUrl = IDUNA_BASE .. "/games/shankpit",
    BoardEvery = 5,
    QueueEvery = 1,
}

-- THE ARENA IS THE PLACE'S OWN LASER TAG CONTENT: Tools named Blaster/AutoBlaster, the Teams service and the
-- SpawnLocations the checked-in base place already ships. Nothing here creates, renames or clones any of it --
-- it reads what is there and says plainly when it is not, so a round either has an arena or has a reason. This
-- closes the gap with SHANKPIT-460: a round you can actually play, not only a ticket to one.
-- A Tool inside a player's Backpack is not the place's loadout: it is what somebody is carrying, and it sits
-- under Workspace the moment they equip it. Counting it would let one player's inventory become the arena.
local function insidePlayer(inst)
    local p = inst.Parent
    while p ~= nil do
        if p.ClassName == "Player" then return true end
        p = p.Parent
    end
    return false
end

local function collectArena()
    local tree = { teams = {}, spawns = {}, tools = {} }
    for _, serviceName in ipairs({ "Workspace", "ServerStorage", "StarterPack" }) do
        local okService, service = pcall(function() return game:GetService(serviceName) end)
        if okService then
            local okDesc, descendants = pcall(function() return service:GetDescendants() end)
            if okDesc then
                for _, inst in ipairs(descendants) do
                    if inst.ClassName == "SpawnLocation" then
                        tree.spawns[#tree.spawns + 1] = {
                            name = inst.Name,
                            team = (inst.Team ~= nil) and inst.Team.Name or nil,
                        }
                    elseif inst.ClassName == "Tool" and not insidePlayer(inst) then
                        tree.tools[#tree.tools + 1] = { name = inst.Name }
                    end
                end
            end
        end
    end
    local okTeams, teamsService = pcall(function() return game:GetService("Teams") end)
    if okTeams then
        local okList, list = pcall(function() return teamsService:GetTeams() end)
        if okList then
            for _, team in ipairs(list) do tree.teams[#tree.teams + 1] = { name = team.Name } end
        end
    end
    return tree
end

local ARENA = arena460.describe(collectArena())
if not ARENA.ok then
    warn("arena460: this place has no usable arena -- " .. table.concat(ARENA.missing, ",") ..
         " (players can still queue; a round will say why it cannot start)")
end

-- The arena is not read once and frozen: a Studio session adds its pads after this script starts, and a publish
-- can take a team away. Only pads, tools and teams matter, and the loop re-reads at most every two seconds, so a
-- place full of effects does not turn GetDescendants into a per-frame job. `arena460.refresh` keeps the last
-- usable arena when a re-read comes back worse; this only decides WHEN to look again.
local ARENA_MIN_REREAD_MS = 2000
local arenaDirty, arenaStale = false, (ARENA.stale == true)
local function arenaRelevant(inst)
    return inst.ClassName == "SpawnLocation" or inst.ClassName == "Tool"
end
for _, serviceName in ipairs({ "Workspace", "ServerStorage", "StarterPack" }) do
    pcall(function()
        local service = game:GetService(serviceName)
        service.DescendantAdded:Connect(function(inst)
            if arenaRelevant(inst) then arenaDirty = true end
        end)
        service.DescendantRemoved:Connect(function(inst)
            if arenaRelevant(inst) then arenaDirty = true end
        end)
    end)
end
pcall(function()
    local teamsService = game:GetService("Teams")
    teamsService.TeamAdded:Connect(function() arenaDirty = true end)
    teamsService.TeamRemoved:Connect(function() arenaDirty = true end)
end)

local queue = queue460.new()
local snapshot = nil
local seats = {}          -- [player] = { user_id, uuid, name, mode, ticket, expires_at, queued_at }
local identities = {}     -- [player] = login460 identity (player_id, token, expires_at, guest_secret)
local savedFor = {}       -- [user_id] = what may be persisted (player_id + guest_secret, never the token)

-- Identity persistence. DataStore is optional on purpose: a place that cannot reach it still runs, and the
-- ONLY thing lost is that a returning player is a new guest instead of the same one.
local storeOk, identityStore = pcall(function()
    return game:GetService("DataStoreService"):GetDataStore("SHANKPIT460_Identity_v1")
end)
if not storeOk then identityStore = nil end

-- WOTAN and IDUNA are the truth; this is the place's cache of what they publish (src/stats460.lua). The board
-- is still pushed whole to the screens; the fan-out is for anything that needs a per-record write.
local statsCache = stats460.new()
local statsSinks = {}
do
    local haveStore, statsStore = pcall(function()
        return game:GetService("DataStoreService"):GetDataStore("SHANKPIT460_Stats_v1")
    end)
    if haveStore then
        statsSinks[#statsSinks + 1] = {
            name = "datastore",
            write = function(record) statsStore:SetAsync("p" .. tostring(record.pid), record) end,
        }
    end
end

local function loadSaved(userId)
    if not identityStore then return nil end
    local ok, saved = pcall(function() return identityStore:GetAsync("u" .. tostring(userId)) end)
    if not ok or type(saved) ~= "table" then return nil end
    return saved
end

local function saveIdentity(userId, identity)
    local saved = login460.to_save(identity)
    if not saved or not identityStore then return end
    savedFor[userId] = saved
    pcall(function() identityStore:SetAsync("u" .. tostring(userId), saved) end)
end

local function event(name)
    local e = Instance.new("RemoteEvent")
    e.Name = name
    e.Parent = ReplicatedStorage
    return e
end

local matchFound = event("SHANKPIT460_MatchFound")
local boardEvent = event("SHANKPIT460_Board")
local queueEvent = event("SHANKPIT460_Queue")

local function nowMs()
    return math.floor(os.clock() * 1000)
end

local function httpJson(url, method, body, headers)
    if url == "" then return nil, "not-configured" end
    local ok, res = pcall(function()
        return HttpService:RequestAsync({
            Url = url,
            Method = method or "GET",
            Headers = headers or { ["Content-Type"] = "application/json" },
            Body = body,
        })
    end)
    if not ok then return nil, "http-failed" end
    if not res.Success then return nil, "http-" .. tostring(res.StatusCode) end
    local decoded
    local decodedOk = pcall(function()
        decoded = HttpService:JSONDecode(res.Body)
    end)
    if not decodedOk then return nil, "bad-json" end
    return decoded, nil
end

-- Step 2. IDUNA mints; this server only reads what came back, and only through the module that knows the bytes.
local function mintTicket(token)
    if type(token) ~= "string" or token == "" then return { ok = false, reason = "no-token" } end
    local decoded, err = httpJson(CONFIG.TicketUrl, "POST", "{}", {
        ["Authorization"] = "Bearer " .. token,
        ["Content-Type"] = "application/json",
    })
    if not decoded then return { ok = false, reason = err or "no-answer" } end
    return ticket460.from_mint(decoded, os.time())
end

local function pushBoard()
    if not snapshot then return end
    boardEvent:FireAllClients({
        rows = net460.rows(snapshot, 10),
        population = net460.population(snapshot),
        supports = net460.supports(snapshot),
        stale = net460.stale(snapshot, nowMs()),
    })
end

local function refreshSnapshot()
    local decoded, err = httpJson(CONFIG.NetUrl, "GET", nil)
    if type(decoded) == "table" then
        snapshot = decoded
        local records = type(decoded.records) == "table" and decoded.records or {}
        -- `recent[]` is the per-match ticker (pid0/pid1/winner/ms). `results[]` is an aggregate per challenge
        -- kind and cannot drive a per-player update, so it is deliberately not read here.
        local recent = type(decoded.recent) == "table" and decoded.recent or {}
        if not statsCache.booted then
            -- Cold start: load the ladder, REMEMBER the ticker, replay nothing. Those matches already happened.
            stats460.boot(statsCache, records, recent)
        else
            local applied = 0
            for _, match in ipairs(recent) do
                if stats460.apply_result(statsCache, match, nowMs()) then applied = applied + 1 end
            end
            local reconciled = stats460.reconcile(statsCache, records, nowMs())
            local pids = {}
            for _, record in ipairs(records) do
                if record.pid ~= nil then pids[#pids + 1] = stats460.key(record.pid) end
            end
            local fanned = stats460.fan_out(statsCache, pids, statsSinks)
            if applied > 0 or reconciled.conflicts > 0 or reconciled.expired > 0 or #fanned.failed > 0 then
                print(string.format(
                    "stats460: %d results, %d confirmed, %d conflicts, %d expired; fan-out %d ok, %d failed",
                    applied, reconciled.confirmed, reconciled.conflicts, reconciled.expired,
                    fanned.ok, #fanned.failed))
            end
        end
    elseif err then
        -- a dead authority leaves the cache exactly as it was: stale is readable, invented is not
        print("stats460: board read failed (" .. tostring(err) .. ") -- keeping the last cache")
    end
    pushBoard()
end

-- Step 1. Sign in for real. Reuse what was saved for this UserId, otherwise register a real IDUNA player.
local function ensureIdentity(player)
    local existing = identities[player]
    if existing and login460.token_ok(existing, os.time()) then return existing, nil end

    local saved = savedFor[player.UserId] or loadSaved(player.UserId)
    if saved then
        local identity, why, wasSaved = login460.identity_for(
            function(method, url, body, headers) return httpJson(url, method, body, headers) end,
            CONFIG.GameUrl, { [player.UserId] = saved }, player.UserId, os.time())
        if identity then
            identities[player] = identity
            saveIdentity(player.UserId, identity)
            return identity, nil
        end
        -- A saved secret IDUNA no longer accepts is a real refusal; registering a second account behind the
        -- player's back would silently split their history, so it is reported instead.
        if wasSaved or why == "http-401" or why == "http-403" then return nil, "signin:" .. tostring(why) end
    end

    local identity, why = login460.sign_in(
        function(method, url, body, headers) return httpJson(url, method, body, headers) end,
        CONFIG.GameUrl, player.DisplayName, os.time())
    if not identity then return nil, "signup:" .. tostring(why) end
    identities[player] = identity
    saveIdentity(player.UserId, identity)
    return identity, nil
end

local function dropSeat(player)
    local seat = seats[player]
    if not seat then return end
    queue460.remove(queue, seat.mode, seat.user_id)
    seats[player] = nil
end

Players.PlayerAdded:Connect(function(player)
    player:SetAttribute("SHANKPIT460_State", "needs-ticket")
    player:SetAttribute("SHANKPIT460_Pid", "")
end)

queueEvent.OnServerEvent:Connect(function(player, action, arg)
    if action == "signin" then
        local identity, why = ensureIdentity(player)
        if not identity then
            player:SetAttribute("SHANKPIT460_State", "refused:" .. tostring(why))
            return
        end
        player:SetAttribute("SHANKPIT460_Pid", identity.player_id)
        player:SetAttribute("SHANKPIT460_State", "signed-in")
        queueEvent:FireClient(player, {
            signed_in = true,
            player_id = identity.player_id,
            display_name = identity.display_name,
            account_state = identity.account_state,
            email = identity.email,
        })
    elseif action == "link" then
        -- "Keep this account": IDUNA adds credentials to the SAME player row, so history carries over.
        -- The password is forwarded once and never stored by this server or this place.
        local identity = identities[player]
        if not identity then identity, _ = ensureIdentity(player) end
        if not identity then
            player:SetAttribute("SHANKPIT460_State", "refused:not-signed-in")
            return
        end
        local linked, why = login460.link_email(
            function(method, url, body, headers) return httpJson(url, method, body, headers) end,
            CONFIG.GameUrl, identity,
            tostring(arg and arg.email or ""), tostring(arg and arg.password or ""), os.time())
        if not linked then
            player:SetAttribute("SHANKPIT460_State", "refused:link:" .. tostring(why))
            queueEvent:FireClient(player, { link_error = tostring(why) })
            return
        end
        identities[player] = linked
        saveIdentity(player.UserId, linked)
        player:SetAttribute("SHANKPIT460_State", "linked")
        queueEvent:FireClient(player, { linked = true, email = linked.email })
    elseif action == "join" then
        local mode = tostring(arg and arg.mode or "casual")
        -- A hand-passed token still works (a player who signed in on IDUNA's own page), otherwise this
        -- server signs the player in itself.
        local identity = identities[player]
        if type(arg) == "table" and type(arg.token) == "string" and arg.token ~= "" then
            identity = nil
        else
            identity, _ = ensureIdentity(player)
        end
        if not identity and not (type(arg) == "table" and arg.token) then
            player:SetAttribute("SHANKPIT460_State", "refused:not-signed-in")
            return
        end
        local verdict = mintTicket(type(arg) == "table" and arg.token or identity.token)
        if not verdict.ok then
            player:SetAttribute("SHANKPIT460_State", "refused:" .. tostring(verdict.reason))
            return
        end
        -- The ticket is for whoever IDUNA says the token belongs to; a mismatch means the token and the
        -- seat disagree and the player is not sent anywhere.
        if identity and verdict.player_id ~= identity.player_id then
            player:SetAttribute("SHANKPIT460_State", "refused:signed-in-as-someone-else")
            return
        end
        local rating = nil
        -- A rating needs the world's ladder row for this identity, and the ladder is keyed by the world's
        -- own pid -- a binding that does not exist yet. The queue's default (1000, the ladder's own
        -- rating_start) stands in rather than a made-up number.
        local entry, why = queue460.enqueue(queue, mode, {
            pid = player.UserId,
            rating = rating,
            name = (identity and identity.display_name ~= "" and identity.display_name) or player.Name,
            joined_ms = nowMs(),
        })
        if not entry then
            player:SetAttribute("SHANKPIT460_State", "queue:" .. tostring(why))
            return
        end
        seats[player] = {
            user_id = player.UserId,
            uuid = verdict.player_id,
            name = (identity and identity.display_name ~= "" and identity.display_name) or player.Name,
            mode = mode,
            ticket = verdict.hex,
            expires_at = verdict.expires_at,
            queued_at = entry.joined_ms,
        }
        player:SetAttribute("SHANKPIT460_State", "queued:" .. mode)
        player:SetAttribute("SHANKPIT460_Pid", verdict.player_id)
        queueEvent:FireClient(player, {
            position = queue460.position(queue, mode, player.UserId),
            mode = mode,
            rating = entry.rating,
            expires_at = verdict.expires_at,
        })
    elseif action == "leave" then
        dropSeat(player)
        player:SetAttribute("SHANKPIT460_State", "needs-ticket")
    end
end)

Players.PlayerRemoving:Connect(dropSeat)

-- One loop: the queue every second, the board every five. A client can neither seat itself nor declare a result.
task.spawn(function()
    local lastQueue, lastBoard, lastArena = 0, 0, 0
    while true do
        local now = nowMs()
        if arenaDirty and now - lastArena >= ARENA_MIN_REREAD_MS then
            lastArena = now
            arenaDirty = false
            ARENA = arena460.refresh(ARENA, collectArena())
            if (ARENA.stale == true) ~= arenaStale then
                arenaStale = (ARENA.stale == true)
                if arenaStale then
                    warn("arena460: this place no longer reads as a usable arena (" ..
                         table.concat(ARENA.fresh_missing or {}, ",") ..
                         ") -- rounds keep the last arena it did")
                else
                    print(string.format("arena460: arena re-read -- %d pads, %d weapons, %d teams",
                        #ARENA.spawns, #ARENA.weapons, #ARENA.teams))
                end
            end
        end
        if now - lastQueue >= CONFIG.QueueEvery * 1000 then
            lastQueue = now
            local nowS = os.time()
            for _, mode in ipairs({ "casual", "ranked" }) do
                local tick = queue460.tick(queue, mode, now)
                for _, p in ipairs(tick.pairs) do
                    -- Each side gets ITS OWN team and pad. The payload used to carry one `arena_spawn` for both,
                    -- which stands two players on the same team's spawn and calls it a match.
                    local pair = arena460.assign_pair(ARENA,
                        { user_id = p.first.pid, name = p.first.name },
                        { user_id = p.second.pid, name = p.second.name })
                    -- The place's own loadout, so the payload carries every blaster it offers rather than an
                    -- assumed "first one is the only one".
                    local kit = ARENA.ok and arena460.loadout(ARENA) or nil
                    for player, seat in pairs(seats) do
                        if seat.user_id == p.first.pid or seat.user_id == p.second.pid then
                            local iAmFirst = (seat.user_id == p.first.pid)
                            local mine = pair and pair.by_pid[tostring(seat.user_id)] or nil
                            local placed, placeWhy = nil, nil
                            if mine then placed, placeWhy = arena460.placement(ARENA, mine) end
                            if placed then
                                -- Roblox's own two properties do the placing: Team chooses the side, RespawnLocation
                                -- chooses the pad the engine respawns this body at. Nothing is teleported and no
                                -- respawn is forced -- when a body comes back is the place's round, not this lobby.
                                local positioned = pcall(function()
                                    for _, team in ipairs(game:GetService("Teams"):GetTeams()) do
                                        if team.Name == placed.team then player.Team = team end
                                    end
                                    for _, inst in ipairs(game:GetService("Workspace"):GetDescendants()) do
                                        if inst.ClassName == "SpawnLocation" and inst.Name == placed.respawn then
                                            player.RespawnLocation = inst
                                        end
                                    end
                                end)
                                if not positioned then
                                    warn("arena460: could not set team/pad for " .. player.Name .. " (" ..
                                         placed.team .. " / " .. placed.respawn .. ")")
                                end
                            elseif placeWhy then
                                warn("arena460: " .. player.Name .. " has no placement -- " .. placeWhy)
                            end
                            -- A ticket has a 5-minute life and was minted when they queued, so tell each side
                            -- what is actually left rather than what it was at mint time.
                            local remaining = ticket460.usable(seat.ticket, nowS)
                            matchFound:FireClient(player, {
                                mode = mode,
                                uuid = seat.uuid,
                                opponent = iAmFirst and p.second.name or p.first.name,
                                opponent_rating = iAmFirst and p.second.rating or p.first.rating,
                                gap = p.gap,
                                ticket = seat.ticket,
                                expires_at = seat.expires_at,
                                ticket_ok = remaining.ok,
                                ticket_reason = remaining.ok and nil or remaining.reason,
                                -- What the round needs from the place itself, read once from its own Laser Tag
                                -- content. nil + a reason when the place cannot host one; never a default.
                                arena_ok = ARENA.ok,
                                arena_team = placed and placed.team or nil,
                                arena_spawn = placed and placed.respawn or nil,
                                arena_weapon = kit and kit.weapons[1] or nil,
                                arena_weapons = kit and kit.weapons or nil,
                                arena_teams = ARENA.ok and ARENA.teams or nil,
                                arena_reason = ARENA.ok and nil or table.concat(ARENA.missing, ","),
                            })
                            player:SetAttribute("SHANKPIT460_State", "matched:" .. mode)
                            seats[player] = nil
                        end
                    end
                end
                for _, e in ipairs(tick.expired) do
                    for player, seat in pairs(seats) do
                        if seat.user_id == e.pid then
                            player:SetAttribute("SHANKPIT460_State", "expired")
                            seats[player] = nil
                        end
                    end
                end
                for _, e in ipairs(tick.fills) do
                    -- casual only; the body STAYS queued, because a human who arrives first still wins.
                    queueEvent:FireAllClients({ fillable = { pid = e.pid, name = e.name, mode = mode } })
                end
            end
        end
        if now - lastBoard >= CONFIG.BoardEvery * 1000 then
            lastBoard = now
            refreshSnapshot()
        end
        task.wait(0.25)
    end
end)

refreshSnapshot()
