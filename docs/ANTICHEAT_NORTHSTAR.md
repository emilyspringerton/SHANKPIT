# SHANKPIT Anti-Cheat NORTHSTAR

Founder real-time, working through a real research conversation on hardware-level input cheats
(ATmega32U4/Arduino-based USB HID mouse emulation, the same class of device used to drive
computer-vision aimbots that anti-cheat software can't distinguish from a real mouse at the OS
level): "ok northstar this for anticheat." This doc scopes what's real, buildable, and grounded
in SHANKPIT's own existing architecture -- not a generic anti-cheat essay.

## The real threat model

Hardware-based cheating (Arduino/ATmega32U4 "KM boxes", DMA cards) works by feeding a real,
genuine USB HID mouse signal to the OS -- the game's own input layer can't tell it apart from a
human hand, because it genuinely isn't lying about being a mouse. Software-only detection
(process scanning, kernel drivers) can't see it either, since there's no injected code to find --
the "cheat" lives entirely outside the game's own process, watching the screen (or, in more
advanced setups, reading memory) and steering a real physical-looking input device.

**What this means for SHANKPIT specifically, checked directly against this codebase, not
assumed:**

- SHANKPIT is already **fully server-authoritative** for combat -- `packages/common/physics.h`'s
  `update_weapons`/`check_hit_location` resolve every hit server-side from the player's own real,
  server-tracked `yaw`/`pitch`. A hardware cheat can't fabricate a kill or damage number; it can
  only steer *where the crosshair points*. This narrows the real attack surface to one thing:
  **unnaturally good aim**, not "cheating" in the broader sense (no wallhacks-as-data-exfiltration
  problem here the way a client-authoritative game would have, though a screen-reading ESP-style
  wallhack is a separate, real, not-yet-scoped threat -- named here, not solved).
- The server already receives raw `yaw`/`pitch` in every real `UserCmd` at
  `CLIENT_USERCMD_HZ`=60Hz (`apps/lobby/src/main.c`), processed server-side by
  `process_user_cmd` (`apps/server/src/main.c`). **The raw material for behavioral telemetry
  (angular velocity, snap detection, tracking smoothness) already flows through the server on
  every single tick.** No new wire protocol is needed for Phase 0 below.
- SHANKPIT already has a **real, working bot-pool architecture** where bots are genuine connected
  UDP clients speaking the exact same wire protocol a human does (`ops/shankpit-bot-pool.sh`,
  `apps2/emily-bot`, `scripts/frozen_policy_bot.py`) -- there is no special server-side-only "NPC"
  concept to build from scratch. A bot-only lobby is not a new kind of match, it's the *same*
  match type with the human-side matchmaking slots filled by bots instead of humans.
- S459-62 already built a real, working precedent for "make QUEUE spawn a *specific*, chosen
  opponent instead of the normal pool" (`is_active_opponent`/`GetActiveOpponent`, IDUNA's
  checkpoint registry, `ops/shankpit-bot-pool.sh`'s own active-opponent branch) -- directly
  reusable groundwork for "route *this specific, suspected* player into a match that's secretly
  all-bot."

## The real design, as the founder actually arrived at it (credit where due)

