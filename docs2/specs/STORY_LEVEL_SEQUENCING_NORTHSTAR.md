# Story Level Sequencing — NORTHSTAR

Founder real-time, 2026-09-17 (routed via `emily observe`, Apple #20064): "lets not work on
voxworld this is a legacy world building on it isnt useful ... we need a way to start devining
the story as the levels we build in the level editor we dont need the text cutscene in the
beginning we just need to spawn into the first map that the story is we need a way to define how
the levels fit together and then we need the loading points or whatever the opposite of the
spawners is? ... so we need a way to string 2 levels together and then once we have that lets get
our waypointing tools set up so i can define the waypoints and the scriptable interactions etc
... it needs to be flexible where we bake in some good defaults for how the npcs behave and
interact with extension points for overriding that default behavior via map scripts." Follow-up,
same thread: "like the bot detecting the player that should be baked in" / "but then the bot
waving at the player that should be configurable per map in some maps it may run up to you and
start talking then say follow me! then run and kill an enemy who knows."

**VOXWORLD is legacy as of this doc.** `story_ai_seed_voxworld_encounter`, the hardcoded Breach
Titan boss fight, and the S470/S471/S472 wandering-robot work all stay in the codebase (they're
real and working, and S472's live bug reports are still worth fixing opportunistically) but are
not receiving new feature investment. All new story content is authored in NOCK's level editor.

## The real architectural split (confirmed with the founder directly)

- **Detection is baked into the engine, never overridable.** Whether an NPC notices the player
  (distance/vision/hearing) is core AI plumbing — `ai_gather_perception` today, the
  `STORY_AI_GREET_RADIUS` check S470 added. A map author never needs to touch this.
- **The reaction is what's scriptable per map.** What happens once an NPC notices the player is
  map-specific creative content: S470's wave-then-dance is one example reaction, not *the*
  reaction. A different map might run up to the player, say a line, "follow me!", then run off
  and fight something else.

## Phase 1 — Level graph + exit points + skip the cutscene

The smallest real slice, and the one everything else depends on.

- **`next_level_id`** on a `Level` (IDUNA `internal/shankpit`, nullable — no next level is a real,
  valid "end of the story" or "not part of a chain" state). v0 is a **chain, not a general graph**:
  one `next_level_id` per level. A level with multiple real branches is real, later work if it's
  ever needed — nothing here blocks it, but a linked list is the honest scope for "string 2 levels
  together."
- **`is_story_start`** on a `Level`, mirroring `IsDefaultQueue`'s own exact real enforcement
  pattern (exactly one level may hold it at a time, same validation shape). `MODE_STORY`'s match
  init checks the registry for whichever level currently holds this flag; if one exists, fetches
  and applies it via the ALREADY-PROVEN-SAFE `server_apply_custom_level` path (the same one
  `queue_load_default_level` already calls mid-round) instead of
  `story_ai_seed_voxworld_encounter`, and sets `story_phase = STORY_PHASE_PLAYING` directly —
  `STORY_PHASE_CUTSCENE` is skipped entirely for this path (VOXWORLD's own hardcoded encounter
  keeps its cutscene, untouched, for whoever still runs it directly).
- **New `LevelExit` scriptable object** — a placed trigger volume (position + radius, same
  authoring shape as `NavNode`), the "opposite of a spawner" the founder was reaching for. A
  player entering it server-side triggers: fetch this level's own `next_level_id` via
  `level_boxes_fetch_export`, call `server_apply_custom_level` again (the exact mechanism
  `server_advance_queue_round` already proved safe for a live, connected match), respawn at the
  new level's own `Spawners`. v0 scope: `MODE_STORY` only — `MODE_QUEUE`/deathmatch levels don't
  have level-graph semantics and don't need this.
- Full pattern for each new field/object: IDUNA Go struct + validation + migration + handler
  wiring + `ExportDoc` resolution + NOCK frontend inspector — the same four-times-proven shape
  Doors (S463) → NavNodes (S465) → Characters (S468) already established.

## Phase 2 — NPC reaction scripts

Founder's own explicit call (asked directly, answered directly): **both**, phased.

- **v0: native JSON step-list**, authored directly in the NOCK level editor. An ordered sequence
  of steps (`SAY <line>`, `FOLLOW <player>`, `MOVE_TO <x,y,z>`, `SET_MODE <mode>`, `WAIT <ms>`,
  etc. — exact step vocabulary TBD when this phase starts). Generalizes `AI_MODE_SCRIPTED`
  (S461-04, currently a single walk-to+hold step) into a real multi-step sequence runner. A
  `Character` (or a new per-trigger reference) names which script to run instead of the built-in
  default reaction. S470's wave-then-dance becomes the first named built-in script in this
  library, not a special case.
- **v1: PARENA escape hatch**, for anything a flat step-list genuinely can't express — the
  founder's own example: a level where picking up an item grants immunity to a later scripted
  explosion. That's real cross-entity conditional state, not a linear sequence — needs actual
  branching/state, which JSON-step-list deliberately does not attempt to grow into. SHANKPIT has
  zero PARENA integration today (unlike PAPERCRAFT/ECOWAR/GTA7/DEADWEIGHT's own established
  "mods first everything" convention) — embedding it here is real, separate, not-yet-scoped work:
  needs a runtime embed or FFI bridge, a real emit target or interpreter path, and NOCK authoring
  UI for `.prn` scripts. Not started. JSON v0 is NOT a throwaway prototype for this — it stays the
  fast, designer-friendly onramp permanently; PARENA is additive for the cases JSON genuinely
  can't reach, not a replacement.

## What this deliberately does not cover yet

- A general level GRAPH (branching story paths, multiple exits to different levels) — v0 is a
  chain. Real, later work if the story ever needs to branch.
- Any PARENA integration in SHANKPIT at all (Phase 2's own v1).
- Retrofitting VOXWORLD's own hardcoded encounter onto this system — it stays as its own,
  separate, legacy path.
- The exact JSON step vocabulary for Phase 2 — named as real future work, not designed yet.

## Related

- `docs/STORY_SYSTEM_NORTHSTAR.md` — the original scriptable-object arc (doors, waypoints,
  characters) this phase's own "full pattern" line reuses directly.
- `docs2/specs/AI_SCRIPTED_ANIMATION_NORTHSTAR.md` / `EMILY/BACKLOG.md` S461-04 — `AI_MODE_SCRIPTED`,
  the primitive Phase 2's reaction-script runner generalizes.
- `EMILY/BACKLOG.md` S470/S471/S472 — the VOXWORLD wave-then-dance work this doc explicitly
  deprioritizes going forward (stays live, not receiving new investment).
