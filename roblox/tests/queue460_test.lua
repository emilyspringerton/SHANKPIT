-- The 460 queue: the widening window, deterministic pairing, expiry, and who may bot-fill.
-- Every number below is hand-derived from queue460's own constants.
package.path = "roblox/src/?.lua;" .. package.path
local Q = require("queue460")

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

-- the window: 100 at t=0, +50 per whole second, capped at 600
eq(Q.window(0), 100, "0 ms -> 100")
eq(Q.window(999), 100, "999 ms is not a whole second")
eq(Q.window(1000), 150, "1000 ms -> 150")
eq(Q.window(5000), 350, "5000 ms -> 350")
eq(Q.window(10000), 600, "10000 ms would be 600")
eq(Q.window(600000), 600, "capped at 600")
eq(Q.window(-50), 100, "negative wait is treated as zero")

-- seat bookkeeping
local q = Q.new()
local a = Q.enqueue(q, "casual", { pid = 1, rating = 1000, name = "witness", joined_ms = 0 })
ok(a ~= nil and a.id == 1, "first seat gets id 1")
local dup, why = Q.enqueue(q, "casual", { pid = 1, rating = 1000, joined_ms = 0 })
eq(dup, nil, "one seat per body per line")
eq(why, "duplicate", "...and it says why")
ok(Q.enqueue(q, "ranked", { pid = 1, rating = 1000, joined_ms = 0 }) ~= nil,
   "the same body may queue ranked at the same time")
eq(Q.enqueue(q, "duel", { pid = 2 }), nil, "an unknown mode is refused")
eq(Q.position(q, "casual", 1), 1, "position of the only seat is 1")
Q.remove(q, "casual", 1)
eq(Q.position(q, "casual", 1), nil, "removing the seat removes the position")

-- pairing inside the window, and the exact boundary of it
local function two(r0, r1, j0, j1)
    local qq = Q.new()
    Q.enqueue(qq, "casual", { pid = 1, rating = r0, joined_ms = j0 or 0 })
    Q.enqueue(qq, "casual", { pid = 2, rating = r1, joined_ms = j1 or 0 })
    return qq
end
local p = Q.pair(two(1000, 1080, 0, 0), "casual", 0)
ok(p ~= nil, "an 80-point gap pairs immediately inside the 100 window")
eq(p.gap, 80, "the reported gap is the rating gap")
eq(p.first.pid, 1, "the earlier-seated body is first")
eq(p.second.pid, 2, "the later one is second")
eq(Q.pair(two(1000, 1101, 0, 0), "casual", 0), nil, "a 101-point gap does not pair at t=0")
eq(Q.pair(two(1000, 1150, 0, 0), "casual", 999), nil, "not yet at 999 ms")
ok(Q.pair(two(1000, 1150, 0, 0), "casual", 1000) ~= nil, "150 pairs exactly at the 1000 ms window")
eq(Q.pair(two(1000, 1300, 0, 0), "casual", 3999), nil, "300 does not fit a 250 window")
ok(Q.pair(two(1000, 1300, 0, 0), "casual", 4000) ~= nil, "300 fits exactly at 4000 ms")

-- the STRICTER window governs: a fresh body cannot drag a patient one into a lopsided game
local mixed = Q.new()
Q.enqueue(mixed, "casual", { pid = 1, rating = 1000, joined_ms = 0 })     -- has waited 5 s
Q.enqueue(mixed, "casual", { pid = 2, rating = 1300, joined_ms = 5000 })  -- just walked in
eq(Q.pair(mixed, "casual", 5000), nil, "the newcomer's 100 window governs, not the veteran's 350")
eq(Q.pair(mixed, "casual", 8999), nil, "at 3.999 s the newcomer's window is 250 -- a 300 gap still does not fit")
ok(Q.pair(mixed, "casual", 9000) ~= nil, "...and 300 fits at 4 s of ITS OWN wait (window 300)")