Working through the conversation, the founder converged on a real, coherent, three-layer system,
independently re-deriving a legitimate anti-cheat pattern (shadow-pooling / soft-ban-into-bots is
a real, known industry technique, e.g. Riot's own public statements on similar mechanisms) rather
than a naive "detect and instant-ban" approach:

1. **Suspicion, not proof, triggers a silent reroute.** A player whose server-side telemetry
   crosses a real, tuned threshold (see Phase 0) gets silently placed into a match where every
   other "player" is actually a bot -- they are never told, never banned, the queue UI looks
   identical.
2. **The bots in that match escalate toward physically impossible.** Not just "hard bots" --
   bots with reaction times *below the real human nerve-conduction floor* (~100-120ms for a
   visual stimulus, a real, checkable physiological constant, not a guess) and pixel-perfect
   tracking. A human, even a genuine prodigy, cannot out-track a 0ms-reaction opponent -- so
   *consistently* beating or trading evenly with these bots is itself a strong, real, physically-
   grounded signal, independent of the original suspicion trigger.
3. **The asymmetric outcome is the real safety net.** If the player is a genuine, high-skill
   human, they lose to impossible bots (as any human would), the system quietly reroutes them
   back to real matchmaking, and nothing bad ever happened to them -- worst case, one weird-
   feeling match. If they're running a hardware cheat, they either get crushed anyway (revealing
   the cheat script's own real limits) or they keep pace with a physically-impossible opponent,
   which is close to unfakeable proof. **This is the real reason the system is safe to run
   automatically without human review at the trigger stage** -- the cost of a false positive is
   bounded and small (one bad match), not a wrongful ban.

## Phase 0 -- passive telemetry (build first, ships nothing user-visible)

Real, buildable today against the exact fields already flowing through `process_user_cmd`:

- Per-tick angular delta (`yaw`/`pitch` change between consecutive `UserCmd`s from the same
  client) -- the raw signal every downstream check builds on.
- **Snap detection**: a delta magnitude/timing combination that's inconsistent with human wrist
  mechanics (near-instant, large-angle reorientation with no deceleration tail). Real reference
  point from the research conversation: human tracking has a real, characteristic deceleration
  arc; a scripted flick doesn't.
- **Consistency-under-fire scoring**: track hit-rate and reaction latency specifically in the
  window right after an enemy becomes visible in a player's own view frustum (server already
  knows both players' real positions -- this is a real, computable "time from visible to first
  shot" metric, not a proxy).
- Output: a rolling, per-player suspicion score, logged server-side. **Nothing acts on it yet in
  this phase** -- the real goal here is collecting enough real match data to know what the actual
  human baseline distribution looks like on THIS game's own real movement/TTK numbers before
  building a trigger threshold on top of guessed constants.

## Phase 1 -- silent reroute into a bot-only match

- New, real internal match type (not exposed anywhere in matchmaking UI) that fills every
  non-suspect slot with bots via the existing bot-pool mechanism (`ops/shankpit-bot-pool.sh`'s
  own real spawn path, or the frozen-policy-bot path S459-62 already wired for "active opponent")
  instead of real players.
- Real, honest open question, named not solved: SHANKPIT's current matchmaking
  (`queue_activate_match`) pools real, already-connected clients together. Silently diverting ONE
  specific client to a bot-only instance while everyone else stays in the normal pool is a real,
  non-trivial routing change to that function, not a config flag -- scoped as its own real
  subphase, not hand-waved.
- Real UX constraint: the deception only works if the match *feels* like a normal QUEUE match
  from the UI/HUD's own perspective (score, names, chat if any) -- bots need real, plausible
  fake names/behavior, not an obvious "BOT_1" tag.

## Phase 2 -- the escalating bot ladder ("rage bots")

- Tier 1: SHANKPIT's existing heuristic `emily-bot` AI, tuned hard but still human-plausible --
  a real, cheap first filter (genuinely great players might just beat this tier and get routed
  back immediately, no further scrutiny needed).
- Tier 2: `frozen_policy_bot.py`-driven PPO checkpoints (already real and live, S459-54's own
  league) selected for high measured Elo -- a real, trained-not-scripted opponent, harder to
  dismiss as "obviously a bot" if the suspect ever reviews their own replay.
- Tier 3: the real "impossible" tier -- server-side reaction time clamped to sub-human (a real,
  direct code change to whatever aim-decision latency the bot AI uses, not a new AI), perfect
  tracking. This tier's own output is the real, load-bearing evidence signal named above.
- Real, honest gap: SHANKPIT's heuristic/PPO bots currently aim using the same real movement
  physics a player does -- "impossible reaction time" needs a real, explicit latency-floor
  parameter added to whichever aim-decision code drives Tier 3, not assumed to already exist.

## Phase 3 -- outcome handling

- **Never an automatic permaban from this system alone.** The bot-match data becomes evidence
  attached to the account for human review, or a real, tunable auto-action (extended matchmaking
  delay, silent skill-pool demotion) with a bounded, reversible cost -- matching the founder's
  own "if they aren't cheating they have good scrim partners" framing: the SAME mechanism is
  real, valuable training-partner infrastructure for a legitimate top player, not purely
  punitive.
- Real, honest, deliberately-deferred question: ban-evasion via new accounts is a separate,
  larger identity/account-integrity problem (IDUNA's own domain), not something this doc solves.

## What this doc deliberately does NOT solve

- Screen-reading / computer-vision cheats that never touch the wire protocol at all (they just
  look at rendered pixels) -- server-side telemetry on `UserCmd` angles is necessary but not
  sufficient against a well-tuned CV-only cheat that adds realistic jitter. Named as a real,
  known limitation of ANY server-side-only approach, not solved here.
- Memory-reading cheats that bypass the visual pipeline entirely (ESP/wallhack-style). Different
  threat class, different mitigation (this doc is aim-cheat-focused, matching the real research
  conversation's own scope).
- The LAN/physical-inspection tier the founder also named (hardware vetting at live events) --
  real and valuable, but organizational/operational, not a code project.

## Status

NORTHSTAR only. No code written. Real, phased plan above; Phase 0 (passive telemetry) is the
correct real starting point -- it ships nothing risky, and every later phase's own trigger
threshold needs Phase 0's real collected data to be tuned against actual SHANKPIT match data
rather than guessed constants.
