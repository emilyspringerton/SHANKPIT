# Ragdoll Orientation Physics — NORTHSTAR

Registered as `SHANKPIT-RAGDOLL-ORIENT-NORTH`. Scoping doc for `EMILY/BACKLOG.md` SECTION 496.

## Founder real-time direction, in order

"can we add ragdoll and rigid body physics including stuff with mass?" → (S484/S494/S495: three
real spike iterations, findings below) → "so you are telling me that we need to use machine
learning via the goldenband format to teach these ragdolls how to walk again?" → "yes i think that
if we get that right it would be huge work towards this 1 first obvi and then 2" → "we may as well
see how far we can push the engine while we are building it it impacts the kinds of stories we can
tell" → "and then what does it look like to take it all the way to robotics? do we have to build
cad in? do we need to model actual real world component capabilities?" → "but if the character
falls over theres no way its going to look human like when they get up its going to be horiffic
and synthetic probably i dunno id like to see where this takes us."

## What the spikes already proved (S484/S494/S495, real, live-verified, not assumed)

Three real iterations of `tools/ragdoll_spike/main.c` (throwaway, not wired into gameplay), each
run against the real `mannequin_npc.gskel` (65 joints — the same skeleton already used for real
multiplayer players via `SKIN_MANNEQUIN`):

1. **Verlet/PBD point-mass + distance constraints is numerically stable** on this engine's real
   skeleton data at its real 64Hz tick rate. Found and fixed two real bugs (a ground-clamp
   velocity spike, a distance-constraint sign error) before this was even true.
2. **A real per-bone mass model** (proportional to each joint's own parent-bone length) and a
   **real bend constraint** (one-sided minimum-distance to grandparent, preventing
   hyperextension/fold-back) were added and directly verified working — 0/63 constraint
   violations at the final settled state.
3. **Real ground friction** (killing horizontal implied-velocity on contact) measurably tightens
   the resting spread (fingertip spread dropped from ~0.6-0.7 units to ~0.3-0.5) — a real, if
   partial, improvement, not dead weight.
4. **The real, conclusive ceiling**: none of the above — individually or combined — can ever
   produce an anatomically plausible resting pose, because distance/bend/friction constraints
   have no concept of orientation at all, only relative distances between points. A fully
   extended, symmetric "starfish" satisfies every one of them equally as well as a naturally
   crumpled pose would.

## Reframing the actual goal (a real correction made mid-session)

Lying roughly flat/horizontal after falling is **not the bug** — that's genuinely what happens to
an unconscious body, and is what every real game's ragdoll (Half-Life 2, GTA, etc.) actually
produces. The real, specific problem is that the CURRENT spike produces a symmetric,
poker-straight, radially-splayed shape — not that it ends up on the ground. The real goal is: **an
anatomically plausible crumpled pose** (knees/elbows staying naturally bent, limbs never swinging
into anatomically-impossible planes) while still correctly settling onto the ground under gravity.

## Real technical design — v0, no new asset format needed

Full 6DOF rigid-body dynamics (each bone as an independent rigid body with position + orientation
+ angular momentum, connected via ball-socket + swing/twist limit constraints — the real, correct,
general solution, e.g. Müller et al.'s XPBD) is the honest destination, but v0 can get real,
visible value without it, and without touching `.gskel`'s own format at all:

- **Swing-axis constraint, derived from rest-pose geometry.** For a joint J with parent P and
  grandparent G, the rest pose's own `(P→G) × (P→J)` cross product gives a real, joint-specific
  "natural bending plane normal" — no new metadata needed, this is pure geometry already present
  in every existing `.gskel` asset. At runtime, penalize/correct any component of J's current
  position that pulls it OUT of the plane containing P, G, and that rest-pose normal — i.e. resist
  a knee swinging sideways or a leg twisting into a direction it can't anatomically reach, using
  ONLY the point-mass data the spike already has.
- **What this cannot do**: constrain TWIST (rotation around a bone's own long axis) — that's
  fundamentally unobservable from two end-point positions alone; genuinely needs real per-bone
  orientation state (a third reference point, or a real quaternion per bone). Named honestly as a
  v1 gap, not silently skipped.
- **What this also cannot do**: prevent the WHOLE hierarchy from collectively tipping over to lie
  flat as a rigid unit — and per the reframing above, it shouldn't try to. A swing-constrained
  ragdoll can and should still end up horizontal; the difference is it'll look like a person lying
  down, not a symmetric abstract shape.

## Phased plan

1. **Swing-axis constraints on the major joints only** (hips, knees, shoulders, elbows) — the
   joints with the biggest visual impact on "does this look like a body." Fingers/spine/toes
   deferred — real but comparatively low-impact, same triage logic `AI_SCRIPTED_ANIMATION_
   NORTHSTAR.md` already used for phasing lip sync/bone-controller work.
2. **v1: real per-bone orientation state + twist limits** — the genuinely bigger lift (quaternion
   per bone, real angular-velocity integration, real swing+twist cone limits instead of the v0
   plane-projection approximation). This is the point where a real rigid-body library
   architecture (even if hand-rolled, not third-party, matching SIM-100 §2's own determinism
   requirement) becomes the honest description of what's being built, not "PBD with extra
   constraints."
3. **Collision shapes per bone** (capsules) — named in the original S484 spike as real future
   work, still not started, still lower priority than orientation.

## Get-up / recovery is a SEPARATE problem, not solved by any of the above

Founder's own correct instinct: "theres no way its going to look human like when they get up its
going to be horiffic and synthetic." Confirmed, not disputed — a physics-only recovery (an RL
policy rewarded purely for "successfully stand up," with no reference to imitate) is a well-
documented failure mode in real RL research: grotesque, twitchy, alien movement. This is exactly
what `HQ-SPEC-SIM-100` §4's own Reward Compiler is FOR — imitation reward (pose/velocity/
end-effector tracking against a real, human-authored or mocap `.gband` reference clip), not a bare
task-success reward. "The animator's clip is the spec; physics is the implementation; the policy
is the compiled artifact" (SIM-100 §1) is the literal answer to this concern. Three real, honest
paths for recovery, genuinely different costs:
- **Authored blend** (cheap, no ML, ships now): once the ragdoll settles, snap/blend into a
  hand-authored "stand up" `.gband` clip via GOLDENBAND's own real `gseq` stitching (already
  built, already proven — "stitch animations together like James Bond walk turn raise gun shoot").
  Doesn't adapt to HOW the character fell, but looks human because it IS authored human motion.
