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

## What's real, honestly not built yet

- **Client-side clip selection during the hold.** `AI_MODE_SCRIPTED` is server-authoritative
  timing/positioning only right now — nothing on the client currently reads an AI's mode to pick
  a specific animation clip for the hold. A stationary bot in `AI_MODE_SCRIPTED` today renders
  exactly like a stationary bot in any other mode (whatever `draw_player_3rd`'s existing idle pose
  already is). Wiring "this AI is in SCRIPTED, play clip X" needs either a new wire field (mode is
  already broadcast implicitly via bot behavior, but not explicitly as an enum the client can
  switch on) or inferring it from position/velocity — real, undecided design work.
- **Multi-actor frame synchronization** (matched-name entities, early arrivals wait, simultaneous
  ignition). Nothing here builds toward this yet — it needs a real new primitive (something like
  S461-03's squad grouping, but keyed by a shared marker/name and gated on ALL members reaching
  "arrived" before any of them starts the locked animation, not just each one independently).
  Real, separate follow-up.
- **Bone-controller look-at** (neck/torso tracking a target, layered over a locked or looping
  clip). Small and self-contained on top of `gpose.c`'s existing FK path (see above), but not
  started — needs a joint-index lookup by name (skeleton manifests already carry joint names) and
  a per-frame quaternion override before `gpose_compute_skin_matrices` runs.
- **Phoneme/audio-driven mouth-controller lip sync.** The largest, least-started piece — needs
  real waveform-to-phoneme mapping integrated with `packages/audio/audio.c`, which currently has
  no such analysis. Out of scope for this pass entirely; not even design-sketched yet.
- **Connecting `gband_skel_npc`/`gseq` to actual gameplay bots**, if that ever becomes the desired
  direction instead of extending `tyler_body`/`draw_player_3rd` — a real, undecided architectural
  fork this doc deliberately does not resolve. `story_ai_seed_voxworld_encounter` and
  `AI_WAYPOINT_NAV_NORTHSTAR.md` both already assume bots render via the existing `tyler_body`
  path; nothing here changes that assumption.

## Related

- `EMILY/BACKLOG.md` S461 — the full four-phase plan (arrival steering, tactical pathing, squad
  leader, this doc's phase all now have real, shipped v0s; the four "not built yet" items above
  are the real remaining surface of S461-04 specifically).
- `docs2/specs/AI_WAYPOINT_NAV_NORTHSTAR.md` — S461-01, the sibling doc for tactical pathing.
- `packages/goldenband/gseq.c`/`.h`, `gpose.c`/`.h`, `gband_skel_npc.c`/`.h` — the animation-layer
  code this doc audits.
- `packages/simulation/story_ai.c`/`.h` — `AI_MODE_SCRIPTED`, `story_ai_trigger_scripted`,
  `ai_run_scripted`.
