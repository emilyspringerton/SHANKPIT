# MODE_TYLER — TYLER VALHANNA cold open as a real SHANKPIT game mode (S536)

Founder real-time, in sequence: *"bring it to life with the shankpit engine... write a new game
mode called TYLER... use the engine to tell the story like helf life - the characters speak...
you are a floating orb like a wisp... they dont see you... use nock levels... there is a spawn
but no exit for this level... script it in with REFLX=UX... the exit needs to be a new primative
for exit... make exit scriptable... write events for the half life style coordinated animations...
use manequins... get the events right and the animations just fake it... use rigid body so later
when you can interract with the environment we can... just a wisp you can fly through the walls...
use the blocks in the nock editor... when you wake up wake up as the duck in a new level with the
cube the default new nock level make that CONSTRUCT... the duck needs to be a separate character
model have it be the robot that has the girl name with the 2 legs... look into the BIG_O
oooooooold construct fille for an implementation of this... combat still needs to work in regular
shankpit... create new shaders and materials and procedural textures... i have added a few
ecxperimental parena generated textures... play around witth the parena texture generator too"* →
(mid-build) *"oh no ITS IN PAPERCRAFT"* → *"THE OLD SHANKPIT CONSTRUCT WAS THE ORIGINAL REBOOT OF
THE ENGINE INTO PAPERCRAFT"* → *"WE NEED TO MERGE THE 2 ENGINES THIS IS LIKE BUILDING HALF LIFE 1
AND 2 AT THE SAME TIME ITS READY TO MERGE IT ALL IN"* → *"THIS IS A DEMO FOR BIG_O"*.

