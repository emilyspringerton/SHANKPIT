# REDGARDEN/ECOWAR squad warfare inside SHANKPIT — what is real, what is not, and the next lanes

Written 2026-11-03. This file exists because a design conversation described the port in confident
detail using names that do not exist yet. The point of the doc is to keep the *direction* (PARENA mods
eating the engine, squad warfare as a SHANKPIT scene) while refusing the invented API surface, so the
next thread starts from verified seams instead of pasted prose.

Read `docs/GFD_UNIFICATION.md` for the precedent this should follow: GFD was **cloned and read** before
anything was promised, and "nothing from GFD overwrites a SHANKPIT file" was the rule that protected the
engine.

## The direction, in one sentence

SHANKPIT keeps the thin, unsafe host (sockets, GL, input, file handles) and PARENA owns the rules; a
squad-warfare scene and its armies are therefore **rules plus a level generator**, not a second engine.

## What is already real here

| Seam | Real files | State |
|---|---|---|
| Rules-outside-the-engine | 45 `.prn` in `PARENA/stdlib/shankpit/`, 39 generated `packages/simulation/*_rules.c` checked in | the "engine eaten from the outside in" is already the house style, not a proposal |
| Regeneration | checked-in C is the source of truth in the build; `PARENA` generates, SHANKPIT compiles | `make server` / `make lobby` never call the compiler |
| Co-located compile macro | `PARENA/bazel/parena_compile.bzl`, `SHANKPIT/packages/common/BUILD.bazel` | real, but SHANKPIT's sim rules do **not** use it — they live in PARENA's stdlib |
| Tactical pheromone markers | `PACKET_PHEROMONE_THROW` (11), `packages/simulation/zombies_pheromone_rules.c` | real throw + steer-to-marker decision, already server-authoritative |
| Squad-scale AI substrate | `packages/simulation/ai_nav.c`, `zombie_brain.c`, `ai_brain_rules.c`, `npc_archetype.c` | real per-agent navigation/decision code, integer rules |
| Destruction / support | `PACKET_BRICK_STATE` (13), `structure_rules.c`, `brick_bodies.c`, `brick_mod.c` | real load-cascade and shard conversion |
| Ragdolls | `ragdoll_pool.c`, `rigid_ragdoll.c`, `packages/goldenband/grb.c` | real 17-segment bodies, plane + AABB contacts |
| Match/state broadcast precedent | `PACKET_ZONE_STATE` (17), `PACKET_EMPIRE_STATE` (18), `PACKET_KIT_CALL` (19) | the shape a squad-state packet should copy |
| PvP scene plumbing | `SCENE_CUSTOM_LEVEL` (9) + `proc_dungeon.h`/`zone_registry.h` | one seed → identical level on both sides; the cheap way to get a map without a new scene id |
| Rendering | GOLDENBAND skinned meshes, GLSL inverted-hull outline, `env_matrix` weather | real, but **no hero or creep assets exist** |

## What is not here, and must not be assumed

- **`REDGARDEN` and `ECOWAR` are not in this checkout.** Neither directory exists at the monorepo root,
  so no lane/card/hero code can be read, ported, or diffed from this machine today. That is lane 0 and
  it blocks every other lane that claims to port something specific.
- **No `SCENE_ECOWAR`.** Scene ids stop at `SCENE_CUSTOM_LEVEL` (9). Adding an id before there is a
  generator that both sides can run deterministically repeats the mistake the zone work already avoided.
- **No card-play packet.** `PACKET_ARENA_CARD_PLAY` does not exist; the real packet table is
  CONNECT/USERCMD/SNAPSHOT/WELCOME/VOXEL/IMPACT/SCENE_CHANGE/DISCONNECT/RACING/PHEROMONE/WORLD_CLOCK/
  BRICK/QUEUE_LEVEL/SHIELD/SPRAY/ZONE/EMPIRE/KIT/VEHICLE/WOW_WIRE. A card play is a usercmd bit or a new
  packet with a real layout and a real decoder test — not a name.
