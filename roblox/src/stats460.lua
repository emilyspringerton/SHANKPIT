-- src/stats460.lua -- the Roblox side of the stats as a CACHE that is allowed to run ahead of the truth.
--
-- The ordering this file assumes, stated once because everything else follows from it:
--
--     IDUNA and WOTAN are the authority.  The place is a cache of what they publish.
--
-- So the cache may write optimistically -- a match that the world has published is applied NOW, so the player
-- sees their win on this frame instead of waiting for the next board read -- but the cache never becomes the
-- record. `reconcile()` is the only thing that sets a value to authority truth, and `rollback()` exists because
-- an optimistic write the authority refuses must be undone rather than forgotten.
--
-- Two things this file refuses to do, both because a fabricated stat is worse than a blank one:
--   * invent a field: a delta may only move a numeric field the record ALREADY carries;
--   * invent a rating: `recent[]` says who won, not by how much Elo, so results move wins/games only and the
--     rating stays whatever the authority last published.
--
-- WHERE THE RESULTS COME FROM (checked against the publisher, not assumed): `packages/simulation/dw_net.c`
-- writes `results[]` as an aggregate per challenge kind ({kind, kind_id, matches}) and `recent[]` as the
-- per-match ticker ({kind, pid0, pid1, winner, rounds, hull0, hull1, ms}, newest first). Only `recent[]` can
-- drive a per-player update; `results[]` cannot and is not used for one.
--
-- Pids: the publisher emits pids as NUMBERS. Keys here are normalised through `M.key()` so a numeric pid, a
-- string pid and the same player in two shapes are one cache entry -- the place was dropped on this exact
-- distinction once already.
--
-- No HTTP, no DataStore, no game: callers pass records in and sinks out, which is what lets this be tested
-- under plain Lua (roblox/tests/stats460_test.lua).

local M = {}

-- How long an unacknowledged write may stand. Past this the authority has had several publication intervals
-- to answer and did not, so the write is treated as lost and the previous value is restored.
M.PENDING_TTL_MS = 30000

-- How many result keys are remembered for de-duplication. `recent[]` is capped and re-sent every poll, so the
-- window only has to outlast the ticker.
M.SEEN_MAX = 64

function M.key(pid)
    if pid == nil then return nil end
    return tostring(pid)
end

local function shallow(t)
    local o = {}
    for k, v in pairs(t) do o[k] = v end
    return o
end

local function find_pending(cache, key)
    for i, p in ipairs(cache.pending) do
        if p.pid == key then return p, i end
    end
    return nil, nil
end

