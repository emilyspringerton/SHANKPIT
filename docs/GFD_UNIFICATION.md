# Unifying GoblinFoxDragon with SHANKPIT — plan, and what phase 1 actually shipped

The founder: *"we need to start unifying gfd and shankpit archipelligo ... shankpit is the frontend for the
MMO ... GFD is a fork of SHANKPIT maybe its time to unfork it and just merge GFD all of it needs to be
brought into shankpit and the shankpit engine improvements that are already in place dont get overwritten
... including bringing in the GFD exclusive levels ... all mods based PARENA based."*

GFD has been **cloned and read** (`../GoblinFoxDragon`, not guessed at). What is actually in it:

| Path | What it is |
|---|---|
| `apps/mud`, `apps2/mud` | the text MUD / state engine (jobs, combat text, progression) — **no spatial data at all** |
| `apps2/battlegrounds_gui`, `apps/lobby` | a 3D client, the fork of SHANKPIT's lobby |
| `packages/world/town_map.c` + `generated/town_expansion_blocks.inl` | **Town**: 17 named buildings + expansion blocks as x/z/w/d/h seeds, scale 3.0 — real geometry |
| `data/crystal_seed_meadow.json` | **Meadow**: a 44×44 goblin *simulation* seed (goblins, kinds, grid) — **not** geometry |
| `server/`, `server-go/`, `services/` | Go services (field office, etc.) |

## The rule that protects SHANKPIT

**Nothing from GFD overwrites a SHANKPIT file.** GFD contributed data and ideas; the engine
(XPBD `grb.c`, `structure.h`, `brick_*`, lighting, vehicles, widgets) is untouched. Where GFD had a
parallel implementation, SHANKPIT's is the one that runs and GFD's content is imported *into* its schema.

## Phases

**Phase 1 — shipped (this change).**
- `zone_rules.prn`: the shared-zone lifecycle (900 s grace, 8 zones) and the dungeon layout numbers, all
  PARENA, with identical C defaults.
- `zone_registry.h`: the non-instanced zone table — first entrant generates, later entrants join, expiry
  after fifteen empty minutes, re-entry inside the window keeps the layout.
- `proc_dungeon.h`: one seed → one `CustomLevelData`, byte-identical on every machine, saved as real level
  JSON, with a gate that is a real lever → relay → machine circuit.
- `scripts/import_gfd_levels.py`: Town → `var/maps/gfd_town.json` (boxes, perimeter, spawners). Meadow is
  reported as a sim seed, not a level, rather than fabricated.
- Reachable: **F7** in game and `--dungeon [zone]` on the command line.

**Phase 2 — the zone packet (next).** `PACKET_ZONE_STATE` carrying zone id, seed, occupancy and the
grace countdown. A LAN client then generates the dungeon from the seed instead of being sent geometry,
which is the same "identical geometry compiled in" contract `--level` already relies on. Client entry
must not stall the render thread while generating (generation is a few hundred boxes — measure before
optimising).

**Phase 3 — batteries-included host.** `--coop-host` that starts the authoritative server in-process (or
as a child process) on the LAN interface, advertises over broadcast, and writes progress under `var/`.
Single-player = the same path with nobody else joining, which is the Minecraft model the founder named.
The current `--host/--port` client path already exists; what is missing is the host side owning both the
UDP game loop and the zone registry.

**Phase 4 — GFD state server.** Take the MUD's job/stat/inventory model, and **port its rules to PARENA**
(`stats_bridge.prn`: job → cooldown/haste/evasion/max HP, equipment → affinities). The C side consumes a
flat struct; it must not learn about 23 jobs. The dungeon's `difficulty` tier is the first input.
Persistence is the state server's job (name-keyed JSON saves), not the client's.

**Phase 5 — Meadow, properly.** Meadow has a *simulation* seed, not geometry. Turning it into a 3D zone
means either authoring it as a NOCK level or teaching `proc_dungeon.h` a second layout kind (outdoor
cells with heightfield sampling). Do that only when a scene needs it — inventing a conversion now would be
a fake import.

## Honest state of "multiplayer"

Today the zone registry and the dungeon are **host-local**. Two players on one machine's session share
the layout; a LAN friend cannot join a zone yet because there is no packet carrying the zone's identity or
seed, and the learn-from-seed path is phase 2. Saying "co-op works" before that would be false.
