# SHANKPIT Story System NORTHSTAR

Founder real-time, across several turns of real design conversation: "how can we make maps and
objects scriptable ideally with parena [...] doors, ladders, in game computer screens" ->
"via the nock tools" -> "I want to [...] make a level loading system like half life used [...]
we can have start and end markers for the level and then we can have stories that stitch the
levels together. we can even use the same levels to create 2 stories that vary vastly just due
to the order of the chapters (novel called crossings does this)" -> "we actually need multiple
entrances or exits [...] almost a tree structure but not totally think about time loops and
arbitrary story definition if exit b go here if exit a go here if 2 decisions ago you said a
then next time we go c etc parena scriptable."

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
2. **Scriptable map objects** (doors/ladders/screens, and -- load-bearing for the story system --
   entrance/exit markers). A level's `LevelBox` array (`packages/world/level_boxes.h`) today
   carries only geometry + color + material name + an unused `friction` field -- no
   trigger/interact/script concept exists anywhere in SHANKPIT. An "exit marker" IS an
   interactable object (a trigger volume that runs a script when a player enters it), so this
   has to exist before the story system has anything to hook into.
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

## Part 2: scriptable map objects (doors, ladders, screens, markers)

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
`kind` (door | ladder | screen | exit_marker | entrance_marker, extensible), and a
`script_asset_id` referencing a real, compiled behavior. A **narrow, fixed function contract per
kind** (same discipline `internal/nock/procgen.go`'s `validateProcTextureSource` already
enforces for texture scripts -- reject anything outside the contract, don't trust the emitter
alone): e.g.

```
(defn door-tick [(dist-to-nearest-player : F64) (input-held : Bool) (door-state : DoorState)] : DoorState ...)
(defn ladder-tick [(player-vertical-input : F64) (on-ladder : Bool)] : F64 ...)   ; returns a vertical velocity override
(defn screen-render [(state : ScreenState)] : ScreenContent ...)                  ; what text/image the screen shows
```

Ladders and doors are real per-tick state machines the server evaluates every tick for every
placed instance (same tick-loop shape bot AI already uses in `apps/server/src/main.c`'s main
loop -- a new "map object update" pass slots in right after the per-player physics pass, before
snapshot broadcast, per this session's own earlier finding). Screens are lower-frequency
(re-evaluated on interaction, not every tick).

**Target/trust question, resolved**: `procgen.go`'s own texture pipeline deliberately avoided
PARENA's C target for *LLM-generated* source specifically because an unrestricted `#target`/
inline-C escape hatch is a real, unsandboxed code-exec vector when the actual code author is a
model, not a human. Map-object scripts are hand-authored by a trusted map designer through NOCK
(same `iduna.admin`-gated surface every other NOCK authoring tool already requires) -- the same
trust level as any other server-side code an admin ships. The C target is the right choice here,
not a compromise: it's what PAPERCRAFT's own precedent already uses, and it's what makes the
per-tick door/ladder evaluation cheap enough to run for every instance, every tick, in a 60Hz
server loop (no JVM round trip the way the texture pipeline's Java target would require).

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

**Where it runs**: `next-chapter` is evaluated server-side, once, at the moment a player crosses
an exit marker (Part 2's `exit_marker` kind) -- not a per-tick cost. Same PARENA C-target /
`level_mod.prn`-style linking as Part 2's objects; a story's compiled behavior is really just
another script asset the object system already has a place for (an exit marker's
`script_asset_id` points at a `next-chapter` implementation instead of a `door-tick` one).

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

1. **Phase 0**: per-level scene identity (Part 1). Blocking, small, mechanical.
2. **Phase 1**: the object system's real plumbing -- `LevelInteractable` format, the server-side
   compile pipeline (PARENA -> C -> shared object, mirroring `procgen.go`'s
   PARENA->Java->`javac` pipeline but targeting C + `dlopen`), and ONE real object kind
   (recommend: door, the simplest state machine) end to end, NOCK-authored.
3. **Phase 2**: ladder and screen kinds, plus entrance/exit marker kinds specifically (these are
   "just doors with different tick contracts," not a new system).
4. **Phase 3**: the story engine itself -- `next-chapter` evaluation wired to exit-marker
   crossing, the batteries-included ordered-list NOCK UI, then the advanced PARENA-scripted path.

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
