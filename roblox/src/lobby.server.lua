-- src/lobby.server.lua -- the glue: tickets minted by IDUNA, seats taken here, pairs read by the world.
--
-- Everything a test can reach is in the modules beside this file. What is here needs a running server: Players,
-- HttpService, RemoteEvents, a loop.
--
-- The 460 flow, end to end:
--   1. a body joins and presents its IDUNA session TOKEN (device-flow result; the place never mints one)
--   2. the server asks IDUNA for a connect ticket with that token as a Bearer, and reads IDUNA's answer through
--      ticket460.from_mint -- which refuses a ticket whose JSON fields disagree with the bytes IDUNA signed
--   3. the body takes a seat in casual or ranked (queue460), keyed by Roblox UserId
--   4. a tick pairs bodies by the widening window; each side is handed ITS OWN ticket to present to the C server
--   5. the published ladder is re-read and pushed to the screens (net460)
--
-- HONEST LIMITS, named where they bite:
--   * Roblox cannot reach 127.0.0.1 -- NetUrl/TicketUrl must be public endpoints.
--   * The place does not mint tokens. A player authenticates with IDUNA out of band (device flow) and hands the
--     resulting token to this server; there is no login screen here yet, which is the next real piece.
--   * There is NO binding yet between a Roblox UserId and a SHANKPIT pid, so a player queues at the default
--     rating (1000) and the board is read-only. Inventing a binding would put a made-up number on a screen.
--   * The arena is the C server. A pairing hands out a ticket; it does not host an FPS.

local Players = game:GetService("Players")
local HttpService = game:GetService("HttpService")
local ReplicatedStorage = game:GetService("ReplicatedStorage")

local queue460 = require(script.Parent.queue460)
local ticket460 = require(script.Parent.ticket460)
local net460 = require(script.Parent.net460)

local CONFIG = {
    NetUrl = "https://okemily.com/api/v1/shankpit-net/deadweight",
    -- IDUNA's connect-ticket mint (IDUNA/internal/http/handlers/shankpit_ticket.go): 72 hex chars, 5 min TTL,
    -- presented once at PACKET_CONNECT and verified by the C server's own shared secret.
    TicketUrl = "https://iam.okemily.com/api/v1/shankpit/ticket",
    BoardEvery = 5,
    QueueEvery = 1,
}

local queue = queue460.new()
local snapshot = nil
local seats = {}          -- [player] = { user_id, uuid, name, mode, ticket, expires_at, queued_at }

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
    local decoded = httpJson(CONFIG.NetUrl, "GET", nil)
    if type(decoded) == "table" then
        snapshot = decoded
    end
    pushBoard()
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
    if action == "join" then
        local mode = tostring(arg and arg.mode or "casual")
        local verdict = mintTicket(arg and arg.token)
        if not verdict.ok then
            player:SetAttribute("SHANKPIT460_State", "refused:" .. tostring(verdict.reason))
            return
        end
        local rating = nil
        -- A record lookup needs a Roblox<->pid binding this layer does not have yet; the queue's own default
        -- (1000, the ladder's rating_start) stands in rather than a made-up rating.
        local entry, why = queue460.enqueue(queue, mode, {
            pid = player.UserId,
            rating = rating,
            name = player.Name,
            joined_ms = nowMs(),
        })
        if not entry then
            player:SetAttribute("SHANKPIT460_State", "queue:" .. tostring(why))
            return
        end
        seats[player] = {
            user_id = player.UserId,
            uuid = verdict.player_id,
            name = player.Name,
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
    local lastQueue, lastBoard = 0, 0
    while true do
        local now = nowMs()
        if now - lastQueue >= CONFIG.QueueEvery * 1000 then
            lastQueue = now
            local nowS = os.time()
            for _, mode in ipairs({ "casual", "ranked" }) do
                local tick = queue460.tick(queue, mode, now)
                for _, p in ipairs(tick.pairs) do
                    for player, seat in pairs(seats) do
                        if seat.user_id == p.first.pid or seat.user_id == p.second.pid then
                            local iAmFirst = (seat.user_id == p.first.pid)
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
