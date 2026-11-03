-- The optimistic cache, asserted against hand-built records -- no HTTP, no DataStore, no game.
-- The question every section answers: what does the player see, what happens when the authority disagrees,
-- goes quiet, or a sink throws, and does a published match get counted exactly once.
package.path = "roblox/src/?.lua;" .. package.path
local stats = require("stats460")

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

local function cache_with(recs)
    local c = stats.new()
    for _, r in ipairs(recs) do stats.put(c, r) end
    return c
end

-- ---- 1. the cache holds exactly what it was given: no invented fields ------------------------------
local c = cache_with({ { pid = "a", rating = 1000, wins = 5 } })
local r = stats.get(c, "a")
eq(r.rating, 1000, "loads rating")
eq(r.wins, 5, "loads wins")
eq(r.losses, nil, "does not invent a field the authority never sent")
eq(stats.get(c, "nobody"), nil, "unknown player is nil, not a blank record")
eq((stats.put(c, { rating = 1 })), nil, "a record with no pid is refused")
eq((stats.put(c, { pid = "" })), nil, "an empty pid is refused")

-- ---- 1b. pids: the publisher emits NUMBERS, and both shapes must be one entry ----------------------
local cn = cache_with({ { pid = 7, rating = 900, wins = 1 } })
ok(stats.get(cn, 7) ~= nil, "numeric pid loads")
ok(stats.get(cn, "7") ~= nil, "the same pid as a string reads the same record")
eq(stats.key(7), "7", "key normalises a number")
eq(stats.key("7"), "7", "key normalises a string")
stats.optimistic(cn, "7", { wins = 1 }, 1)
eq(stats.get(cn, 7).wins, 2, "a write through the string pid is visible through the number")
eq(stats.get(cn, 7).rating, 900, "and did not disturb another field")