This is the real content vehicle for TYLER's own VALHANNA cold open
(`TYLER/episodes/vh01_valhanna_coldopen.md`), built as a genuine v0 slice of the S536 BIG_O↔
SHANKPIT engine merge track (`docs2/specs/BIGO_ENGINE_MERGE_NORTHSTAR.md`) — a demo proving this
one engine can carry a scripted, mannequin-driven first-phase sequence (BIG_O's own "day" register)
and a third-person, physics-controlled second phase (BIG_O's own "night"/social register) back to
back, in one game mode, on top of infrastructure this repo had already built for other reasons
(REFLUX, `story_ai`'s scripted-sequence AI, `gband_skel_npc`'s five mannequin kits, the live
level-transition mechanism).

## What's real and verified this pass

- **`MODE_TYLER=109`** (`packages/common/protocol.h`), reachable on the dedicated server via a new
  `--tyler` flag (`apps/server/src/main.c`), same gap `--story` closed for `MODE_STORY` at S465.
- **`STATE_SPECTATOR` made real** (`packages/common/protocol.h`, `packages/simulation/
  local_game.h`'s `update_entity`) — defined since before this pass but, checked directly, never
  actually read by any collision/gravity code anywhere. Now a real, reusable, mode-agnostic
  free-fly (no collision, no gravity, `BTN_JUMP`/`crouching` for vertical control) — the wisp.
  Deliberately not pitch-driven: `PlayerState.pitch` is a real wire field nothing ever actually
  populates from a client's real look angle (a separate, pre-existing gap, named not fixed).
- **REFLUX as the real scripting spine.** `REFLUX_ACTION_TYLER_BEAT` (`packages/reflux/
  reflux_runtime.h`) dispatched once per beat transition; the coordinator also dispatches the
  *existing* `REFLUX_ACTION_BUTTON_PRESSED` for Tyler's own scripted "press the button" beat —
  same payload shape a real player press already uses, REFLUX doesn't distinguish the two.
- **A real Half-Life `scripted_sequence`-style coordinator** (`packages/simulation/
  tyler_coldopen.{h,c}`) — an 8-beat fixed table (`TylerBeat`), each beat calling the *already-
  built* `story_ai_trigger_scripted`/`AI_MODE_SCRIPTED` (S461-04) once per actor with a hand-tuned
  hold covering the whole beat. Real, named simplification: no query exists anywhere in
  `story_ai.h` for "is this scripted move actually done," and `gsync`'s own real multi-actor-
  ignition primitive (`packages/goldenband/gsync.c`) isn't wired into `story_ai_trigger_scripted`'s
  call path either — this coordinator runs its own independent beat clock instead of either.
- **A genuinely new, scriptable "exit" primitive**, not a `LevelExit` proximity trigger (this level
  authors none at all — "spawn but no exit" is the *absence* of any exit, not a locked one, since a
  non-colliding wisp has no reliable way to walk into a volume anyway). `story_force_level_
  transition(next_id, target_spawner_id, now_ms)` (`apps/server/src/main.c`) is `story_check_
  level_exits`' own already-proven transition body, factored out so the coordinator's final beat
  can call it directly the instant Tyler's own scripted "press" completes.
- **Two real levels, live in IDUNA's own NOCK registry** (not local throwaway JSON — matches
  `SHANKPIT/CLAUDE.md`'s own "Level Registry Doubles as Living Documentation" standing
  instruction): `TYLER_VALHANNA_ICELAND_1986` (id 24) and `CONSTRUCT` (id 23,
  `next_level_id`-chained from the first), created via a real one-shot loader
  (`IDUNA/cmd/nock_gen_tyler_levels`) directly against `internal/shankpit.LevelStore` — walls
  (server-room shell, four empty racks, the printer, the ECS monitor box, the button), one
  spawner, and two `AI_ROLE_STORY_ALLY` characters (Tyler = `AI_KIT_STAN`, Hana = `AI_KIT_MIKE` —
  placeholder kit assignments, named honestly, not new character art).
- **Two real PARENA-generated procedural textures**, compiled through the actual, already-existing
  PARENA→Java procgen pipeline (`IDUNA/internal/nock/procgen.go`), not hand-drawn: `PARENA/stdlib/
  shankpit/textures/institutional_tile.prn` and `ecs_screen_glow.prn`, loaded via a one-shot Go
  program (`IDUNA/cmd/nock_gen_textures`, bypassing `texture-generate`'s own Vertex AI requirement
  by calling `CreateProceduralTexture` directly with hand-authored source) — real ids 25/26 in
  NOCK's texture library, real rendered PNGs visually checked (and one real bug caught this way:
  `min64` vs `max64` in the tile's own seam function, producing corner-dots instead of a grid
  until fixed). **Real, honest limit, not glossed over**: this renderer has no UV-texture-mapping
  path anywhere (`packages/render` — checked directly, zero PNG/texture loading exists; every
  `LevelBox` is flat-shaded via `glColor3f`). The two levels' own walls use plain RGB colors
  sampled from the real generated PNGs (and `Material` names matching the texture library entries,
  for traceability) — an honest placeholder, not a claim that the floor is actually texture-mapped.
- **Third-person Duck camera** (`apps/lobby/src/main.c`) — a real spherical orbit
  (`cam_dist·cos/sin(pitch)`), the exact formula PAPERCRAFT's own `apps/client/src/main.c` already
  proved (confirmed by reading it directly, not assumed — this is what "the BIG_O construct file"
  turned out to actually be: `PAPERCRAFT/SHANKPIT_CONSTRUCT.txt`, the frozen source snapshot
  PAPERCRAFT's own engine rebooted from), ported into this engine's *existing* `cx`/`cz`/`cam_y`
  third-person mechanism (already live for vehicles/death-cam) rather than a second camera path.
  Gated on `MODE_TYLER && forced_kit==AI_KIT_LEELA` — every other mode's camera is untouched.
- **The Duck renders as Leela** — `gband_skel_npc`'s own five-kit roster (S468) already has a
  two-legged, no-arm rig under a girl's name: `AI_KIT_LEELA` (17 joints vs. Stan/Mike/George's
  43/43/47 — checked directly against `docs2/specs/AI_SCRIPTED_ANIMATION_NORTHSTAR.md`'s own
  joint-count audit). `draw_player_3rd`'s `SKIN_MANNEQUIN` dispatch, previously gated to `is_bot`
  only (S466), now also fires for a REAL, human-controlled player whose own `forced_kit` was set
  server-side (`tyler_apply_phase_override`) — the one place in the engine a real player's own
  forced kit, not just a spawned NPC's, is honored, closing the gap S492's own doc comment named.
- **Combat in every other mode is untouched.** Every change above is additive and mode-gated
  (`MODE_TYLER` or a `forced_kit`/`STATE_SPECTATOR` check nothing else sets) — `make server`/
  `make lobby` both build clean, zero new warnings beyond this repo's own large pre-existing set.
- **Live-verified end to end, server-side, not just compiled**: `--tyler --level examples/
  tyler-valhanna/tyler_1986_iceland.json` on a real running dedicated server (verbose logging
  build) shows `MODE_SELECTED mode=109` → `CUSTOM_LEVEL_CHARACTERS_SPAWNED count=2` →
  `TYLER_COLDOPEN_STARTED tyler_slot=1 hana_slot=2` → (all 8 beats run their real hold timers) →
  `STORY_LEVEL_TRANSITION next_level_id=23 name=CONSTRUCT target_spawner_id=0` — the real exit
  primitive firing, the real live IDUNA registry fetch succeeding, no crash, no hang.

## Real, honest, not landed this pass

- **Rigid body**: named as a forward-compatible marker only, per the founder's own "so LATER we
  can" framing — no per-tick simulation wired to the cube/thermos props this pass. The real
  backend for that later work already exists and doesn't need inventing:
  `packages/simulation/rigid_ragdoll.{h,c}` (2026-09-27, XPBD via `packages/goldenband/grb`) is a
  genuine rigid-body solver, currently wired to the mannequin skeleton only via a standalone tool
  (`tools/rigid_ragdoll`) — extending `GrbWorld` to free (non-ragdoll) interactable props is the
  real, scoped next step, not a new physics system.
- **Subtitle rendering only reaches LOCAL single-player mode.** `apps/lobby/src/main.c`'s new HUD
  block (small, bottom-left, explicitly NOT full-screen, per the founder's own ask) polls THIS
  PROCESS's own REFLUX log — real and correct when `apps/lobby` itself is running the simulation
  (`local_game.h`'s `local_update`), but REFLUX has no wire-protocol packet at all
  (`packages/reflux/reflux_runtime.c`'s own doc comment), so a client connected to a separate
  dedicated `apps/server` process cannot see that process's own dispatches. **`tyler_coldopen_
  start`/`tick` are also only wired into the dedicated server's tick loop this pass** — `local_
  game.h`'s own `local_update`/`local_init_match` needs the equivalent wiring (plus lobby's own
  separate, hand-maintained character-spawn implementation around `apps/lobby/src/main.c:2415`
  touched) for local single-player play to work end to end; not attempted this pass given time,
  named here rather than silently left broken.
- **The "ECS screen" and "exit button" are found by hardcoded world position**, not by name or by
  material lookup — `Wall.Name` (IDUNA `internal/shankpit.Wall`) is confirmed, by its own doc
  comment, purely a NOCK-authoring convenience the native client's hand-rolled JSON scanner never
  parses at all. A general material-name-based prop lookup would be the natural, reusable
  follow-up; not needed for one bespoke level this pass.
- **No visual verification of any client rendering** — this sandbox has no GL driver, same
  standing limitation every prior SHANKPIT/BIG_O client-render pass in this repo already carries.
  Compiled-and-linked-correctly plus the real server-side end-to-end log trace above is the real
  bar this pass clears.
- **`AI_MODE_SCRIPTED` client-side clip selection** (which animation actually plays during a hold)
  remains the same real, already-named gap `AI_SCRIPTED_ANIMATION_NORTHSTAR.md` closed structurally
  (mannequin kits render and idle/walk-switch for real) but never finished connecting to scripted-
  mode-specific clip choice — Tyler/Hana play whatever `gband_skel_npc`'s existing idle/walk
  switching already does, not a bespoke "read printout"/"plead" gesture.

## Engine-merge continuation — third_person becomes a real, mode-agnostic capability

Founder real-time, same session: *"WE NEED TO MERGE THE 2 ENGINES THIS IS LIKE BUILDING HALF LIFE
1 AND 2 AT THE SAME TIME ITS READY TO MERGE IT ALL IN"* → *"THIS IS A DEMO FOR BIG_O"* → (next
session) *"continue to merge the 2 engines."* The pass above proved the tech (PAPERCRAFT's real
orbit camera works inside SHANKPIT's own pre-existing `cx`/`cz`/`cam_y` mechanism) but shipped it as
a demo-shaped hack: the camera branch was gated on `local_state.game_mode == MODE_TYLER &&
render_p->forced_kit == AI_KIT_LEELA` — two unrelated concerns (which game mode is running, which
skin a player wears) standing in for the one real thing that should gate a camera choice: does
*this player* want third person.

**What changed this pass**: a new, standalone, wire-synced `PlayerState.third_person` /
`NetPlayer.third_person` field (`packages/common/protocol.h`), following `forced_kit`'s own exact
established pattern (server sets it, `apps/server/src/main.c`'s snapshot loop serializes it,
`apps/lobby/src/main.c` deserializes it into the local mirror). The camera branch in
`apps/lobby/src/main.c` now keys on `render_p->third_person` alone — mode-agnostic, skin-agnostic.
`tyler_apply_phase_override` is still the only real caller (the Duck phase sets `third_person=1`
alongside, not instead of, `forced_kit=AI_KIT_LEELA`; the wisp phase sets both to 0/off) — that
hasn't changed behaviorally. What changed is that this is no longer MODE_TYLER's own private hack:
any future mode (the real target being `MODE_STORY`'s not-yet-built night/social-stealth register,
per `BIGO_ENGINE_MERGE_NORTHSTAR.md`) can now request third person for a real player by setting one
field, with no new camera branch to write.

**Verified**: `make server`/`make lobby` both build clean, zero new warnings. Re-ran the exact same
live end-to-end smoke test (`--tyler --port 16969 --level examples/tyler-valhanna/
tyler_1986_iceland.json`, non-conflicting port so as not to touch the live `:6969`/`:6971`
services) — identical log sequence (`MODE_SELECTED mode=109` → `CUSTOM_LEVEL_CHARACTERS_SPAWNED
count=2` → `TYLER_COLDOPEN_STARTED` → `STORY_LEVEL_TRANSITION next_level_id=23 name=CONSTRUCT`),
zero behavior drift from the generalization.

**Real, honest, not landed this pass**: `MODE_STORY` itself does not yet set `third_person` for
anyone — there is no "night phase" gameplay concept built yet (`lab_sim.c` still has zero UI/
interaction model, per `BIGO_ENGINE_MERGE_NORTHSTAR.md` §3). This pass makes the flag real and
ready, it does not invent a second consumer speculatively. No aiming/hitscan adjustment was made
for a hypothetical third-person combat mode — every live combat mode still defaults `third_person`
to 0 via the same zero-initialization `forced_kit` already relied on, so "combat still works in
regular SHANKPIT modes" holds exactly as before, unchanged.

## Related

- `TYLER/episodes/vh01_valhanna_coldopen.md` — the real script this mode stages.
- `docs2/specs/BIGO_ENGINE_MERGE_NORTHSTAR.md` (S536) — the engine-merge track this demo serves.
- `docs/STORY_SYSTEM_NORTHSTAR.md` — the scriptable map-object model (`doors`/`characters`/
  `LevelExit`) this pass's own new "exit" primitive extends rather than replaces.
- `docs2/specs/AI_SCRIPTED_ANIMATION_NORTHSTAR.md` — `story_ai_trigger_scripted`/`gband_skel_npc`,
  reused as-is, not re-implemented.
- `PAPERCRAFT/SHANKPIT_CONSTRUCT.txt` / `PAPERCRAFT/apps/client/src/main.c` — the real source of
  the third-person orbit camera this pass ports in.
- `packages/simulation/rigid_ragdoll.{h,c}` — the real rigid-body backend named for later.
