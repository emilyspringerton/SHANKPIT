-- The snapshot reader, asserted against hand-built snapshots -- no HTTP, no game.
package.path = "roblox/src/?.lua;" .. package.path
local net = require("net460")

local checks, failures = 0, 0
local function ok(cond, what)
    checks = checks + 1
    if not cond then failures = failures + 1; print("  FAIL  " .. what) end
end
local function eq(got, want, what)
    checks = checks + 1
    if got ~= want then
        failures = failures + 1
        print(string.format("  FAIL  %s (got %s want %s)", what, tostring(got), tostring(want)))
    end
end

-- schema 1 / 2 / 3 -- what each one can and cannot say
eq(net.schema({ schema = 3 }), 3, "schema 3 reads")
eq(net.schema({}), 0, "no schema is 0")
eq(net.schema(nil), 0, "nil snapshot is 0")
local s3 = net.supports({ schema = 3 })
ok(s3.rating and s3.stage, "schema 3 supports rating and stage")
local s1 = net.supports({ schema = 1 })
ok((not s1.rating) and (not s1.stage), "schema 1 supports neither")

-- staleness: 15 s window, symmetric on a wrong clock, missing timestamp is stale
ok(not net.stale({ generated_ms = 1000 }, 2000), "1 s old is fresh")
eq(net.stale({ generated_ms = 1000 }, 20000), true, "19 s old is stale")
eq(net.stale({ generated_ms = 1000 }, 16001), true, "16001 ms trips the 15000 window")
eq(net.stale({ generated_ms = 1000 }, 16000), false, "16000 ms is exactly the window")
eq(net.stale({}, 1000), true, "undated snapshot is stale")
eq(net.stale({ generated_ms = 5000 }, 4000), false, "clock behind is not proof of staleness")

-- the ladder's own order: rating desc, wins desc, pid asc
local records = {
    { pid = 2, rating = 1000, wins = 5 },
    { pid = 1, rating = 1000, wins = 5 },
    { pid = 3, rating = 1020, wins = 0 },
    { pid = 4, wins = 9 },
}
local ranked = net.rank(records)
eq(ranked[1].pid, 3, "highest rating first")
eq(ranked[2].pid, 1, "tie on rating, tie on wins -> lower pid first")
eq(ranked[3].pid, 2, "then the other")
eq(ranked[4].pid, 4, "a record with no rating sorts last, not out")

local snap = {
    schema = 3, generated_ms = 0, population = 4,
    meta = { kind = "defense" },
    records = records,
    stage = { round = 1, rounds = 2, over = false, champion = -1 },
}
local pop = net.population(snap)
eq(pop.count, 4, "population is the exporter's own count")
eq(pop.meta_kind, "defense", "meta kind passes through")
eq(net.population({}).count, 0, "no population field falls back to the record count")
eq(net.population({}).meta_kind, "none", "missing meta reads as none")

local found = net.record_for(snap, 3)
ok(found ~= nil and found.rating == 1020, "record_for finds by pid")
eq(net.record_for(snap, 99), nil, "record_for refuses an absent pid")
eq(net.record_for(snap, "nonsense"), nil, "record_for refuses a non-numeric pid")

local rows = net.rows(snap, 2)
eq(#rows, 2, "rows respects the cap")
eq(rows[1].standing, 1, "first row is standing 1")
eq(rows[1].pid, 3, "first row is the highest rated")
eq(rows[2].standing, 2, "second row is standing 2")
eq(#net.rows(snap), 4, "no cap means everybody")

print(string.format("net460: %d checks, %d failures", checks, failures))
os.exit(failures == 0 and 0 or 1)
