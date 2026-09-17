# AI Waypoint/Cover Navigation — NORTHSTAR

Founder real-time direction, 2026-09-17, two messages (routed via `emily observe`, Apples
#20016/#20020, curated into `EMILY/BACKLOG.md` S461): a full auto-pilot NPC AI design (sensor
layer, FSM, tactical pathing over a tagged node network, steering, squad AI), then a direct
follow-up asking grid/waypoint vs. polygon navmesh and confirming explicitly: *"we are going to
need a waypoint system in the levels and maps northstar it."*

## Why waypoint graph, not navmesh

SHANKPIT has no wall-collision data structure and no navmesh-bake pipeline anywhere in the
codebase (checked directly — grep for navmesh/pathfind/waypoint/astar across the repo turns up
nothing before this work; `packages/world/terrain.c` is a ground heightfield only, it has no
concept of walls, props, or any vertical obstacle). A polygon navmesh needs real level geometry
to bake from; SHANKPIT doesn't have that geometry in a queryable form yet. A hand-authored
waypoint graph needs none of that — it's the same real authoring convention `story_ai.c` already
uses for `AIPatrolPoint` (see `story_ai_seed_voxworld_encounter`), just extended to carry edges
and cover tags. This also matches the founder's own explicit preference (Half-Life 1 style,
simpler to write from scratch) over a modern navmesh.

## What's built (S461-01, packages/simulation/ai_nav.c/.h)

- `AINavGraph`/`AINavNode` — up to 32 nodes, up to 4 neighbors each, real bidirectional edges
  (`ai_nav_link`).
- `is_cover` + a normalized `cover_dir` per node — the direction FROM the node AWAY FROM the
  obstacle providing cover. A threat is only actually blocked when it lies roughly opposite
  `cover_dir` (dot product test, threshold -0.3).
- `ai_nav_find_path` — real A* over the graph's authored edges (not straight-line — respects
  actual topology), small-graph O(n²) open-list scan (fine at ≤32 nodes, no priority queue
  needed).
- `ai_nav_find_cover` — nearest reachable cover node that actually faces away from a given threat
  position, not just the nearest cover tile.
- Wired into `story_ai.c`'s `AI_MODE_FLEE` (previously a dead enum value with no behavior): a
  real health-gated flee (`health < 30`, matching `bot_client`'s own existing `w_retreat`
  convention, AND `courage < 0.7` so tank-identity roles like Gore Brute/Bombardier/Rift Hound
  fight through it) queries cover once, A*s to it, and holds there. Falls back to a straight
  repulsion vector away from the threat when no graph is authored for the scene or nothing in it
  qualifies as cover against the current threat — a real, expected case, not an error path.
- One real graph authored so far: a 6-node loop + 1 chord for the `SCENE_VOXWORLD` story
  encounter (`story_ai_seed_voxworld_encounter`), 2 of the 6 nodes tagged as cover. **The
  `cover_dir` values on those two nodes are a first, unverified pass** — SHANKPIT has no
  wall-collision data to check them against, so they're a plausible guess, not confirmed against
  the actual level layout. Real, named follow-up.

## What's deliberately terminal, not a bug

Flee has no re-engage path. Once triggered, an AI holds at cover (or its repulsion fallback)
forever. This is intentional: there is no health-regen system anywhere in this codebase, so a
re-engage would mean walking back into the same fight at the same low health that caused the
flee in the first place. "Wounded, hides, stays hidden" is the honest v0 behavior. A
regen-driven re-engage cycle is real, separate follow-up work, not scoped here.

## NOCK level-editor authoring — shipped (2026-09-17)

Founder real-time, direct follow-up: "we are going to need a waypoint system in the levels and
maps northstar it" / "continue filling in the gaps in our level editor." `SCENE_CUSTOM_LEVEL`
levels can now carry a real waypoint/cover graph end to end: `ShankpitLevelEditor.tsx` has a
"Waypoint / cover nodes" panel (place nodes at the spawner marker, toggle cover + direction, link
neighbors via a checklist — position edited numerically, same v0 scope `ObjectInspector` already
uses, not 3D-dragged), IDUNA's `internal/shankpit.NavNode` + `navNodesForExport` resolve authored
node ids into real export-time array positions, and SHANKPIT's new `LevelNavNode` parser
(`packages/world/level_boxes.h`) + `story_ai_load_nav_graph` load that graph into `g_story_nav`
when a custom level loads (`server_apply_custom_level`) — replacing the previous state where the
*only* way to get a graph into the running server was hardcoded C in
`story_ai_seed_voxworld_encounter`. See `EMILY/BACKLOG.md`'s own entry for full commit references.

## Real, not-yet-built follow-up work

- **Per-scene authoring for the built-in story scenes.** Only `SCENE_VOXWORLD`'s story encounter
  has a graph (hardcoded); the other built-in scenes (`SCENE_GARAGE_OSAKA`, `SCENE_STADIUM`,
  `SCENE_DUST_COMPOUND`, `SCENE_CITY`, `SCENE_OIL_TANKER`, `SCENE_POO_POO_ISLAND`,
  `SCENE_STORY_CAVE`) still have none — `ai_nav_find_cover`/`ai_nav_find_path` correctly no-op in
  an unauthored scene rather than crashing, but no NPC in those scenes gets real tactical behavior
  yet. `SCENE_CUSTOM_LEVEL` no longer needs this (NOCK authoring covers it, see above) — this item
  is now scoped narrowly to the built-in, non-custom scenes only.
- **cover_dir verification against real level geometry.** Flagged above — the VOXWORLD graph's
  two cover nodes need checking against the actual level once there's a way to do that (currently
  none — no wall-collision or prop-placement query exists anywhere in SHANKPIT).
- **General tactical pathing beyond flee.** `ai_nav_find_path`/A* is generic — patrol, investigate,
  and search could all route through the graph instead of `ai_move_towards`'s direct line once
  graphs exist for more scenes, giving real obstacle-respecting movement everywhere, not just
  during flee. Not attempted here — flee was the one real, concrete consumer named in the
  founder's own auto-pilot design ("Find the nearest node tagged IsCover..."), so it's the one
  built first.
- **Squad-aware retreat.** S461-03 (squad leader system, not yet built) could route a fleeing
  unit's cover choice through squad coordination (e.g. don't all flee to the same node) once both
  systems exist.

## Related

- `EMILY/BACKLOG.md` S461 — the full four-phase plan this fits into (arrival steering done,
  tactical pathing/flee done here, squad leader and scripted animation composition still open).
- `packages/simulation/story_ai.c`/`.h` — the FSM/sensor layer this plugs into.
- `packages/world/terrain.c` — the only existing world-geometry query (ground height only).
