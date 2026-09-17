# SHANKPIT Story System NORTHSTAR

Founder real-time, across several turns of real design conversation: "how can we make maps and
objects scriptable ideally with parena [...] doors, ladders, in game computer screens" ->
"via the nock tools" -> "I want to [...] make a level loading system like half life used [...]
we can have start and end markers for the level and then we can have stories that stitch the
levels together. we can even use the same levels to create 2 stories that vary vastly just due
to the order of the chapters (novel called crossings does this)" -> "we actually need multiple
entrances or exits [...] almost a tree structure but not totally think about time loops and
arbitrary story definition if exit b go here if exit a go here if 2 decisions ago you said a
then next time we go c etc parena scriptable" -> "also scriptable characters state machine
dynamic idle look around check watch etc" -> "also scriptable player events like hitting a
certain hallway loads in the next enemies trigger sounds trigger events like characters falling
out of a vent etc."

This doc scopes three real, coupled systems in the order they actually depend on each other, not
a flat feature list. Nothing here is built yet -- this is the design pass before code, same
discipline `docs/ANTICHEAT_NORTHSTAR.md` already established for this repo.

## Why these three systems, in this order

1. **Per-level scene identity** (blocking, must go first). Checked directly, not assumed: every
   NOCK-authored custom level shares one hardcoded `SCENE_CUSTOM_LEVEL` scene id
   (`packages/common/physics.h`) -- the server/client can tell "this is a custom level" but not
   *which* one. A story system that says "go from level A to level B" has nothing to name B
   with today. This was already a real, found-live bug this session (spray decals leaking
   across level switches, S459-74) caused by exactly this gap.
2. **Scriptable map objects** (doors/ladders/screens, ambient characters, and generic trigger
   volumes -- which entrance/exit markers turn out to be a special case of, see below). A
   level's `LevelBox` array (`packages/world/level_boxes.h`) today carries only geometry + color
   + material name + an unused `friction` field -- no trigger/interact/script concept exists
   anywhere in SHANKPIT. An "exit marker" IS an interactable object (a trigger volume that runs
   a script when a player enters it), so this has to exist before the story system has anything
   to hook into.
