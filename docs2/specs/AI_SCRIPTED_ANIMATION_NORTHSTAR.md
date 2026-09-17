# AI Scripted Animation Composition — NORTHSTAR

Founder real-time direction, 2026-09-17, direct follow-up to the auto-pilot AI design (routed via
`emily observe`, curated into `EMILY/BACKLOG.md` S461-04): asked how to compose animations, then
gave a Half-Life `scripted_sequence` breakdown — single-character movement-hook-then-locked-
animation chaining with `On End Sequence` handoff, multi-character frame synchronization (matched
entity names, early arrivals wait, simultaneous ignition), and realism layering (neck/torso
bone-controller look-at, phoneme/.wav-driven mouth-controller lip sync).

## Real audit done first — what's already there, what's actually connected to what

This is the largest of S461's four phases and spans two repos (SHANKPIT's AI/gameplay layer,
GOLDENBAND's animation layer), so it gets its own NORTHSTAR rather than a BACKLOG paragraph, same
convention `AI_WAYPOINT_NAV_NORTHSTAR.md` (S461-01) already set.

- **GSeq already provides real sequence-stitching with crossfade**
  (`packages/goldenband/gseq.c`/`.h`, S144-XX, founder's own prior real-time ask: "make sure we
  can stitch animations together like James Bond walk turn raise gun shoot"). A `GSeq` is an
  ordered list of clips with nlerp crossfade between steps, loop-or-hold at the end. This already
  covers most of the mechanical shape of "chain clip A into clip B" the founder's `scripted_
  sequence` breakdown describes — it just isn't wired to anything AI-driven yet (see below).
- **`gpose.c` already does real N-joint FK + skinning** from arbitrary `pose_rot`/`pose_trans`
  arrays — a bone-controller look-at override (rotate one joint's quaternion before calling
  `gpose_compute_skin_matrices`) is a small, self-contained addition on top of an already-real
  API, not a new animation system.
- **The real, load-bearing gap, checked directly rather than assumed**: `gband_skel_npc.c`
  (S459-97) — the module that actually calls `gpose`/plays a clip on an NPC — is a single,
  hardcoded, decorative proof-of-concept. `apps/lobby/src/main.c`'s only call site
  (`gband_skel_npc_draw(0, 4.0f, 0.0f, 4.0f, ...)`, scene-gated to `SCENE_VOXWORLD`) draws exactly
  one mannequin at a fixed position, always looping its one imported clip. **It has zero
  connection to any `story_ai.c`-controlled NPC.** Story-mode bots are drawn through a completely
  different, already-existing path: the same `draw_player_3rd`/`gband_mesh_rig.c` 5-joint
  `tyler_body` rig every human player and every other bot already uses (confirmed by reading the
  lobby's own per-frame render loop — bots and players are the exact same `PlayerState` array,
  drawn by the exact same call). So "how do we compose animations for the auto-pilot AI" has a
  real, working, if limited, baseline already (the `tyler_body` rig's hand-authored channels), and
  a real, more general, not-yet-connected alternative (`gpose`/`gseq`/`gband_skel_npc`) sitting
  next to it.
- **`AI_MODE_SCRIPTED` was a dead enum value** — defined in `story_ai.h`, referenced nowhere in
  `story_ai.c` before this pass, same class of gap `AI_MODE_FLEE` was before S461-01.

## What's built (S461-04, this pass)

Server-authoritative logic only, in `story_ai.c` — the AI/timing layer of the `scripted_sequence`
pattern, not the rendering layer:

- `story_ai_trigger_scripted(player_id, x, y, z, hold_ms, now_ms)` — a real public API: sends the
  named AI into `AI_MODE_SCRIPTED`, remembering the marker and hold duration.
- `ai_run_scripted` — walks to the marker with real arrival steering (S461-02's own
  `ai_move_towards` slow-radius mechanism), then holds position for `hold_ms` once within 4 units,
  matching the founder's own two-stage description ("intercepted the character's dynamic AI and
  commanded them to move toward that exact marker" then "forced the NPC into a locked 'scripted
  state'").
- **A real locked state**: the per-tick mode-decision loop now skips `AI_MODE_SCRIPTED` AIs
  entirely — combat/investigate/flee perception cannot interrupt a scripted sequence, matching
  the founder's own "locked scripted state" framing exactly, not an approximation.
- **On End Sequence handoff**: once the hold expires, `ai_set_mode` hands back to whatever mode
  preceded the trigger (`previous_mode`, a field that already existed and is set automatically by
  every mode transition) — falls back to `AI_MODE_PATROL` in the one degenerate case
  (`previous_mode` itself being `AI_MODE_SCRIPTED`, e.g. a second trigger landing before the first
  resolved).

`make server` and `make lobby` both build clean.

## Engine-layer primitives shipped since this doc was written (2026-09-17, GOLDENBAND)

Apple #20015, direct founder follow-up: "continue scripted-sequence animation work in GOLDENBAND
— multi-actor frame synchronization... plus procedural bone-controller look-at and phoneme
lip-sync layering on top of gseq/gpose." Both real, buildable pieces named below shipped as
tested, engine-agnostic GOLDENBAND library code (source: `GOLDENBAND/src/`, vendored here per the
usual convention) — **neither is wired to any SHANKPIT gameplay consumer yet**, since (per the
finding below) nothing in SHANKPIT actually drives `gband_skel_npc`/`gseq` from gameplay state at
all today. They exist and are tested in isolation, ready for that wiring once it happens.

- **Bone-controller look-at — `gpose_look_at`** (`packages/goldenband/gpose.c`/`.h`). Rotates a
  caller-specified local "forward" axis on one joint to face a world-space target, nlerp-clamped
  to a max turn angle (the founder's own "up to 30 degrees" example) so it never breaks a locked
  pose outright. Built on a newly-exposed `gpose_compute_joint_world` (the FK loop
  `gpose_compute_skin_matrices` already ran internally, now a real, separate, reusable primitive).
  5 real tests (unclamped/clamped/degenerate cases), all passing.
- **Multi-actor frame synchronization — `gsync.c`/`.h`** (new module, `GSYNC_MAX_MEMBERS`-member
  named barrier). Members report arrival independently (pathfinding speeds vary, matching the
  founder's own framing exactly); `gsync_check_ignition` fires exactly once, the real tick every
  member has arrived, so a caller resets every one of its own `GSeqPlayer`s to `elapsed=0`
  simultaneously — real "Wait state, then simultaneous ignition," without this module ever
  touching an animation type itself (same engine-agnostic discipline every other GOLDENBAND
  module already holds itself to). 5 real tests (ignition timing, idempotent re-arrival, early
  arrivals genuinely waiting, re-arming by name, degenerate member-count rejection).

## What's real, honestly not built yet

- **Client-side clip selection during the hold.** `AI_MODE_SCRIPTED` is server-authoritative
  timing/positioning only right now — nothing on the client currently reads an AI's mode to pick
  a specific animation clip for the hold. A stationary bot in `AI_MODE_SCRIPTED` today renders
  exactly like a stationary bot in any other mode (whatever `draw_player_3rd`'s existing idle pose
  already is). Wiring "this AI is in SCRIPTED, play clip X" needs either a new wire field (mode is
  already broadcast implicitly via bot behavior, but not explicitly as an enum the client can
  switch on) or inferring it from position/velocity — real, undecided design work. `gsync`'s own
  ignition signal is ready to consume once this exists; it doesn't help until something calls it.
- **`gpose_look_at`/`gsync` are still not called anywhere in SHANKPIT.** Real, tested library
  functions, zero call sites. But the actual prerequisite this bullet originally named —
  "nothing connects `story_ai.c`'s NPCs to `gband_skel_npc`'s rendering" — is now **closed**
  (2026-09-17, founder real-time: "can we animate and model end to end?"): `story_ai`-controlled
  NPCs render via `gband_skel_npc_draw` today (new `SKIN_MANNEQUIN`, forced onto `is_bot` players
  in `MODE_STORY`/`MODE_STORY_CAVE` via `draw_player_3rd`'s existing `forced_skin` mechanism —
  see `EMILY/BACKLOG.md`'s own entry for the full commit). `gband_skel_npc` also gained real
  idle/walk clip switching in the same pass (previously a single frozen clip). What's real,
  honestly still missing: `gpose_look_at`/`gsync` themselves are STILL not wired into this new
  connection — the render path exists now, but nothing calls look-at for a neck/torso bone or
  drives multi-actor sync through it yet. That's the real, now-much-smaller remaining gap.
- **Phoneme/audio-driven mouth-controller lip sync.** Checked directly before attempting anything:
  `packages/audio/audio.c` synthesizes every sound as a PCM wavetable at init — there is no WAV/
  external-audio-file loader anywhere in this codebase, so even the founder's own simpler framing
  ("jiggled the mouth mesh in sync with the volume frequency of the .wav voice file" — real
  amplitude-envelope driving, not full phoneme classification) has no real voice-line asset to
  sample from yet. The actual prerequisite is a WAV/dialogue-asset pipeline, not a smarter
  analysis algorithm. Out of scope for this pass; not even design-sketched beyond this finding.
- ~~Connecting `gband_skel_npc`/`gseq` to actual gameplay bots~~ — **done, 2026-09-17** (see
  above). The real architectural fork this bullet named IS now resolved, decisively: story_ai
  NPCs render via the general `gpose`/`gband_skel_npc` pipeline (`SKIN_MANNEQUIN`), not
  `tyler_body`.
- ~~Only ONE character look exists for NPCs today~~ — **closed, 2026-09-17** (founder real-time:
  "i just added universal animation library 1..." then "GEORGE LEELA MIKE AND STAN ARE ANIMATED
  ROBOT CHARACTERS WITH MESH RIG AND ANIMATIONS PER BOT"). `gband_skel_npc`'s own "only one
  character asset loaded at a time" v0 scope is lifted: it now supports multiple simultaneous
  kits (`GbandSkelNpcKit` array, `kit_index`-parameterized load/draw — see GOLDENBAND commit
  304a08d, vendored into SHANKPIT). Five real kits load at startup: mannequin (now with genuine
  `UAL1_Standard` idle/walk clips, closing the earlier "same clip for both" caveat too), Stan,
  Mike, Leela, George — each its own real mesh+skeleton+animation set (43/43/17/47 joints
  respectively), exported and validated from IDUNA's `nock_animations` table.
  `draw_player_skin_mannequin` selects a real, ready kit deterministically from `p->id`. Real,
  honestly still missing: this is NOT role-aware yet — `AIRole` isn't networked to the client at
  all (`PlayerState` carries no role field; only `story_ai.c`'s server-side `AIController` array
  knows it), so `AI_ROLE_GORE_BRUTE` vs `AI_ROLE_STORM_CALLER` don't yet map to specific looks.
  That's the real, now-narrower remaining gap — needs a new wire field, not a rendering change.

## Related

- `EMILY/BACKLOG.md` S461 — the full four-phase plan (arrival steering, tactical pathing, squad
  leader, this doc's phase all now have real, shipped v0s; the four "not built yet" items above
  are the real remaining surface of S461-04 specifically).
- `docs2/specs/AI_WAYPOINT_NAV_NORTHSTAR.md` — S461-01, the sibling doc for tactical pathing.
- `packages/goldenband/gseq.c`/`.h`, `gpose.c`/`.h`, `gband_skel_npc.c`/`.h` — the animation-layer
  code this doc audits.
- `packages/simulation/story_ai.c`/`.h` — `AI_MODE_SCRIPTED`, `story_ai_trigger_scripted`,
  `ai_run_scripted`.