- **Synthesized reference clip via fall-and-reverse** (S497, cheap, no ML, no mocap/animator
  needed to bootstrap it — a real third option, distinct from both of the others). Founder
  real-time: "fuck it play the death animation backwards - let the robot learn how to just pop
  back up" → corrected in the very next message once the first cut mis-scoped this as a literal
  playable animation: "my bro the rag doll doesnt need to stand up if its not rigid body it wont
  fall over - reversing the death animation was supposed to be the oracle that the RL learns
  against to be able to stand back up after falling over." The corrected, real design: record a
  ragdoll's fall (per-tick joint positions, the same point-mass spike this doc already covers),
  reverse the tick order, and write it out as a real `.gband` file — that clip becomes an
  imitation-reward TARGET for the Reward Compiler (SIM-100 §4), not a directly-played animation.
  A trained recovery policy gets REWARDED for tracking this clip's arc, the same way it would get
  rewarded for tracking a mocap or hand-authored reference — the only thing novel here is that
  the reference clip itself is synthesized for free from a physics sim instead of captured or
  hand-keyed. Real, load-bearing limitation, named honestly: this only produces a *reasonable*
  reference arc (verified end to end in `tools/ragdoll_spike/main.c`'s own write-out — the
  reversed clip runs from the fall's settled/lying pose at tick 0 up to the original standing
  rest pose at the final tick, real numbers: joint0 y goes 0.000 → 7.987 across the clip), not a
  guaranteed-plausible one — it's built from point-mass positions with no orientation state (see
  this doc's own v1 gap), so it inherits every limitation already named above (poker-straight
  limbs, no twist). Good enough as a reward-compiler TARGET (which only needs a directionally
  sane arc to shape training against), not good enough as a directly-playable animation. Also
  answers the founder's own immediate follow-up ("most importantly we need a roll over
  animation") for this specific mechanism: a separately-authored roll-over step is NOT needed
  here, because the reversed clip retraces whatever orientation the character actually fell
  into, not a fixed assumed "flat on back" starting pose — a roll-over animation would still be
  a real, separate need for any OTHER recovery path that assumes a fixed starting pose (e.g. an
  authored blend triggered without knowing the fall orientation).
- **Trained recovery policy** (the real GOLDENBAND/SIM-100 destination, not built yet — confirmed
  earlier this session only step 1 of SIM-100's own Build Sequence, the `.gband` format itself, is
  done): adapts to fall direction/state, imitation-trained against a reference clip (either an
  authored one or one synthesized per the fall-and-reverse path above), but needs the full
  reward-compiler + training-backbone pipeline (SIM-100 §8 steps 3+), which needs real,
  actuatable, orientation-and-limit-aware joints underneath it FIRST — i.e. this doc's own real
  prerequisite work, not a shortcut around it.

### Get-up oracle clip — real, verified artifact (S497)

`tools/ragdoll_spike/main.c` now records every tick of its forward fall simulation and, after the
sim completes, writes the reversed sequence out as a real `.gband` binary + manifest
(`tools/ragdoll_spike/output/getup_oracle.gband(.json)`, gitignored — generated, not source).
Verified against GOLDENBAND's own real reader, not just the writer's own claim: loaded via
`gb_init`, `gb_verify` reports a matching content hash (PASS), `duration_ticks=192`,
`num_channels=195` (65 joints × tx/ty/tz, position-only — no rotation channels exist yet, this
spike has no per-bone orientation state). This is a real, working proof that the "record the fall,
reverse it" idea produces a loadable `.gband` asset today, not just a design-doc claim — the
missing piece for a REAL trained recovery policy is everything this doc's own Phased Plan already
names (swing-axis/orientation joints, then SIM-100 §8 steps 3+ for the actual Reward Compiler and
training backbone), not the oracle-clip generation step itself.

## Robotics / CAD (the founder's own direct question, answered here for the record)

`HQ-SPEC-SIM-100` §3 already names this: a hardware-bound skeleton needs real **actuator
metadata per joint — torque limits, velocity limits, gear backlash notes** — and retargeting
"includes a feasibility pass: any frame demanding infeasible joint velocity/torque is flagged at
authoring time, not discovered on a test stand." This does NOT require full CAD inside the
simulator — it requires the real NUMBERS CAD/motor-datasheet work produces (per-link mass,
inertia, torque/velocity curves, backlash), sourced from whichever real actuators/gearboxes a
hardware team actually specs, fed in as simulation parameters the sim checks feasibility against.
The joint swing/twist LIMIT work this doc scopes is the same real category of per-joint metadata
real actuator limits would eventually need too — this is genuinely shared groundwork, not
game-only throwaway work. §6's own graduated actuation ladder (sim → hardware-in-the-loop bench →
tethered stand → untethered → staff-facing → guest-facing, human biometric approval required for
any physical deployment) is the real, already-specified gate before any of this touches an actual
motor — long-horizon, not a near-term blocker.

No timeline committed. Phase 1 (swing-axis constraints on major joints) is the real, immediate,
buildable next increment.
