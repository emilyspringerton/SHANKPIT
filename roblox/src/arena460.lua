-- src/arena460.lua -- the place's OWN Laser Tag content, read as the arena for a SHANKPIT 460 round.
--
-- WHY THIS EXISTS: SHANKPIT-460 is a client you play a round in. This layer used to stop at the front door --
-- sign in, queue, take a ticket -- and leave the player with nowhere to go. The checked-in base place
-- (`shankpit_roblox.rbxl`) already ships Roblox's Laser Tag content (Tools named `Blaster` / `AutoBlaster`,
-- a `Teams` service, `SpawnLocation`s including a green one, `Tagged`, `TeamColor`), so the arena is a thing to
-- READ, not a thing to build. Nothing here creates, renames or clones an instance: it takes a plain description
-- of the place and answers what a round needs, so the answers are testable without a Roblox runtime.
--
-- It owns no rule the place already owns: Roblox's own Tool damage and Teams decide tags. This module only says
-- which spawn belongs to which team, which weapon a seat gets, and who won from final tag counts it is handed.
-- It refuses to invent an arena: no spawns, no weapons, no teams, or a team with no spawn is reported as a
-- reason the round cannot start rather than papered over with a default.

local M = {}

local unpack = table.unpack or unpack

-- tree = {
--   teams  = { { name = "Red Team" }, ... },
--   spawns = { { name = "SpawnLocation", team = "Red Team" }, ... },   -- team may be nil
--   tools  = { { name = "Blaster" }, ... },
-- }
function M.describe(tree)
    local out = { ok = false, teams = {}, spawns = {}, weapons = {}, missing = {} }
    if type(tree) ~= "table" then
        out.missing[#out.missing + 1] = "no-place"
        return out
    end

    for _, team in ipairs(tree.teams or {}) do
        if type(team) == "table" and type(team.name) == "string" and team.name ~= "" then
            out.teams[#out.teams + 1] = team.name
        end
    end
    for _, spawn in ipairs(tree.spawns or {}) do
        if type(spawn) == "table" and type(spawn.name) == "string" and spawn.name ~= "" then
            out.spawns[#out.spawns + 1] = { name = spawn.name, team = spawn.team }
        end
    end
    for _, tool in ipairs(tree.tools or {}) do
        if type(tool) == "table" and type(tool.name) == "string" and tool.name ~= "" then
            out.weapons[#out.weapons + 1] = tool.name
        end
    end

    if #out.teams == 0 then out.missing[#out.missing + 1] = "no-teams" end
    if #out.spawns == 0 then out.missing[#out.missing + 1] = "no-spawns" end
    if #out.weapons == 0 then out.missing[#out.missing + 1] = "no-weapons" end

    -- A team with no pad is a seat with nowhere to stand. Report the team, do not fall back to someone else's.
    for _, team in ipairs(out.teams) do
        local covered = false
        for _, spawn in ipairs(out.spawns) do
            if spawn.team == team then covered = true end
        end
        if not covered then out.missing[#out.missing + 1] = "no-spawn-for-team:" .. team end
    end

    out.ok = #out.missing == 0
    return out
end

-- A place is not frozen: a Studio session adds the pads after the server script starts, and a publish can take
-- one away. So the arena is re-read -- but a re-read is allowed to be WORSE than what we had (Workspace is
-- briefly empty while a place rebuilds, a team edit lands before its pad does), and a lobby that forgot its
-- arena mid-session would refuse rounds it could actually host. A usable read replaces; an unusable read keeps
-- the last usable arena and reports what the re-read could not see, so the reason is still inspectable.
function M.refresh(previous, tree)
    local fresh = M.describe(tree)
    if fresh.ok then return fresh, nil end
    if type(previous) == "table" and previous.ok then
        return {
            ok = true, stale = true,
            teams = previous.teams, spawns = previous.spawns, weapons = previous.weapons,
            missing = {}, fresh_missing = fresh.missing,
        }, "kept-last-usable:" .. table.concat(fresh.missing, ",")
    end
    return fresh, nil
end

-- Seats in, assignments out. Teams alternate in seat order, deterministically, so two players never land on the
-- same side by accident of dictionary ordering. A SpawnLocation is re-usable (that is what it is for), so more
-- seats than pads is not a failure; no pad at all already failed in describe().
function M.seat(arena, seats)
    if type(arena) ~= "table" or not arena.ok then return nil, "arena-not-ok" end
    if type(seats) ~= "table" or #seats == 0 then return nil, "no-seats" end
    local out = {}
    for i, seat in ipairs(seats) do
        -- teams[i] is the team this seat is on; a pad is found by THAT name, never by seat parity alone
        local team = arena.teams[((i - 1) % #arena.teams) + 1]
        local spawn = nil
        for _, pad in ipairs(arena.spawns) do
            if spawn == nil and pad.team == team then spawn = pad.name end
        end
        out[#out + 1] = { seat = seat, team = team, spawn = spawn }
    end
    return out
end

-- The pairing path. Two seats in, each seat's OWN team and pad out, keyed by pid -- because telling both sides
-- of a pair the same spawn is the failure this exists to prevent: it stands two players on one team's pad and
-- calls it a match. A place that cannot seat the pair is reported, never half-seated.
function M.assign_pair(arena, first, second)
    if type(arena) ~= "table" or not arena.ok then return nil, "arena-not-ok" end
    if type(first) ~= "table" or type(second) ~= "table" then return nil, "no-seats" end
    local assignments, why = M.seat(arena, { first, second })
    if not assignments then return nil, why end
    local by_pid = {}
    for _, a in ipairs(assignments) do
        if type(a.seat) == "table" and a.seat.user_id ~= nil then
            by_pid[tostring(a.seat.user_id)] = a
        end
    end
    return { first = assignments[1], second = assignments[2], by_pid = by_pid }
end

-- The tools a seat is given. This layer does not decide what a Blaster does -- Roblox's own Tool does.
function M.loadout(arena)
    if type(arena) ~= "table" or not arena.ok then return nil, "arena-not-ok" end
    return { weapons = { unpack(arena.weapons) } }
end

-- Final tag counts in, a result out. A tie is a draw, not a coin flip; a malformed count is refused rather than
-- read as zero, because "nobody tagged anyone" and "the number is missing" are different facts.
function M.result(tags)
    if type(tags) ~= "table" or #tags == 0 then return nil, "no-tags" end
    local best, bestTags, tie = nil, -1, false
    for _, entry in ipairs(tags) do
        local name = type(entry) == "table" and entry.team or nil
        local count = type(entry) == "table" and tonumber(entry.tags) or nil
        if type(name) ~= "string" or name == "" or count == nil then return nil, "bad-tags" end
        if count > bestTags then
            best, bestTags, tie = name, count, false
        elseif count == bestTags then
            tie = true
        end
    end
    if tie then return { draw = true, winner = nil, tags = tags } end
    return { draw = false, winner = best, tags = tags }
end

-- Which seats won. A draw wins nobody, and a seat whose team is not the winner is not "lost" by invention --
-- it is simply not the winner.
function M.outcomes(assignments, result)
    local out = {}
    if type(assignments) ~= "table" or type(result) ~= "table" then return out end
    for _, assignment in ipairs(assignments) do
        out[#out + 1] = {
            seat = assignment.seat,
            team = assignment.team,
            won = (not result.draw) and assignment.team == result.winner or false,
        }
    end
    return out
end

return M