-- deterministic tie-break: three equal bodies pair in seat order, not in hash order
local tie = Q.new()
Q.enqueue(tie, "casual", { pid = 5, rating = 1000, joined_ms = 0 })
Q.enqueue(tie, "casual", { pid = 3, rating = 1000, joined_ms = 0 })
Q.enqueue(tie, "casual", { pid = 9, rating = 1000, joined_ms = 0 })
local tp = Q.pair(tie, "casual", 0)
eq(tp.first.pid, 5, "tie goes to the earliest-seated body")
eq(tp.second.pid, 3, "then the next")
eq(#tie.casual, 1, "the third body is left in line")

-- a tick hands back every legal pair, and expiry releases seats after two minutes
local busy = Q.new()
Q.enqueue(busy, "casual", { pid = 1, rating = 1000, joined_ms = 0 })
Q.enqueue(busy, "casual", { pid = 2, rating = 1010, joined_ms = 0 })
Q.enqueue(busy, "casual", { pid = 3, rating = 2000, joined_ms = 0 })
Q.enqueue(busy, "casual", { pid = 4, rating = 2010, joined_ms = 0 })
local t = Q.tick(busy, "casual", 0)
eq(#t.pairs, 2, "two clear pairs in one tick")
eq(t.pairs[1].first.pid, 1, "closest pair first")
eq(#busy.casual, 0, "the line is empty afterwards")

local old = Q.new()
Q.enqueue(old, "casual", { pid = 1, rating = 1000, joined_ms = 0 })
eq(#Q.tick(old, "casual", 120000).expired, 0, "120000 ms is exactly the limit, not past it")
local gone = Q.tick(old, "casual", 120001)
eq(#gone.expired, 1, "120001 ms releases the seat")
eq(#old.casual, 0, "and the line is empty")

-- bot fill: casual only, after 30 s, and the body STAYS queued
local fill = Q.new()
Q.enqueue(fill, "casual", { pid = 1, rating = 1000, joined_ms = 0 })
eq(#Q.tick(fill, "casual", 29999).fills, 0, "not fillable at 29.999 s")
eq(#Q.tick(fill, "casual", 30000).fills, 1, "fillable at 30 s")
eq(#fill.casual, 1, "still standing in line while fillable")
local rankfill = Q.new()
Q.enqueue(rankfill, "ranked", { pid = 1, rating = 1000, joined_ms = 0 })
eq(#Q.tick(rankfill, "ranked", 600000).fills, 0, "ranked never bot-fills")

-- THE THRESHOLDS ARE PARENA'S. This proves the module is running the emitted file and not a hand-copy of it:
-- all of these numbers come out of PARENA/stdlib/shankpit/queue_rules.prn via `make regen-roblox-rules`.
local R = require("queue460_rules")
ok(Q.RULES == R, "the queue delegates to the generated PARENA rules")
eq(R.queueWindow(0), 100, "generated: window starts at 100")
eq(R.queueWindow(9000), 550, "generated: 9 s of waiting is 550")
eq(R.queueStrictWindow(0, 9000), 100, "generated: the strict window is the smaller of the two")
-- BOTH bodies must have waited 4 s for a 300 window to govern: with waits (0, 4000) the STRICT window is the
-- first body's 100, so a 300 gap correctly does not fit. That is the rule, measured, not a rounding accident.
eq(R.queueGapFits(-300, 4000, 4000), 1, "generated: a signed gap is taken as a magnitude")
eq(R.queueGapFits(301, 4000, 4000), 0, "generated: 301 does not fit a 300 window")
eq(R.queueGapFits(300, 0, 4000), 0, "generated: the fresh body's 100 governs, not the veteran's 300")
eq(R.queueExpired(120000), 0, "generated: exactly two minutes is not expired")
eq(R.queueExpired(120001), 1, "generated: one ms past is")
eq(R.queueFillable(1, 999999), 0, "generated: ranked is never fillable")
eq(R.queueFillable(0, 30000), 1, "generated: casual at 30 s is fillable")

print(string.format("queue460: %d checks, %d failures", checks, failures))
os.exit(failures == 0 and 0 or 1)
