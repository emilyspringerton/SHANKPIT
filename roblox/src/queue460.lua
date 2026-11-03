-- src/queue460.lua -- the 460 queue: the LISTS live here, the RULES do not.
--
-- SHANKPIT-460 DNA (shankpit-460/docs2/NORTHSTAR.md, S156-03): "basic matchmaking as a simplified
-- VS2-lifecycle-shaped queue". This is that queue and nothing else -- no board, no card game, no deck, no
-- citizen life. NOOBTOPIA already owns the wide version of this world; the Roblox SHANKPIT surface is
-- deliberately the narrow one.
--
-- EVERY THRESHOLD IN HERE IS SOMEWHERE ELSE, ON PURPOSE. `queue460_rules.lua` is emitted from
-- `PARENA/stdlib/shankpit/queue_rules.prn` by `roblox/scripts/gen_queue460.sh`, so the window's base, its
-- growth, its cap, the expiry and the fill delay are typed once and compared once. What is left in this file is
-- what PARENA is not for: a list of seats, a closest-pair search, and bookkeeping. If a number ever appears in
-- this file that also appears in the .prn, the copy is the bug -- same rule `deadweight_rating.lua` and
-- `deadweight_stage.lua` already live by in NOOBTOPIA.
--
-- PURE: no network, no clock of its own (every call takes `now_ms`), no `game`, no `workspace`. That is what
-- lets `roblox/tests/queue460_test.lua` pin the numbers by hand under plain Lua.

local rules = require("queue460_rules")

local M = {}

-- Exposed so a test (or a host) can prove the module is running the generated rules rather than a stale copy.
M.RULES = rules

M.MODES = { casual = true, ranked = true }

-- Rating points of tolerance after waiting wait_ms. Delegates; kept as a function so callers never see floats
-- reach the integer rules.
function M.window(wait_ms)
    return rules.queueWindow(math.floor(tonumber(wait_ms) or 0))
end

function M.new()
    return { next_id = 1, casual = {}, ranked = {} }
end

-- One seat per body per mode: a body in casual may also queue ranked, but never twice in the same line.
function M.enqueue(q, mode, entry)
    if not M.MODES[mode] then return nil, "mode" end
    if type(entry) ~= "table" then return nil, "entry" end
    local pid = tonumber(entry.pid)
    if not pid then return nil, "pid" end
    local list = q[mode]
    for i = 1, #list do
        if list[i].pid == pid then return nil, "duplicate" end
    end
    local e = {
        id = q.next_id,
        pid = pid,
        -- 1000 is the ladder's own starting rating (`rating_start()` in
        -- PARENA/stdlib/deadweight/rating.prn); a caller that has a real one passes it.
        rating = tonumber(entry.rating) or 1000,
        name = tostring(entry.name or ""),
        joined_ms = tonumber(entry.joined_ms) or 0,
    }
    q.next_id = q.next_id + 1
    list[#list + 1] = e
    return e
end

function M.remove(q, mode, pid)
    local list = q[mode]
    if not list then return nil end
    local want = tonumber(pid)
    for i = 1, #list do
        if list[i].pid == want then
            return table.remove(list, i)
        end
    end
    return nil
end

function M.position(q, mode, pid)
    local list = q[mode]
    if not list then return nil end
    local want = tonumber(pid)
    for i = 1, #list do
        if list[i].pid == want then return i end
    end
    return nil
end

-- The closest legal pair in this line, or nil. Whether a gap is legal is `queue-gap-fits`'s call (the strict,
-- shorter-waiting window), so this only searches. Ties on the gap go to the earlier-seated body, then to the
-- lower pid -- a fixed order, so the same queue always hands back the same pair.
function M.pair(q, mode, now_ms)
    local list = q[mode]
    if not list or #list < 2 then return nil end
    local bi, bj, bgap = nil, nil, nil
    for i = 1, #list - 1 do
        for j = i + 1, #list do
            local a, b = list[i], list[j]
            local gap = math.abs(a.rating - b.rating)
            if rules.queueGapFits(gap, now_ms - a.joined_ms, now_ms - b.joined_ms) == 1 then
                local better = false
                if bgap == nil then
                    better = true
                elseif gap < bgap then
                    better = true
                elseif gap == bgap then
                    local cur = list[bi]
                    if a.id < cur.id then better = true
                    elseif a.id == cur.id and b.id < list[bj].id then better = true end
                end
                if better then bi, bj, bgap = i, j, gap end
            end
        end
    end
    if not bi then return nil end
    local second = table.remove(list, bj)
    local first = table.remove(list, bi)
    return { first = first, second = second, gap = bgap }
end

-- One tick of a line: release the expired, hand back every legal pair, and name who is fillable. Expiry and
-- fillability are the rules module's answers -- including "ranked is never fillable", which is a policy this
-- file must not be able to get wrong.
function M.tick(q, mode, now_ms)
    local out = { pairs = {}, expired = {}, fills = {} }
    local list = q[mode]
    if not list then return out end
    local i = 1
    while i <= #list do
        if rules.queueExpired(now_ms - list[i].joined_ms) == 1 then
            out.expired[#out.expired + 1] = table.remove(list, i)
        else
            i = i + 1
        end
    end
    while true do
        local p = M.pair(q, mode, now_ms)
        if not p then break end
        out.pairs[#out.pairs + 1] = p
    end
    local ranked = (mode == "ranked") and 1 or 0
    for k = 1, #list do
        if rules.queueFillable(ranked, now_ms - list[k].joined_ms) == 1 then
            out.fills[#out.fills + 1] = list[k]
        end
    end
    return out
end

return M