- **No `PACKET_ECOWAR_MATCH_STATE`, no `PACKET_JUNGLE_BUFF_STATE`, no `commander_ai_rules.prn`, no
  `jungle_creep_rules.prn`, no `pheromone_targeting.prn`, no `rg_account.h`.** All of these appeared in
  the pasted blueprint; none exist in either repo checked out here.
- **No lane/creep/hero simulation at all.** `grep -ri "lane"` over `packages/` finds ragdoll contact
  planes, not lanes. Squads, waves, camps and shops are new work, not a port of existing SHANKPIT code.
- **No `PACKET_*` encryption or signed match tickets for gameplay.** The only real identity path is the
  IDUNA ticket in `packages/protocol/wow_ticket.h`, and it authenticates a *client*, not a match.

## Lanes, in order

### 0. Get the source of the thing being ported (blocked today)
Clone `REDGARDEN` and `ECOWAR` beside `SHANKPIT` and read them the way GFD was read: which files hold
the 10v10 match loop, the card effects, the creep camp tables, the hero roster. Do not trust the pasted
summary; it already got packet names, scene ids and rule filenames wrong.
*Done when:* a table in this doc names the real REDGARDEN/ECOWAR files and the real functions a port
would call, each verified by opening it.

### 1. One map both sides generate: a squad lane on `SCENE_CUSTOM_LEVEL`
Use the zone/dungeon precedent (`proc_dungeon.h`, one seed → identical `CustomLevelData`) to emit a map
with two spawn plates, a mid lane and three neutral camps as real boxes and spawners. No new scene id,
no new generator.
*Done when:* `--dungeon squad_lane` on two machines produces byte-identical level JSON and the existing
`PACKET_ZONE_STATE` reload path keeps them in sync.

### 2. Creeps as rules over the AI that exists
A creep camp is `ai_nav` + `zombie_brain` + a PARENA table (aggro radius, leash, respawn seconds,
wave composition) — not a new behaviour tree. The pheromone marker already gives the player a way to
pull aggro; wire the same `zombies_pheromone_should_steer_to_marker` decision to squad units.
*Done when:* a neutral camp leashes, aggros and respawns from a table in `.prn`, and a thrown pheromone
marker redirects an unengaged camp, both asserted headlessly.

### 3. Squads need a state packet, so design the layout before the feature
Copy `PACKET_ZONE_STATE`'s discipline: fixed fields, a decoder that refuses short/wrong-version input,
a test that asserts every byte. Start with the minimum that the server already knows — per-team wave
count, next wave in ms, camp ownership — not per-unit positions.
*Done when:* a `make test-*` proves the decode refusals and a 1 Hz repeat causes no client reload.

### 4. Heroes are an asset problem before they are a code problem
There are no hero meshes. GOLDENBAND can skin and ragdoll a rig, and the outline shader can mark teams,
but someone has to author (or generate) the meshes. Do not ship capsules with hero names and call the
port done.
*Done when:* at least one hero renders from a real mesh in the existing pipeline, and the code degrades
to a placeholder body only when the asset is missing — visibly, not silently.

### 5. Backend: reuse the identity that exists, then a read-only ladder page
Match auth should ride the same IDUNA ticket verification as `wow_ticket.h`; do not add a second auth
scheme (`rg_account.h` does not exist here). Publish results the way DEADWEIGHT's ladder already does
(`dw_net.c` → schema-1 JSON → WOTAN page) rather than inventing a game service.
*Done when:* a finished squad match produces one JSON document that a WOTAN page can read, and the
ticket check is the same code path the wire uses.

### 6. Only then talk about a second faction or the "living map"
The pasted design's best idea is also its most expensive: terrain that remembers damage (canopy loss,
persistent scars). SHANKPIT already has the ingredients — `PACKET_BRICK_STATE`, `structure_rules`,
`zone_registry` persistence — so the honest version is persistence of existing destruction state per
zone, not a new cellular automaton.
*Done when:* a zone reload reconstructs which bricks were destroyed without shipping geometry.

## Honest limits of this doc

No display on this box, no Go toolchain, and no REDGARDEN/ECOWAR checkout: lanes 1-3 can be built and
tested headlessly here, lane 4 cannot be seen, and lane 0 cannot even be started from this machine.
Every "done when" above is chosen so that it is checkable by a command, not by a screenshot.
