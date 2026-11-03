-- src/net460.lua -- the world's own numbers, read on the Roblox side.
--
-- SHANKPIT-460 DNA, narrow: this is the READ half of "web stats" -- the same fact WOTAN's Citizens page
-- renders, handed to a Roblox server. It owns no rating, no record and no order of its own; everything here
-- either mirrors the published snapshot exactly or reports that it cannot.
--
-- WHERE THE DATA COMES FROM (checked, not assumed): `packages/simulation/dw_net.c` publishes
-- `{schema, generated_ms, population, meta, records[], challenges[], results[], recent[], stage}` through
-- `tools/shankpit_netd.c` at `/api/v1/shankpit-net/deadweight`, and it is written only when the world names a
-- file (`SHANKPIT_NET_SNAPSHOT`), because a world that publishes nothing should cost nothing. So this module
-- takes an ALREADY-DECODED table (`HttpService:JSONDecode` in the place, a literal in the tests) and never
-- does HTTP itself: the network belongs to whoever calls it, which is what lets the whole thing be tested
-- without a game running.
--
-- THE SORT IS THE LADDER'S RULE, NOT A SECOND OPINION: rating desc, wins desc, pid asc -- the same total order
-- WOTAN's page sorts by and the same one `dw_stage` seeds its bracket from. The snapshot deliberately ships
-- no pre-sorted array (schema 2's own note: shipping the same fact twice is how two pages start disagreeing
-- about who is first), so sorting here is not duplication; inventing a tie-break would be.

local M = {}

-- How old a snapshot may be before a screen should say so. 15 s is ~7 publication intervals (`dw_net_publish`
-- writes at most every 2 s), so a healthy world never trips it and a dead one trips it immediately.
M.MAX_AGE_MS = 15000

-- Schema 1 had no `rating`; 2 added it; 3 added the stage bracket. Read what is there and say what is not,
-- rather than refusing an older snapshot the world may legitimately still be writing.
function M.schema(snapshot)
    if type(snapshot) ~= "table" then return 0 end
    local n = tonumber(snapshot.schema)
    if not n then return 0 end
    return math.floor(n)
end

function M.supports(snapshot)
    local s = M.schema(snapshot)
    return {
        rating = s >= 2,
        stage  = s >= 3,
    }
end

-- Stale means "do not draw this as live". A missing timestamp is stale (an undated number is not evidence of
-- freshness), and a timestamp in the future is treated as fresh but measured symmetrically -- a box whose clock
-- is off by an hour should not blank the board.
function M.stale(snapshot, now_ms, max_age_ms)
    if type(snapshot) ~= "table" then return true end
    local gen = tonumber(snapshot.generated_ms)
    if not gen then return true end
    local age = tonumber(now_ms) - gen
    if not age then return true end
    if age < 0 then age = -age end
    return age > (tonumber(max_age_ms) or M.MAX_AGE_MS)
end

-- rating desc, wins desc, pid asc. A record with no rating (schema 1, or a malformed line) sorts last rather
-- than being dropped: a body that exists with an unknown number is still a body.
function M.rank(records)
    local out = {}
    if type(records) ~= "table" then return out end
    for i = 1, #records do
        if type(records[i]) == "table" then out[#out + 1] = records[i] end
    end
    table.sort(out, function(a, b)
        local ra, rb = tonumber(a.rating), tonumber(b.rating)
        if ra ~= nil and rb ~= nil and ra ~= rb then return ra > rb end
        if (ra ~= nil) ~= (rb ~= nil) then return ra ~= nil end
        local wa, wb = tonumber(a.wins) or 0, tonumber(b.wins) or 0
        if wa ~= wb then return wa > wb end
        return (tonumber(a.pid) or 1e9) < (tonumber(b.pid) or 1e9)
    end)
    return out
end

function M.record_for(snapshot, pid)
    if type(snapshot) ~= "table" or type(snapshot.records) ~= "table" then return nil end
    local want = tonumber(pid)
    if not want then return nil end
    for i = 1, #snapshot.records do
        local r = snapshot.records[i]
        if type(r) == "table" and tonumber(r.pid) == want then return r end
    end
    return nil
end

-- What a board header says: how many bodies, and which way the meta leans. The meta's own field names are the
-- exporter's; anything missing reads as "none" instead of a guess.
function M.population(snapshot)
    if type(snapshot) ~= "table" then return { count = 0, meta_kind = "none" } end
    local count = tonumber(snapshot.population)
    if not count then
        count = (type(snapshot.records) == "table") and #snapshot.records or 0
    end
    local kind = "none"
    if type(snapshot.meta) == "table" and snapshot.meta.kind ~= nil then
        kind = tostring(snapshot.meta.kind)
    end
    return { count = count, meta_kind = kind }
end

-- The rows a board draws, capped, with their standing. Standing is 1-based and equals the rank in `rank`, so a
-- reader never re-derives "am I third" from a rating.
function M.rows(snapshot, limit)
    local ranked = M.rank(type(snapshot) == "table" and snapshot.records or nil)
    local out = {}
    local n = #ranked
    if limit and limit < n then n = limit end
    for i = 1, n do
        local r = ranked[i]
        out[#out + 1] = {
            standing = i,
            pid = tonumber(r.pid) or 0,
            name = tostring(r.name or ""),
            rating = tonumber(r.rating),
            wins = tonumber(r.wins) or 0,
            games = tonumber(r.games) or 0,
        }
    end
    return out
end

return M
