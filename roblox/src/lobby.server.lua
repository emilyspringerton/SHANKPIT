-- src/lobby.server.lua -- the glue. Everything a test can reach lives in the three modules beside this file;
-- what is here is only what needs a running Roblox server: Players, HttpService, RemoteEvents, a loop.
--
-- What this server does, in 460 terms:
--   1. a body joins  -> it brings a connect ticket (an opaque string from IDUNA)
--   2. the ticket's SHAPE is checked locally (ticket460), the real verdict comes from IDUNA over HTTP
--   3. the body takes a seat in casual or ranked (queue460)
--   4. a tick pairs bodies by the widening window; a pair asks the world for a real match
--   5. the published ladder is re-read and pushed to the screens (net460)
--
-- HONEST LIMITS, named here because they are the ones that bite in deployment:
--   * Roblox cannot reach 127.0.0.1. SHANKPIT_NET_URL must be the world's PUBLIC netd endpoint (nginx in front
--     of tools/shankpit_netd.c), not the local daemon a dev box runs.
--   * A pairing hands out a MATCH to join; this file does not implement the FPS. Roblox hosts the lobby, the
--     queue and the board -- the arena stays the C server, which is the whole point of 460's tickets.
--   * IDUNA's ticket endpoint is called as an opaque POST: the response is read for `ok`/`pid`/`name`/`ticket`
--     and nothing else is assumed about its shape.

local Players = game:GetService("Players")
local HttpService = game:GetService("HttpService")
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local RunService = game:GetService("RunService")

local queue460 = require(script.Parent.queue460)
local ticket460 = require(script.Parent.ticket460)
local net460 = require(script.Parent.net460)

local CONFIG = {
    -- The world's published snapshot (dw_net -> tools/shankpit_netd.c). Public host only; see the limit above.
    NetUrl = "https://okemily.com/api/v1/shankpit-net/deadweight",
    -- IDUNA's connect-ticket mint. Empty means "no auth configured": the lobby then refuses everyone rather
    -- than inventing identities, which is the only safe default for a place anyone can join.
    TicketUrl = "",
    BoardEvery = 5,
    QueueEvery = 1,
}

local queue = queue460.new()
local snapshot = nil
local seats = {}      -- [player] = { pid = n, name = s, mode = "casual"|"ranked", queued_at = ms }

local matchFound = Instance.new("RemoteEvent")
matchFound.Name = "SHANKPIT460_MatchFound"
matchFound.Parent = ReplicatedStorage

local boardEvent = Instance.new("RemoteEvent")
boardEvent.Name = "SHANKPIT460_Board"
boardEvent.Parent = ReplicatedStorage

local queueEvent = Instance.new("RemoteEvent")
queueEvent.Name = "SHANKPIT460_Queue"
queueEvent.Parent = ReplicatedStorage

local function nowMs()
    return math.floor(os.clock() * 1000)
end

local function httpJson(url, method, body)
    if url == "" then return nil, "not-configured" end
    local ok, res = pcall(function()
        return HttpService:RequestAsync({
            Url = url,
            Method = method or "GET",
            Headers = { ["Content-Type"] = "application/json" },
            Body = body,
        })
    end)
    if not ok then return nil, "http-failed" end
    if not res.Success then return nil, "http-" .. tostring(res.StatusCode) end
    local decoded
    local decodedOk, err = pcall(function()
        decoded = HttpService:JSONDecode(res.Body)
    end)
    if not decodedOk then return nil, "bad-json" end
    return decoded, nil
end

-- The real verdict. The place never validates a signature itself (see ticket460's own header).
local function verifyTicketWithIduna(ticket, player)
    if CONFIG.TicketUrl == "" then return { ok = false, reason = "not-configured" } end
    local body = HttpService:JSONEncode({ ticket = ticket, place = "shankpit-roblox", user_id = player.UserId })
    local decoded, err = httpJson(CONFIG.TicketUrl, "POST", body)
    if not decoded then return { ok = false, reason = err or "no-answer" } end
    return {
        ok = decoded.ok == true,
        pid = tonumber(decoded.pid),
        name = tostring(decoded.name or player.Name),
        ticket = decoded.ticket,
    }
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

Players.PlayerAdded:Connect(function(player)
    player:SetAttribute("SHANKPIT460_State", "needs-ticket")
    player:SetAttribute("SHANKPIT460_Pid", 0)
end)

-- The client hands its ticket over once it has one; the server decides, and a refusal is a refusal.
queueEvent.OnServerEvent:Connect(function(player, action, arg)
    if action == "join" then
        local mode = tostring(arg and arg.mode or "casual")
        local ticket = arg and arg.ticket
        local verdict, consulted = ticket460.accept(ticket, function(t)
            return verifyTicketWithIduna(t, player)
        end)
        if not verdict.ok then
            player:SetAttribute("SHANKPIT460_State", "refused:" .. tostring(verdict.reason))
            return
        end
        local seat = {
            pid = verdict.pid,
            name = verdict.name ~= "" and verdict.name or player.Name,
            mode = mode,
            queued_at = nowMs(),
        }
        local rating = nil
        local rec = snapshot and net460.record_for(snapshot, seat.pid) or nil
        if rec then rating = tonumber(rec.rating) end
        local entry, why = queue460.enqueue(queue, mode, {
            pid = seat.pid, rating = rating, name = seat.name, joined_ms = seat.queued_at,
        })
        if not entry then
            player:SetAttribute("SHANKPIT460_State", "queue:" .. tostring(why))
            return
        end
        seats[player] = seat
        player:SetAttribute("SHANKPIT460_State", "queued:" .. mode)
        player:SetAttribute("SHANKPIT460_Pid", seat.pid)
        queueEvent:FireClient(player, {
            position = queue460.position(queue, mode, seat.pid),
            mode = mode,
            rating = entry.rating,
            verifierConsulted = consulted,
        })
    elseif action == "leave" then
        local seat = seats[player]
        if seat then
            queue460.remove(queue, seat.mode, seat.pid)
            seats[player] = nil
        end
        player:SetAttribute("SHANKPIT460_State", "needs-ticket")
    end
end)

Players.PlayerRemoving:Connect(function(player)
    local seat = seats[player]
    if seat then
        queue460.remove(queue, seat.mode, seat.pid)
        seats[player] = nil
    end
end)

-- One loop for the queue, one for the board. Both run on the server; a client can neither seat itself nor
-- declare who won.
task.spawn(function()
    local lastQueue, lastBoard = 0, 0
    while RunService:IsRunning() or true do
        local now = nowMs()
        if now - lastQueue >= CONFIG.QueueEvery * 1000 then
            lastQueue = now
            for _, mode in ipairs({ "casual", "ranked" }) do
                local tick = queue460.tick(queue, mode, now)
                for _, p in ipairs(tick.pairs) do
                    for player, seat in pairs(seats) do
                        if seat.pid == p.first.pid or seat.pid == p.second.pid then
                            seats[player] = nil
                            matchFound:FireClient(player, {
                                mode = mode,
                                pid = seat.pid,
                                opponent = (seat.pid == p.first.pid) and p.second.name or p.first.name,
                                opponent_rating = (seat.pid == p.first.pid) and p.second.rating or p.first.rating,
                                gap = p.gap,
                            })
                            player:SetAttribute("SHANKPIT460_State", "matched:" .. mode)
                        end
                    end
                end
                for _, e in ipairs(tick.expired) do
                    player:SetAttribute("SHANKPIT460_State", "expired")
                end
                for _, e in ipairs(tick.fills) do
                    -- casual only, and the body STAYS queued: a human who arrives first still wins.
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
