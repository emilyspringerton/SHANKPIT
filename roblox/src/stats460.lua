-- src/stats460.lua -- the Roblox side of the stats as a CACHE that is allowed to run ahead of the truth.
--
-- The ordering this file assumes, stated once because everything else follows from it:
--
--     IDUNA and WOTAN are the authority.  The place is a cache of what they publish.
--
-- So the cache may write optimistically -- a match ends, the local record is updated NOW so the player sees
-- their win on the same frame -- but the cache never becomes the record. `reconcile()` is the only thing that
-- sets a value to authority truth, and `rollback()` exists because an optimistic write that the authority
-- refuses must be undone rather than forgotten. Nothing here is allowed to invent a stat: a field the record
-- does not already carry as a number is left absent, because a fabricated rating is worse than a blank one.
--
-- No HTTP, no DataStore, no game: callers pass records in and sinks out, so this is testable under plain Lua
-- (rogues/tests/stats460_test.lua) and reusable by the place's own Script without a second copy of the rules.

local M = {}

-- How long an unacknowledged write may stand. Past this, the authority has had ~15 publication intervals to
-- answer and did not, so the write is treated as lost and the previous value is restored.
M.PENDING_TTL_MS = 30000

local function shallow(t)
    local o = {}
    for k, v in pairs(t) do o[k] = v end
    return o
end

local function find_pending(cache, pid)
    for i, p in ipairs(cache.pending) do
        if p.pid == pid then return p, i end
    end
    return nil, nil
end

-- cache.records: pid -> record (the shape the authority publishes, plus any optimistic edit on top)
-- cache.pending: an ordered list of writes the authority has not answered yet (at most one per player)
function M.new(opts)
    opts = opts or {}
    return {
        records = {},
        pending = {},
        seq = 0,
        clock = opts.clock,
        log = opts.log,
    }
end

function M.get(cache, pid)
    local r = cache.records[pid]
    return r and shallow(r) or nil
end

-- Load authority truth without any optimistic intent (first paint, or a record nobody has touched).
function M.put(cache, record)
    if type(record) ~= "table" then return nil, "no-record" end
    if type(record.pid) ~= "string" or record.pid == "" then return nil, "no-pid" end
    cache.records[record.pid] = shallow(record)
    return shallow(record)
end

function M.is_pending(cache, pid)
    return (find_pending(cache, pid)) ~= nil
end

-- Apply a delta immediately, before the authority agrees. Only numeric fields the record already carries move:
-- `{ wins = 1, rating = 16 }` on a record without `rating` adds a win and leaves rating absent, and says so.
function M.optimistic(cache, pid, delta, now_ms)
    local rec = cache.records[pid]
    if not rec then return nil, "unknown-player" end
    if type(delta) ~= "table" then return nil, "no-delta" end
    if find_pending(cache, pid) then return nil, "already-pending" end
    local before = shallow(rec)
    local touched, skipped = 0, 0
    for k, d in pairs(delta) do
        if k ~= "pid" and type(d) == "number" then
            if type(rec[k]) == "number" then
                rec[k] = rec[k] + d
                touched = touched + 1
            else
                skipped = skipped + 1
            end
        end
    end
    if touched == 0 then return nil, "nothing-applied" end
    cache.seq = cache.seq + 1
    local entry = { pid = pid, seq = cache.seq, before = before, delta = delta, at_ms = now_ms, skipped = skipped }
    cache.pending[#cache.pending + 1] = entry
    return shallow(rec), nil, entry
end

-- The authority accepted it: the value already in the cache is now truth, so only the pending mark goes.
function M.ack(cache, pid)
    local _, i = find_pending(cache, pid)
    if not i then return false end
    table.remove(cache.pending, i)
    return true
end

-- The authority refused it: put back exactly what was there, so the screen never shows a stat that does not
-- exist anywhere outside this process.
function M.rollback(cache, pid)
    local p, i = find_pending(cache, pid)
    if not p then return false, "no-pending" end
    cache.records[pid] = p.before
    table.remove(cache.pending, i)
    return true
end

-- Authority truth replaces the cache. A pending write the authority answered is a CONFLICT (we were ahead and
-- it disagreed); a pending write it did not mention survives until the TTL, then reverts.
function M.reconcile(cache, server_records, now_ms)
    local report = { written = 0, conflicts = 0, expired = 0, kept = 0 }
    local by_pid = {}
    for _, sr in ipairs(server_records or {}) do
        if type(sr) == "table" and type(sr.pid) == "string" and sr.pid ~= "" then
            by_pid[sr.pid] = sr
        end
    end
    local surviving = {}
    for _, p in ipairs(cache.pending) do
        if by_pid[p.pid] then
            report.conflicts = report.conflicts + 1
        elseif (now_ms - p.at_ms) <= M.PENDING_TTL_MS then
            surviving[#surviving + 1] = p
            report.kept = report.kept + 1
        else
            cache.records[p.pid] = p.before
            report.expired = report.expired + 1
        end
    end
    cache.pending = surviving
    for pid, sr in pairs(by_pid) do
        cache.records[pid] = shallow(sr)
        report.written = report.written + 1
    end
    return report
end

-- Interface fan-out: the cache is written once, every reader is told. One sink throwing is reported and the
-- rest still run -- a scoreboard that cannot draw must not stop the match from being recorded.
function M.fan_out(cache, pids, sinks)
    local report = { ok = 0, failed = {} }
    for _, pid in ipairs(pids or {}) do
        local rec = cache.records[pid]
        if rec then
            for _, sink in ipairs(sinks or {}) do
                local called, err = pcall(sink.write, shallow(rec))
                if called then
                    report.ok = report.ok + 1
                else
                    report.failed[#report.failed + 1] = { name = sink.name or "?", pid = pid, err = tostring(err) }
                end
            end
        end
    end
    return report
end

-- Fan-in for the two authorities, in authority order (IDUNA first, then WOTAN). A field only one of them carries
-- is taken from it; a field they disagree on keeps the higher-authority value and is reported, because averaging
-- two authorities is how a cache starts making up numbers.
function M.merge_sources(sources)
    local out, conflicts = {}, {}
    for _, src in ipairs(sources or {}) do
        for _, rec in ipairs(src.records or {}) do
            if type(rec) == "table" and type(rec.pid) == "string" and rec.pid ~= "" then
                local acc = out[rec.pid]
                if not acc then
                    out[rec.pid] = shallow(rec)
                else
                    for k, v in pairs(rec) do
                        if k ~= "pid" then
                            if acc[k] == nil then
                                acc[k] = v
                            elseif acc[k] ~= v then
                                conflicts[#conflicts + 1] = {
                                    pid = rec.pid, field = k, kept = acc[k], ignored = v,
                                    source = src.name or "?",
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