local function remember(cache, key)
    cache.seen = cache.seen or {}
    cache.seen_order = cache.seen_order or {}
    if cache.seen[key] then return false end
    cache.seen[key] = true
    cache.seen_order[#cache.seen_order + 1] = key
    if #cache.seen_order > M.SEEN_MAX then
        local dropped = table.remove(cache.seen_order, 1)
        cache.seen[dropped] = nil
    end
    return true
end

-- cache.records: key(pid) -> record (the authority's shape, plus any optimistic edit on top)
-- cache.pending: writes the authority has not answered yet, at most one per player (deltas merge into it)
function M.new(opts)
    opts = opts or {}
    return {
        records = {},
        pending = {},
        seen = {},
        seen_order = {},
        booted = false,
        seq = 0,
        clock = opts.clock,
        log = opts.log,
    }
end

function M.get(cache, pid)
    local r = cache.records[M.key(pid)]
    return r and shallow(r) or nil
end

-- Load authority truth with no optimistic intent (first paint, or a record nobody has touched).
function M.put(cache, record)
    if type(record) ~= "table" then return nil, "no-record" end
    local key = M.key(record.pid)
    if not key or key == "" then return nil, "no-pid" end
    cache.records[key] = shallow(record)
    return shallow(record)
end

function M.is_pending(cache, pid)
    return (find_pending(cache, M.key(pid))) ~= nil
end

-- Apply a delta immediately. Only numeric fields the record already carries move; the skipped count is reported
-- so a caller can see that part of its delta had nowhere to land. A second write while one is pending MERGES
-- into it (keeping the original `before`), because two matches can finish inside one publication interval and
-- refusing the second would silently drop a real result.
function M.optimistic(cache, pid, delta, now_ms)
    local key = M.key(pid)
    local rec = cache.records[key]
    if not rec then return nil, "unknown-player" end
    if type(delta) ~= "table" then return nil, "no-delta" end
    -- captured BEFORE the loop mutates anything: this is the value rollback restores and confirmation compares
    -- against, and taking it after the mutation made both of them compare against the incremented number
    local before = shallow(rec)
    local pending = find_pending(cache, key)
    local applied, touched, skipped = {}, 0, 0
    for k, d in pairs(delta) do
        if k ~= "pid" and type(d) == "number" then
            if type(rec[k]) == "number" then
                rec[k] = rec[k] + d
                applied[k] = d
                touched = touched + 1
            else
                skipped = skipped + 1
            end
        end
    end
    if touched == 0 then return nil, "nothing-applied" end
    if pending then
        for k, d in pairs(applied) do
            pending.delta[k] = (pending.delta[k] or 0) + d
            pending.applied[k] = (pending.applied[k] or 0) + d
        end
        pending.at_ms = now_ms
        return shallow(rec), nil, pending
    end
    cache.seq = cache.seq + 1
    local entry = {
        pid = key, seq = cache.seq, before = before, delta = shallow(delta),
        applied = applied, at_ms = now_ms, skipped = skipped,
    }
    cache.pending[#cache.pending + 1] = entry
    return shallow(rec), nil, entry
end

-- The authority accepted it: the value already in the cache is truth now, so only the pending mark goes.
function M.ack(cache, pid)
    local _, i = find_pending(cache, M.key(pid))
    if not i then return false end
    table.remove(cache.pending, i)
    return true
end

-- The authority refused it: put back exactly what was there.
function M.rollback(cache, pid)
    local p, i = find_pending(cache, M.key(pid))
    if not p then return false, "no-pending" end
    cache.records[p.pid] = p.before
    table.remove(cache.pending, i)
    return true
end

-- A key for one finished match. `ms` is the match's own timestamp from the publisher, so the same match seen
-- on two polls is the same key, and two matches between the same pair are not.
function M.result_key(m)
    if type(m) ~= "table" then return nil end
    local p0, p1, w = tonumber(m.pid0), tonumber(m.pid1), tonumber(m.winner)
    if not (p0 and p1 and w) then return nil end
    local ms = tonumber(m.ms) or 0
    return string.format("%d:%d:%d:%d", p0, p1, w, ms)
end

-- Fast path for one published match: wins/games for the winner, games for the loser, games for a draw,
-- nothing at all for a player the cache has not seen (reported, and remembered so a later record cannot
-- retro-apply a match that was already counted as history).
function M.apply_result(cache, m, now_ms)
    local key = M.result_key(m)
    if not key then return nil, "not-a-result" end
    if cache.seen[key] then return nil, "already-seen" end
    remember(cache, key)
    local winner = tonumber(m.winner)
    local pid0, pid1 = M.key(tonumber(m.pid0)), M.key(tonumber(m.pid1))
    local d0 = { games = 1 }
    local d1 = { games = 1 }
    if winner == 0 then d0.wins = 1 elseif winner == 1 then d1.wins = 1 end
    local _, why0 = M.optimistic(cache, pid0, d0, now_ms)
    local _, why1 = M.optimistic(cache, pid1, d1, now_ms)
    return {
        key = key, pid0 = pid0, pid1 = pid1, winner = winner,
        applied0 = why0 == nil, applied1 = why1 == nil,
        why0 = why0, why1 = why1,
    }
end

-- First poll: load the ladder, and remember the ticker WITHOUT replaying it. Those matches already happened;
-- counting them as fresh wins on a cold start would invent a burst of results nobody just played.
function M.boot(cache, records, recent)
    local loaded = 0
    for _, record in ipairs(records or {}) do
        if type(record) == "table" and M.put(cache, record) then loaded = loaded + 1 end
    end
    for _, m in ipairs(recent or {}) do
        local key = M.result_key(m)
        if key then remember(cache, key) end
    end
    cache.booted = true
    return loaded
end

-- Authority truth replaces the cache. A pending write the authority answered is either CONFIRMED (its numbers
-- agree with what we applied) or a CONFLICT (we were ahead and it disagreed). A pending write it did not
-- mention survives until the TTL, then reverts.
function M.reconcile(cache, server_records, now_ms)
    local report = { written = 0, confirmed = 0, conflicts = 0, expired = 0, kept = 0 }
    local by_key = {}
    for _, sr in ipairs(server_records or {}) do
        if type(sr) == "table" then
            local key = M.key(sr.pid)
            if key and key ~= "" then by_key[key] = sr end
        end
    end

    local function agrees(pending, server_record)
        for field, delta in pairs(pending.applied or {}) do
            local before = tonumber(pending.before[field]) or 0
            if tonumber(server_record[field]) ~= before + delta then return false end
        end
        return true
    end

    local surviving = {}
    for _, p in ipairs(cache.pending) do
        local sr = by_key[p.pid]
        if sr then
            if agrees(p, sr) then report.confirmed = report.confirmed + 1
            else report.conflicts = report.conflicts + 1 end
        elseif (now_ms - p.at_ms) <= M.PENDING_TTL_MS then
            surviving[#surviving + 1] = p
            report.kept = report.kept + 1
        else
            cache.records[p.pid] = p.before
            report.expired = report.expired + 1
        end
    end
    cache.pending = surviving
    for key, sr in pairs(by_key) do
        cache.records[key] = shallow(sr)
        report.written = report.written + 1
    end
    return report
end

-- Interface fan-out: the cache is written once, every reader is told. One sink throwing is reported and the
-- rest still run -- a scoreboard that cannot draw must not stop the match from being recorded.
function M.fan_out(cache, pids, sinks)
    local report = { ok = 0, failed = {} }
    for _, pid in ipairs(pids or {}) do
        local rec = cache.records[M.key(pid)]
        if rec then
            for _, sink in ipairs(sinks or {}) do
                local called, err = pcall(sink.write, shallow(rec))
                if called then
                    report.ok = report.ok + 1
                else
                    report.failed[#report.failed + 1] = { name = sink.name or "?", pid = M.key(pid), err = tostring(err) }
                end
            end
        end
    end
    return report
end

-- Fan-in for the two authorities, in authority order (IDUNA first, then WOTAN). A field only one carries is
-- taken from it; a field they disagree on keeps the higher-authority value and is reported, because averaging
-- two authorities is how a cache starts making up numbers.
function M.merge_sources(sources)
    local out, conflicts = {}, {}
    for _, src in ipairs(sources or {}) do
        for _, rec in ipairs(src.records or {}) do
            local key = type(rec) == "table" and M.key(rec.pid) or nil
            if key and key ~= "" then
                local acc = out[key]
                if not acc then
                    out[key] = shallow(rec)
                else
                    for k, v in pairs(rec) do
                        if k ~= "pid" then
                            if acc[k] == nil then
                                acc[k] = v
                            elseif acc[k] ~= v then
                                conflicts[#conflicts + 1] = {
                                    pid = key, field = k, kept = acc[k], ignored = v, source = src.name or "?",
                                }
                            end
                        end
                    end
                end
            end
        end
    end
    return out, conflicts
end

return M