-- ---- 2. optimistic: visible on the same frame, marked pending, merged not dropped ------------------
local c2 = cache_with({ { pid = "a", rating = 1000, wins = 5 } })
ok(not stats.is_pending(c2, "a"), "nothing pending before a write")
local rec, err = stats.optimistic(c2, "a", { wins = 1, rating = 16 }, 1000)
eq(err, nil, "optimistic applies")
eq(rec.wins, 6, "win is visible immediately")
eq(rec.rating, 1016, "rating moves immediately")
eq(stats.get(c2, "a").wins, 6, "the cache itself carries the optimistic value")
ok(stats.is_pending(c2, "a"), "the write is marked pending")
local rec2, err2 = stats.optimistic(c2, "a", { wins = 1 }, 1001)
eq(err2, nil, "a second write merges rather than being refused")
eq(rec2.wins, 7, "both wins counted")
local pend = c2.pending[1]
eq(#c2.pending, 1, "still exactly one pending entry")
eq(pend.applied.wins, 2, "the pending delta accumulated")
eq(pend.before.wins, 5, "the original value is still recoverable")
eq(select(2, stats.optimistic(c2, "ghost", { wins = 1 }, 1001)), "unknown-player", "no cache entry, no write")

-- ---- 3. no invention: a delta may not create a stat -------------------------------------------------
local c3 = cache_with({ { pid = "b", wins = 7 } })
local rec3, err3, entry3 = stats.optimistic(c3, "b", { wins = 1, rating = 16 }, 2000)
eq(err3, nil, "partial delta still applies")
eq(rec3.rating, nil, "rating is NOT invented from a delta")
eq(rec3.wins, 8, "the field that exists does move")
eq(entry3.skipped, 1, "the skipped field is counted, not silently dropped")

-- ---- 4. ack: the authority agreed, only the mark goes ----------------------------------------------
local c4 = cache_with({ { pid = "a", wins = 5 } })
stats.optimistic(c4, "a", { wins = 1 }, 3000)
ok(stats.ack(c4, "a"), "ack clears the pending mark")
ok(not stats.is_pending(c4, "a"), "nothing pending after ack")
eq(stats.get(c4, "a").wins, 6, "the accepted value stays")
eq(stats.ack(c4, "a"), false, "acking twice is not an error, it is false")

-- ---- 5. rollback: refused writes are undone, exactly ------------------------------------------------
local c5 = cache_with({ { pid = "a", rating = 1000, wins = 5, losses = 2 } })
stats.optimistic(c5, "a", { wins = 1, rating = 16 }, 4000)
stats.optimistic(c5, "a", { wins = 1 }, 4001)
ok(stats.rollback(c5, "a"), "rollback succeeds while pending")
eq(stats.get(c5, "a").wins, 5, "wins restored to the value before BOTH writes")
eq(stats.get(c5, "a").rating, 1000, "rating restored")
eq(stats.get(c5, "a").losses, 2, "untouched field untouched")
ok(not stats.is_pending(c5, "a"), "pending cleared by rollback")
eq(select(2, stats.rollback(c5, "a")), "no-pending", "rollback with nothing pending says so")

-- ---- 6. reconcile: agreement is a CONFIRMATION, disagreement is a CONFLICT -------------------------
local c6 = cache_with({ { pid = "a", rating = 1000, wins = 5 } })
stats.optimistic(c6, "a", { wins = 1, rating = 16 }, 5000)
local rep6 = stats.reconcile(c6, { { pid = "a", rating = 1016, wins = 6 } }, 5100)
eq(rep6.written, 1, "one record written from the authority")
eq(rep6.confirmed, 1, "the authority's numbers agree with what we applied")
eq(rep6.conflicts, 0, "so it is not a conflict")
eq(stats.get(c6, "a").rating, 1016, "authority rating is the one kept")
ok(not stats.is_pending(c6, "a"), "answered write is no longer pending")

local c6b = cache_with({ { pid = "a", rating = 1000, wins = 5 } })
stats.optimistic(c6b, "a", { rating = 16 }, 5000)
local rep6b = stats.reconcile(c6b, { { pid = "a", rating = 1008, wins = 5 } }, 5100)
eq(rep6b.conflicts, 1, "a real disagreement is counted as a conflict")
eq(rep6b.confirmed, 0, "and not as a confirmation")
eq(stats.get(c6b, "a").rating, 1008, "authority value wins anyway")

-- ---- 7. reconcile: silence keeps a young write, reverts an expired one -----------------------------
local c7 = cache_with({ { pid = "a", wins = 1 }, { pid = "b", wins = 1 } })
stats.optimistic(c7, "a", { wins = 1 }, 10000)
stats.optimistic(c7, "b", { wins = 1 }, 10000)
local rep7 = stats.reconcile(c7, { { pid = "a", wins = 9 } }, 10000 + stats.PENDING_TTL_MS)
eq(rep7.written, 1, "only the mentioned record was written")
eq(stats.get(c7, "a").wins, 9, "mentioned record took authority truth")
eq(stats.get(c7, "b").wins, 2, "unmentioned write still stands inside the TTL")
ok(stats.is_pending(c7, "b"), "and is still pending")
local rep7b = stats.reconcile(c7, {}, 10000 + stats.PENDING_TTL_MS + 1)
eq(rep7b.expired, 1, "past the TTL the write is counted as lost")
eq(stats.get(c7, "b").wins, 1, "and the previous value comes back")
ok(not stats.is_pending(c7, "b"), "nothing left pending")

-- ---- 8. the published result stream: one match, counted once, winner only --------------------------
local c8 = cache_with({ { pid = 1, wins = 3, games = 4 }, { pid = 2, wins = 1, games = 2 } })
local m = { pid0 = 1, pid1 = 2, winner = 0, rounds = 3, ms = 111000 }
local key = stats.result_key(m)
eq(key, "1:2:0:111000", "result key is the match, not the pair")
local applied = stats.apply_result(c8, m, 6000)
ok(applied ~= nil, "the result applied")
eq(stats.get(c8, 1).wins, 4, "winner gains a win")
eq(stats.get(c8, 1).games, 5, "winner gains a game")
eq(stats.get(c8, 2).wins, 1, "loser gains no win")
eq(stats.get(c8, 2).games, 3, "loser gains a game")
eq(stats.get(c8, 1).rating, nil, "rating is NOT invented from a result")
eq(select(2, stats.apply_result(c8, m, 6001)), "already-seen", "the same match is never counted twice")
local m2 = { pid0 = 1, pid1 = 2, winner = 0, ms = 112000 }
stats.apply_result(c8, m2, 6002)
eq(stats.get(c8, 1).wins, 5, "a second match between the same pair does count")
local draw = { pid0 = 1, pid1 = 2, winner = 2, ms = 113000 }
stats.apply_result(c8, draw, 6003)
eq(stats.get(c8, 1).wins, 5, "a draw awards no win")
eq(stats.get(c8, 1).games, 7, "a draw still counts as a game for both")
eq(stats.get(c8, 2).games, 5, "for both")

-- unknown player: reported, remembered, and not counted when the record later shows up
local c9 = cache_with({ { pid = 1, wins = 1, games = 1 } })
local part = stats.apply_result(c9, { pid0 = 1, pid1 = 99, winner = 0, ms = 5000 }, 7000)
ok(part ~= nil, "a result for a player the cache does not hold is still a result")
eq(part.applied0, true, "the known side applied")
eq(part.applied1, false, "the unknown side is reported, not invented")
eq(part.why1, "unknown-player", "and says why")
eq(stats.apply_result(c9, { pid0 = 1, pid1 = 99, winner = 0, ms = 5000 }, 7001), nil, "and is not retried forever")

-- ---- 9. cold start: history is remembered, not replayed ---------------------------------------------
local c10 = stats.new()
local seeded = stats.boot(c10,
    { { pid = 1, wins = 10, games = 12 }, { pid = 2, wins = 2, games = 12 } },
    { { pid0 = 1, pid1 = 2, winner = 0, ms = 9000 }, { pid0 = 2, pid1 = 1, winner = 1, ms = 9001 } })
eq(seeded, 2, "both records loaded")
eq(stats.get(c10, 1).wins, 10, "loaded win count is the authority's, not incremented")
eq(stats.get(c10, 2).wins, 2, "same for the other side")
ok(c10.booted, "the cache reports itself booted")
eq(select(2, stats.apply_result(c10, { pid0 = 1, pid1 = 2, winner = 0, ms = 9000 }, 10000)), "already-seen",
    "a ticker entry seen at boot is not replayed as a fresh win")
eq(stats.get(c10, 1).wins, 10, "so the win count is untouched")

-- ---- 10. fan-out: every reader is told, one throw cannot stop the others ---------------------------
local c11 = cache_with({ { pid = "a", wins = 1 }, { pid = "b", wins = 2 } })
local seen = {}
local good = { name = "board", write = function(r) seen[#seen + 1] = r.pid .. "=" .. r.wins end }
local bad = { name = "scoreboard", write = function() error("no render target") end }
local rep8 = stats.fan_out(c11, { "a", "b", "missing" }, { good, bad })
eq(rep8.ok, 2, "two successful writes (2 pids x 1 good sink)")
eq(#rep8.failed, 2, "both failures reported")
eq(rep8.failed[1].name, "scoreboard", "the failing sink is named")
eq(rep8.failed[1].pid, "a", "and so is the record")
ok(string.find(rep8.failed[1].err, "no render target") ~= nil, "the sink's own error text survives")
eq(#seen, 2, "the good sink still received both records after the bad one threw")
eq(seen[1], "a=1", "fan-out hands over the cache's value")
eq(stats.fan_out(c11, { "missing" }, { good }).ok, 0, "a pid the cache does not hold writes nothing")

-- ---- 11. fan-in: two authorities, in authority order, disagreements reported ------------------------
local merged, conflicts = stats.merge_sources({
    { name = "iduna", records = { { pid = "a", rating = 1010, wins = 6 } } },
    { name = "wotan", records = { { pid = "a", rating = 1008, wins = 6, deck = "sixteen" },
                                  { pid = "b", rating = 900 } } },
})
eq(merged.a.rating, 1010, "IDUNA is first authority: its rating is kept")
eq(merged.a.deck, "sixteen", "a field only WOTAN carries is filled in")
eq(merged.b.rating, 900, "a player only WOTAN knows still exists")
eq(#conflicts, 1, "one disagreement reported")
eq(conflicts[1].field, "rating", "the disagreement names the field")
eq(conflicts[1].kept, 1010, "and which value won")
eq(conflicts[1].ignored, 1008, "and which was ignored")
eq(conflicts[1].source, "wotan", "and who was overruled")

print(string.format("stats460: %d checks, %d failures", checks, failures))
if failures > 0 then os.exit(1) end