3. **The story engine** (the actual "stitch levels into a narrative" system). Depends on both of
   the above: it needs levels to be individually addressable (#1) and it needs a real place to
   attach "when the player reaches exit marker B, run this decision" (#2).

## Part 1: per-level scene identity

**Real, current state** (checked against the code, not assumed): `apps/server/src/main.c`'s
`--level` arg loads one specific level file via `level_boxes_load_from_file` +
`server_apply_custom_level`, and `apps/lobby/src/main.c` mirrors it client-side via
`level_boxes_apply_to_physics` (called at load and on scene-entry, e.g. around line 7064 and
8771-8772). The server broadcasts `SCENE_CUSTOM_LEVEL` as the scene id regardless of which real
level file was loaded -- both sides independently load the SAME geometry only because both were
launched with the identical `--level` path, an out-of-band assumption, not something the wire
protocol actually encodes.

**The fix**: NOCK-authored levels already have a real, stable identity -- the level row's own id
in IDUNA's `shankpit_levels` table (`internal/http/handlers/shankpit_levels.go`). Broadcast that
real level id alongside (not instead of -- existing scene-id-keyed code stays valid for the
built-in scenes) `SCENE_CUSTOM_LEVEL`, and have client-side scene-entry logic
(`client_apply_scene_id` and friends) key any per-level state (decals, exit markers, story
context) off `(scene_id, level_id)` instead of `scene_id` alone. This is a real, scoped wire
protocol change (same category as S459-69's `vx/vy/vz` addition to `NetPlayer` this session --
grow the struct, rebuild/redeploy client+server+bots in lockstep) and a real client-side keying
change, not a rewrite of the level system.

## Part 2: scriptable map objects (doors, ladders, screens, characters, triggers)

**The real, proven pattern to extend** (not invented here): PAPERCRAFT's `level_mod.prn` is the
one checked-in precedent for PARENA driving real gameplay decisions inside a hand-written C host.
`parena build` emits a plain C function with a native C signature (no FFI shim, no name
mangling) into a generated `.c` file that gets committed and linked straight into the host binary
(`packages/simulation/level_mod.c`, consumed by `apps/server/src/main.c` via a simple forward
declaration + direct call). SHANKPIT already has a `dlopen`/`dlsym` proof of concept
(`apps/dynmod_poc`) for the alternative, runtime-loadable version of the same idea -- load a
compiled `.so` at level-load time instead of linking it in at build time.

**Object model**: a new map-object type, distinct from the pure-geometry `LevelBox` array --
call it `LevelInteractable` (name TBD at implementation time): position/orientation, an object
`kind` (door | ladder | screen | character | trigger, extensible), and a `script_asset_id`
referencing a real, compiled behavior. A **narrow, fixed function contract per kind** (same
discipline `internal/nock/procgen.go`'s `validateProcTextureSource` already enforces for texture
scripts -- reject anything outside the contract, don't trust the emitter alone):

```
(defn door-tick [(dist-to-nearest-player : F64) (input-held : Bool) (door-state : DoorState)] : DoorState ...)
(defn ladder-tick [(player-vertical-input : F64) (on-ladder : Bool)] : F64 ...)   ; returns a vertical velocity override
(defn screen-render [(state : ScreenState)] : ScreenContent ...)                  ; what text/image the screen shows
(defn character-tick [(state : CharacterState) (idle-timer : F64) (player-nearby : Bool)] : CharacterState ...)
(defn on-trigger [(already-fired : Bool)] : TriggerAction ...)
```

Ladders, doors, and characters are real per-tick state machines the server evaluates every tick
for every placed instance (same tick-loop shape bot AI already uses in
`apps/server/src/main.c`'s main loop -- a new "map object update" pass slots in right after the
per-player physics pass, before snapshot broadcast, per this session's own earlier finding).
Screens are lower-frequency (re-evaluated on interaction, not every tick). Triggers are
lowest-frequency of all -- evaluated once, on volume entry (see below).

**Characters** (founder real-time: "also scriptable characters state machine dynamic idle look
around check watch etc"): `CharacterState` is a small enum (Idle, LookAround, CheckWatch, ...
extensible), and `character-tick` is weighted-random-ish transition + per-state timer logic --
the same shape a real ambient-NPC behavior system always is, no new mechanism invented. Each
`CharacterState` maps to a real `.gband` clip pulled from the NOCK animation repository
(S144-09, this session) for playback. One real, named gap: `gb_blend` (GOLDENBAND's runtime
interpolator) only nlerps between adjacent ticks of the SAME clip -- a smooth cut between two
DIFFERENT clips (Idle -> LookAround) needs a small new cross-clip blend helper that doesn't
exist yet. Cosmetic-only (a hard cut works as a real, if rougher, v0), not a blocker.

**Ambient dialogue** (founder real-time: "random interactions where the ais ask a question and
the other gives a random answer like half life" -- HL2's own real citizen-bark precedent): a
`Talking` `CharacterState` two nearby characters can both enter together, driven by a small
question/answer pool rather than new mechanism -- one character's `character-tick` picks a
question line (weighted-random, same as any other state transition), the other picks a matching
answer line, both play a real `.gband` talk-gesture clip for the exchange's duration. Line pools
are just data (small string/id tables), not scripted logic -- `character-tick`'s own real
contract only needs to return "which line index," the host resolves index -> actual text/audio,
same "script returns data, host executes" discipline every other kind here already uses.

**Trigger volumes, generalized** (founder real-time: "also scriptable player events like hitting
a certain hallway loads in the next enemies trigger sounds trigger events like characters
falling out of a vent etc"): rather than a separate `exit_marker`/`entrance_marker` kind, a
single `trigger` kind covers all of it. `on-trigger` returns a `TriggerAction` -- a tagged
result the C host interprets and executes, not a live callback the script makes itself:

```
TriggerAction = SpawnEnemies(spawner-ids) | PlaySound(sound-id) | SpawnCharacterEvent(character-id, entry-point) | AdvanceStory(exit-id) | NoOp
```

This is the same "script returns data, host executes" discipline `door-tick` already uses (a
door script returns a new `DoorState`, it doesn't directly move geometry), kept deliberately
narrow: PARENA never gets a live handle into game internals, only a fixed, host-defined
vocabulary of actions. The real tradeoff, named honestly: that vocabulary has to be extended
deliberately every time a genuinely new action type is needed (can't express arbitrary new
behavior without a host-side code change) -- less flexible than free-form scripting, far easier
to reason about safety- and performance-wise, and consistent with every other kind above.
`AdvanceStory` is what makes an "exit marker" real: it's just a `trigger` volume whose script
returns `AdvanceStory`, which is what Part 3 below actually hooks into -- there is no separate
marker kind after all.

**Sound** (founder real-time: "and scriptable sound for voice overs and enemies also different
footsteps sounds per material etc"): two genuinely different mechanisms, not one. Voiceover/
enemy sound is already covered -- it's just more `PlaySound`/dialogue-line-index outputs from
`trigger`/`character` scripts above, no new plumbing. Per-material footsteps are NOT
PARENA-scriptable and don't need to be: `LevelBox` already carries a real `material_idx` into
`LevelBoxMaterial materials[]` (`packages/world/level_boxes.h`), so footstep sound is a plain
data lookup (material name/id -> sound id) the client does locally based on which box the
player's currently standing on -- same category as the material's existing `shader_name` field,
not scripted logic.

**Target/trust question, resolved**: `procgen.go`'s own texture pipeline deliberately avoided
PARENA's C target for *LLM-generated* source specifically because an unrestricted `#target`/
inline-C escape hatch is a real, unsandboxed code-exec vector when the actual code author is a
model, not a human. Map-object scripts are hand-authored by a trusted map designer through NOCK
(same `iduna.admin`-gated surface every other NOCK authoring tool already requires) -- the same
trust level as any other server-side code an admin ships. The C target is the right choice here,
not a compromise: it's what PAPERCRAFT's own precedent already uses, and it's what makes the
per-tick door/ladder evaluation cheap enough to run for every instance, every tick, in a 60Hz
server loop (no JVM round trip the way the texture pipeline's Java target would require).

**Elevators** (founder real-time: "we need scriptable elevators with doors as NPCs spawners and
level stitch points"): not a new, seventh object kind -- a real COMPOSITE of three kinds already
scoped above, plus one genuinely new mechanism. An elevator is:
- a `door` (or two -- the elevator's own doors, opening/closing at each stop, same `door-tick`
  contract already built and tested, S459-81/82),
- a `trigger` at each floor stop, firing `SpawnEnemies`/`SpawnCharacterEvent` on arrival (an
  ambush waiting on the floor above, a character riding along) or `AdvanceStory` at a top/bottom
  floor that's really a level-stitch point (exactly Part 3's own `AdvanceStory` action below --
  riding an elevator to the top floor is a real `exit-taken` the story engine already knows how
  to handle, no special case needed),
- plus one real, new primitive neither door nor trigger has: **platform movement** -- the one
  thing genuinely missing is a box that translates smoothly between real, named stop positions
  over time, not just toggling open/closed like a door does. `phys_set_custom_level_box_y`
  (S459-81) already proves the mechanism (server mutates a `map_geo` box's own position, players
  standing on it get carried because collision is resolved fresh every tick against wherever the
  box currently is) -- an elevator generalizes it from a single Y-offset toggle to a real,
  multi-stop position interpolation: `(defn elevator-tick [(state : ElevatorState) (call-floor :
  F64)] : ElevatorPose ...)` returning a real `(x, y, z, door-state)` pose per tick, the same
  "script returns data, host executes" discipline every other kind already uses. Real, deferred
  design question, not solved here: whether a moving elevator car needs its own dedicated
  `LevelInteractable` kind (`elevator`) that bundles a door + a trigger + this new pose-return
  contract into one authored object, or stays three separately-placed, script-linked objects a
  designer wires together by hand. Not built in this pass -- named here so the composition is
  clear before Phase 2 (ladder/screen/character/trigger kinds) picks a real answer.

## Part 3: the story engine

**The real shape, from the founder's own framing**: a story is not a fixed list or a fixed tree
-- it's a **PARENA decision function evaluated against accumulated history**:

```
(defn next-chapter [(history : Vec ChapterChoice) (exit-taken : ExitID)] : ChapterRef ...)
```

where `ChapterChoice` is a real, small record (which level, which exit, at what point in the
history) and `ChapterRef` names the next level id (Part 1) + which entrance marker (Part 2) to
spawn the player at. This single shape covers everything named across this design conversation
without special-casing any of it:

- **Batteries-included default**: a trivial `next-chapter` that ignores `history` entirely and
  returns `history[len-1] + 1`'s level in a fixed list -- a straight-line story, zero scripting
  required, the same "works with no editing" bar the NOCK texture/animation import forms already
  hold themselves to.
- **Multiple entrances/exits per level**: not a special case -- a level just has more than one
  Part-2 marker, and the same level can appear more than once in a story (different entrance
  each time) since `ChapterRef` is just data, not a unique tree position.
- **"Almost a tree but not totally," time loops, "2 decisions ago"**: all free consequences of
  `next-chapter` being an arbitrary pure function over the real, ordered `history` vector rather
  than a graph traversal with no memory -- looking back N choices is just indexing the vector;
  revisiting the same level with a different outcome is just the same level id appearing twice
  in different `ChapterRef`s the function returns for different histories.
- **"Two stories from the same levels, wildly different due to chapter order" (the Crossings
  reference)**: two different `next-chapter` scripts pointing at the same pool of level ids is
  exactly that -- the level pool and the story logic are already cleanly separated in this
  model, not coupled.

**Where it runs**: `next-chapter` is evaluated server-side, once, at the moment a `trigger`
volume's `on-trigger` script returns `AdvanceStory(exit-id)` (Part 2) -- not a per-tick cost.
Same PARENA C-target / `level_mod.prn`-style linking as Part 2's objects; a story's compiled
behavior is really just another script asset the object system already has a place for.

## NOCK authoring surface

Follows the exact pattern this session already proved twice (the blank-slate PARENA texture
editor, S459-77; the animation repository's drag-and-drop, S144-09): a new NOCK surface for
placing interactable objects on a level (reusing the existing level editor's canvas,
`frontend/nock/src/ShankpitLevelEditor.tsx`) with a PARENA source textarea per placed object,
pre-filled with a real, working starter template per `kind` (matching
`PARENA_STARTER_SOURCE`'s own role for textures) so "Create" works unedited, compiled and
validated server-side the same way `internal/nock/procgen.go` already compiles-and-rejects bad
PARENA before ever touching a running server. Story authoring is a separate, simple ordered-list
UI for the batteries-included case (drag levels into an order, done) with an "advanced" toggle
that reveals the same raw PARENA textarea for a real custom `next-chapter` script.

## Real, phased build order

1. **Phase 0**: per-level scene identity (Part 1). Blocking, small, mechanical. Not yet done --
   Phase 1 below was built and tested first, against a single hand-authored test level, so it
   didn't need a real level identity yet; this stays a real blocker before multiple distinct
   custom levels with their own doors can coexist correctly.
2. **Phase 1 -- DONE, 2026-09-16, door kind only.** The real plumbing exists and is tested end to
   end: `LevelDoor` (`packages/world/level_boxes.h`, `box_index` + `script_path`, parsed from a
   level's own `doors` JSON array), the PARENA->C compile pipeline (`parena build door_tick.prn
   -o door_tick.c` + `gcc -shared -fPIC`, real, not automated yet -- see below), server-side
   `dlopen`/`dlsym` loading and once-per-tick evaluation (`packages/world/story_doors.h`,
   `story_doors_init`/`story_doors_tick`, wired into `apps/server/src/main.c` right after
   `update_projectiles`), and the actual collision effect (`phys_set_custom_level_box_y`,
   `packages/common/physics.h` -- relocates the box 1000 units below its authored position when
   open, restores it when closed). Real example script + compile instructions:
   `examples/story-doors/door_tick.prn`. Live-verified with a real running server and a real
   connected UDP test client (not just unit-level): an "always closed" control script proved the
   player gets and stays genuinely stuck at the box surface (z held exactly at the collision
   boundary for 380 real ticks), and the real hysteresis script (open within 3 units, close
   past 5) proved the player walks straight through once close enough.
   **Follow-up, same day -- founder: "you know what we are tryna do fill in the gaps."**
   IDUNA-hosted script compile/storage is now real: `internal/nock/door_script_compile.go`
   (real `parena build` + `gcc -shared -fPIC`, same two-real-step shape `procgen.go` already
   automates for the Java-target texture pipeline) + `door_script_store.go` (CRUD, admin-gated
   author API at `/admin/nock/api/door-scripts`, public download at
   `/api/v1/nock-door-scripts/:id/download` -- SHANKPIT's own server has no IDUNA login, same
   posture shankpit-levels' own public export already has). `LevelDoor` gained `script_url`
   (`packages/world/level_boxes.h`); `story_doors_init` downloads and caches it locally before
   `dlopen`, script_path staying a real local-dev fallback. Live-verified as one real, full
   closed loop: compiled a script through IDUNA, confirmed byte-identical output to a
   hand-compiled one, downloaded it back over real HTTP into a real running SHANKPIT server, and
   a real connected client walked through the resulting door -- the entire "via the nock tools"
   gap from this doc's own opening line is closed for the door kind.
   **Follow-up, same day -- founder: "fill the gap in the designer can't write scripts."** The
   missing NOCK UI page now exists: `DoorScripts`/`DoorScriptRow` (IDUNA's `frontend/nock/src/
   App.tsx`), the same real blank-slate-textarea-plus-Create-button pattern the texture/animation
   tools already use, with a one-click copy of the row's own real, public `script_url` ready to
   paste straight into a level's `doors` array. A map designer can now go from "write PARENA" to
   "door works in-game" without touching a terminal at all. **What still doesn't exist, named
   honestly**: door state on the wire protocol (a reconnecting client re-derives nothing about
   door state today), and any client-side visual door movement/animation (server-side collision
   only -- a player currently sees no visual change when a door opens, only that they can now walk through
   where a wall used to block them).
   **Follow-up, 2026-09-17 -- founder real-time: "how do i put doors in my levels?"** The last
   real gap in the authoring loop is closed: a level author could write a PARENA door script
   (DoorScripts page) and compile it, but had no way to actually ATTACH one to a placed wall --
   `ShankpitLevel` (both the IDUNA DB row and the frontend type) had no `doors` concept at all,
   checked directly, not assumed. New `IDUNA/internal/shankpit.Door` (`wall_id` + `script_id`,
   `doors_json` column, migration `202609170001`), a real `WallInspector` UI control in
   `ShankpitLevelEditor.tsx` (a "Door script" dropdown on the selected cube's own inspector panel
   -- a wall IS a door exactly when it has a script attached, no separate toggle to fall out of
   sync), and a real `doorsForExport` resolver: a door's `wall_id` maps to that wall's actual
   0-based POSITION in the exported `walls[]` array (not the wall's own persisted id, and not the
   door's own id -- `level_boxes.h`'s parser indexes directly into the boxes it just parsed), and
   `script_id` resolves to the real absolute `nock-door-scripts` download URL SHANKPIT's own
   `story_doors.h` already fetches. A door referencing a deleted wall is silently skipped at
   export (never crashes), matching this whole system's existing "an author's own stale
   reference mustn't break the level" discipline. **Real, deliberate scope limit, matching
   `flattenObjects`' own established "only the root level's own X reaches the native client"
   precedent for ground planes**: a door can only be attached to one of a level's own ROOT
   walls, never a wall contributed by a nested composed object -- attaching a door to an object's
   own interior wall is real, separate, not-yet-scoped follow-up.
3. **Phase 2**: ladder, screen, character, and trigger kinds (these are "just objects with
   different tick/event contracts," not a new system) -- trigger's `TriggerAction` vocabulary
   can start as just `AdvanceStory`/`NoOp` and grow `SpawnEnemies`/`PlaySound`/
   `SpawnCharacterEvent` as real content needs them, rather than building the full vocabulary
   speculatively.
   **`character` kind — DONE, 2026-09-17.** Founder real-time: "continue filling in the gaps in
   our level editor scriptable env characters etc." A level author places a specific `AIRole` +
   position (`internal/shankpit.Character`, new "Characters" panel in
   `ShankpitLevelEditor.tsx`); `server_apply_custom_level` spawns it for real
   (`story_ai_spawn_enemy`) when that level loads in `MODE_STORY`/`MODE_STORY_CAVE`, gated so it
   can never fire during a live `MODE_QUEUE` round (real safety analysis in the shipping commit,
   not assumed). Depended on two real prerequisites this session found and closed first:
   `story_ai_tick` wasn't reachable on the dedicated server AT ALL before this session (it was
   local-single-player-only, `apps/lobby/src/main.c`'s own `local_update`) and the only general,
   arbitrary-model animation path (`gband_skel_npc`) was a single frozen demo with no AI
   connection — both real, found gaps, both closed (see `EMILY/BACKLOG.md` S466/S467). Live-
   verified end to end with a hand-authored test level. `ladder`/`screen`/`trigger` remain real,
   not-yet-built follow-up — `character` was picked first because it was the one the founder
   named directly, twice.
4. **Phase 3**: the story engine itself -- `next-chapter` evaluation wired to a trigger's
   `AdvanceStory` action, the batteries-included ordered-list NOCK UI, then the advanced
   PARENA-scripted path.

## Explicitly NOT part of this system: real physics objects

Founder real-time: "object physics interractable like half life 2 pick up a cinder block put it
on the other side of a teeter totter to get it to stay like that so you can jump on the other
side to get up etc movable objects have mass and put forces onto other objects via the mass" ->
"we need all of it." This is real, wanted, and deliberately named here as OUT of scope for this
document rather than folded in: everything above (doors/ladders/screens/characters/triggers) is
scripted DECISION logic -- a PARENA function returning a new discrete state or a tagged action,
evaluated against simple scalar inputs. A HL2-style physics object (mass, momentum, a teeter-
totter pivot that responds to weight, forces propagating between objects) is real rigid-body
SIMULATION -- continuous math over positions/velocities/torques/contacts, checked directly:
`packages/common/physics.h`'s own real collision model is AABB-vs-point box collision for
players only (`resolve_collision`'s own real per-box overlap test), with no mass, no rigid body
concept, and no object-on-object force transfer anywhere in this codebase. Extending it to real
movable, massive, forceful objects is a genuinely different, large engine addition -- not a new
`LevelInteractable` kind, not PARENA-scriptable in the same sense (a rigid body solver runs every
tick over continuous state, it doesn't return a discrete enum). Real, honest status: wanted,
acknowledged, not scoped -- deserves its own NORTHSTAR pass grounded in what a real, minimal
rigid-body/constraint solver for this specific game would need, not bolted onto this doc's own
object-kind model as a sixth kind.

## What this does not cover (explicitly deferred)

- True Half-Life-style seamless/streaming level transitions. Not needed: SHANKPIT's custom
  levels are lightweight box geometry with no meaningful load time to hide, so a same-process
  level reload with player state (inventory, `history` vector) carried over in memory is the
  real, sufficient v0 -- a design simplification named here explicitly, not silently assumed.
- Runtime hot-reload of a `.so` script without a server restart (the `dlopen` PoC in
  `apps/dynmod_poc` points at this being possible later; Phase 1 can ship with a
  build-and-relink step, same as PAPERCRAFT's own committed-generated-`.c` convention, and move
  to live `dlopen` if iteration speed demands it).
- Save/checkpoint semantics beyond the in-memory `history` vector (persisting a player's story
  progress across a real disconnect/reconnect) -- real, likely-needed follow-on work, not scoped
  here.
- Any non-C PARENA target for these scripts (Java, TS) -- the C target's speed and the existing
  PAPERCRAFT/`dynmod_poc` precedent make it the only real candidate; not revisited unless a
  concrete need for one of the other targets shows up.
