-- The arena adapter, asserted against hand-built descriptions of a place -- no Roblox runtime.
-- The names used here are the ones the checked-in shankpit_roblox.rbxl actually carries (Blaster, AutoBlaster,
-- Teams, SpawnLocation, a green pad), so the fixtures describe the real place rather than an aspirational one.
package.path = "roblox/src/?.lua;" .. package.path
local arena = require("arena460")

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
local function has(list, want)
    for _, v in ipairs(list) do if v == want then return true end end
    return false
end

local LASER_TAG = {
    teams = { { name = "Red Team" }, { name = "Blue Team" } },
    spawns = {
        { name = "SpawnLocation", team = "Red Team" },
        { name = "BlueSpawn", team = "Blue Team" },
        { name = "GreenSpawnLocation", team = nil },
    },
    tools = { { name = "Blaster" }, { name = "AutoBlaster" } },
}

-- ---- 1. the real place reads as a usable arena ------------------------------------------------
local a = arena.describe(LASER_TAG)
ok(a.ok, "the Laser Tag place is a usable arena")
eq(#a.teams, 2, "both teams found")
eq(#a.spawns, 3, "all three pads found, including the unnamed-team green one")
eq(#a.weapons, 2, "both laser tools found")
eq(#a.missing, 0, "nothing missing")
eq(a.teams[1], "Red Team", "team names are carried as they are")
eq(a.weapons[1], "Blaster", "tool names are carried as they are")

-- ---- 2. an unusable place says why, and never invents a fallback -------------------------------
local none = arena.describe(nil)
ok(not none.ok, "no place is not an arena")
eq(none.missing[1], "no-place", "and says so")

local bare = arena.describe({})
ok(not bare.ok, "an empty place is not an arena")
ok(has(bare.missing, "no-teams"), "missing teams reported")
ok(has(bare.missing, "no-spawns"), "missing spawns reported")
ok(has(bare.missing, "no-weapons"), "missing weapons reported")

local noWeapons = arena.describe({ teams = LASER_TAG.teams, spawns = LASER_TAG.spawns, tools = {} })
ok(not noWeapons.ok, "no blaster means no round")
ok(has(noWeapons.missing, "no-weapons"), "and the reason names the weapons")

local teamNoPad = arena.describe({
    teams = { { name = "Red Team" }, { name = "Blue Team" } },
    spawns = { { name = "SpawnLocation", team = "Red Team" } },
    tools = { { name = "Blaster" } },
})
ok(not teamNoPad.ok, "a team with no pad cannot be seated")
ok(has(teamNoPad.missing, "no-spawn-for-team:Blue Team"), "and the team is named")

-- malformed entries are skipped, not turned into "" or nil-named teams
local sloppy = arena.describe({
    teams = { { name = "" }, { name = 7 }, { name = "Red Team" } },
    spawns = { { name = "" }, { name = "Pad", team = "Red Team" } },
    tools = { {}, { name = "Blaster" } },
})
eq(#sloppy.teams, 1, "only the real team survives")
eq(#sloppy.spawns, 1, "only the real pad survives")
eq(#sloppy.weapons, 1, "only the real tool survives")
ok(sloppy.ok, "and that is still a usable arena")

-- ---- 3. seating: deterministic teams, a pad per team ------------------------------------------
local seats = { { user_id = 11, name = "one" }, { user_id = 22, name = "two" } }
local assigned = arena.seat(a, seats)
ok(assigned ~= nil, "two seats seat fine")
eq(#assigned, 2, "one assignment each")
eq(assigned[1].team, "Red Team", "first seat takes the first team")
eq(assigned[2].team, "Blue Team", "second seat takes the other -- not the same side")
eq(assigned[1].spawn, "SpawnLocation", "red seat gets the red pad")
eq(assigned[2].spawn, "BlueSpawn", "blue seat gets the blue pad")
eq(assigned[1].seat.user_id, 11, "the seat object is carried through")

local four = arena.seat(a, { seats[1], seats[2], seats[1], seats[2] })
eq(four[3].team, "Red Team", "a third seat returns to the first team")
ok(four[4].team == "Blue Team", "and a fourth to the second")
eq(arena.seat({ ok = false, teams = {}, spawns = {}, weapons = {} }, seats), nil, "an unusable arena seats nobody")
eq(select(2, arena.seat(a, {})), "no-seats", "no seats is its own reason")
eq(select(2, arena.seat(nil, seats)), "arena-not-ok", "and a nil arena is too")

-- ---- 4. loadout comes from the place, not from this module ------------------------------------
local kit = arena.loadout(a)
eq(#kit.weapons, 2, "both tools offered")
eq(kit.weapons[1], "Blaster", "in the place's own order")
eq(arena.loadout({ ok = false, weapons = {} }), nil, "an unusable arena has no loadout")

-- ---- 5. a result is arithmetic on tag counts, and a tie is a draw ------------------------------
local win = arena.result({ { team = "Red Team", tags = 12 }, { team = "Blue Team", tags = 9 } })
ok(win ~= nil, "a decided round has a result")
eq(win.winner, "Red Team", "the higher tag count wins")
eq(win.draw, false, "and it is not a draw")

local even = arena.result({ { team = "Red Team", tags = 5 }, { team = "Blue Team", tags = 5 } })
eq(even.draw, true, "equal counts are a draw")
eq(even.winner, nil, "a draw has no winner")

eq(select(2, arena.result({})), "no-tags", "no counts is not a 0-0 draw")
eq(select(2, arena.result({ { team = "Red Team" } })), "bad-tags", "a missing count is refused, not read as zero")
eq(select(2, arena.result({ { tags = 3 } })), "bad-tags", "a missing team is refused too")

-- ---- 6. seat outcomes follow the team, not the seat order -------------------------------------
local outcomes = arena.outcomes(assigned, win)
eq(#outcomes, 2, "one outcome per seat")
eq(outcomes[1].won, true, "the red seat won")
eq(outcomes[2].won, false, "the blue seat did not")
local drawn = arena.outcomes(assigned, even)
eq(drawn[1].won, false, "a draw wins nobody")
eq(drawn[2].won, false, "on either side")
eq(#arena.outcomes(nil, win), 0, "no assignments, no outcomes")

-- ---- 7. a re-read may replace the arena, but a worse read does not erase a usable one -------------------
local same = arena.refresh(a, LASER_TAG)
ok(same.ok, "re-reading the same place keeps it usable")
eq(same.stale, nil, "and is not marked as a kept old one")
eq(#same.spawns, 3, "the pads offered are the fresh read's")

ok(arena.refresh(nil, LASER_TAG).ok, "a first read of a real place is usable")
eq(arena.refresh(nil, {}).ok, false, "a first read of an empty place is not")

local emptied = arena.refresh(a, { teams = LASER_TAG.teams, spawns = {}, tools = LASER_TAG.tools })
ok(emptied.ok, "a transient empty read does not erase a usable arena")
eq(emptied.stale, true, "it is marked as the arena kept from before")
ok(has(emptied.fresh_missing, "no-spawns"), "and says what the re-read could not see")
eq(#emptied.spawns, 3, "the pads that were there are still the ones offered")

-- Down to ONE team is still a usable arena (everyone seats on it), so it REPLACES rather than being kept --
-- keeping the old two-team arena would seat players on a side the place no longer has.
local oneTeam = arena.refresh(a, {
    teams = { { name = "Red Team" } },
    spawns = { { name = "SpawnLocation", team = "Red Team" } },
    tools = LASER_TAG.tools,
})
ok(oneTeam.ok, "a place down to one team is still usable")
eq(oneTeam.stale, nil, "so it replaces the old arena instead of being kept")
eq(#oneTeam.teams, 1, "and offers only the team the place actually has")

-- A team whose PAD is gone is the broken case: that is what describe() refuses, so refresh keeps the old one.
local padless = arena.refresh(a, {
    teams = LASER_TAG.teams,
    spawns = { { name = "SpawnLocation", team = "Red Team" } },
    tools = LASER_TAG.tools,
})
eq(padless.stale, true, "a team whose pad disappeared keeps the previous arena")
eq(#padless.teams, 2, "with both teams still seatable")
ok(has(padless.fresh_missing, "no-spawn-for-team:Blue Team"), "and names the pad the re-read could not find")

print(string.format("arena460: %d checks, %d failures", checks, failures))
if failures > 0 then os.exit(1) end
