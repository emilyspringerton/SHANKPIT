# SHANKPIT Humanness NORTHSTAR

Founder real-time: "I want to build realistic human like ais for games like the soldiers
fighting Gordon in half life or like city dwellers in gta3 or like NPCs in Skyrim that travel I
want realistic scriptable humans use mishri Minecraft bot humanness features to help guide
development of the framework."

This doc scopes a real, grounded "humanness layer" for SHANKPIT's NPCs, modeled directly on
MISHRI's own real, working implementation (`MISHRI/src/humanness/HumannessLayer.ts` and its
sibling managers) — not a generic essay on human-like AI. Research pass done first, not
assumed: read MISHRI's actual jitter/mood code and SHANKPIT's own two existing NPC systems
before writing anything below.

## The real precedent: MISHRI's HumannessLayer

MISHRI (a mineflayer Minecraft bot, `MISHRI/src/humanness/HumannessLayer.ts`, 499 lines) doesn't
build "human-like AI" as one big system — it's a **cross-cutting layer every other manager routes
through**, not a replacement state machine:

- **Timing noise**: `delay(min, max)` is the core primitive — `min + random*(max-min)`, scaled up
  by accumulated fatigue and low energy. `reactionDelay()` picks a real base window (200-1200ms),
  halved when `startled`, 1.5-2x when `tired`.
- **Movement overshoot**: `smoothTurn()` eases in/out via a cubic bezier, then ~30% of the time
  genuinely overshoots the target angle before settling back — overshoot-then-correct, not flat
  noise.
- **Imperfect aim**: real Gaussian (Box-Muller) noise added to yaw/pitch, scaled by a skill
  parameter plus fatigue/startled multipliers.
- **Perception**: no FOV-cone math at all (interesting real, found-live gap in MISHRI itself) —
  instead a randomized look-at-entity/sky/block/idle-drift loop plus reactive flinches and a 40%
  chance to even notice a nearby event. Genuinely different in shape from a vision-cone model.
- **Mood**: an enum (neutral/curious/tired/bored/social/focused/startled/nervous) plus continuous
  energy/curiosity/socialEnergy/boredom/fatigue, re-rolled on independent real timers, feeding
  back into every primitive above (mood modulates reaction speed, turn speed, aim jitter
  magnitude).
- **Behavior selection**: real utility AI (`BehaviorOrchestrator`) — N candidate behaviors each
  scored by a heuristic, `±30%` random noise added, a repeat-penalty applied, highest score wins,
  re-decided every few seconds. Mood feeds the scoring (curiosity→wander/explore, boredom→idle).
- **Real, behavioral-contract tests** (`MISHRI/tests/humanness.test.ts`, run via `bazel test
  //:test`): not smoke tests — real bounds assertions (`delay()` stays in its window,
  `imperfectAim()` stays within ±π, `maybeTypo()` never destroys the underlying word over 50
  trials, `chance(0.5)` lands 20-80/100). This is the real bar SHANKPIT's own version should
  match, not "doesn't crash."

## SHANKPIT's own current state, checked directly

Two entirely separate bot systems exist today, both plain C, neither sharing any jitter/mood
layer:

- **`packages/simulation/story_ai.c`** — the real Half-Life-soldier-shaped combat AI for story
  mode. Already has a real `AIMode` state machine (Patrol/Investigate/Combat/Search/Flee/
  AllyFollow/Scripted) and real, per-role tuned stats (`story_ai.h`'s `AIController`:
  `vision_range`, `vision_fov_deg`, `aim_error_deg`, `courage`, `aggression`, `next_attack_ms`
  cooldowns). `ai_gather_perception()` already does real FOV-cone + hearing-radius perception
  with `last_seen_ms`/`last_known_x,y,z` tracking — genuinely already has imperfect/delayed-
  target-acquisition semantics, just built around fixed per-role numbers, not MISHRI's
  randomized/mood-modulated timing.
- **`packages/simulation/local_game.h`'s `bot_think`** (deathmatch/CTF bots) — nearest-target
  heuristic, proportional yaw turn, tunable `brain.w_*` weights, no timing jitter, no mood.
- **`character-tick`** (`docs/STORY_SYSTEM_NORTHSTAR.md`'s own Characters section, this session)
  — ambient/idle NPCs (Idle/LookAround/CheckWatch), PARENA-scripted, GOLDENBAND-clip-backed. No
  combat, no perception, no mood. Closer to GTA3 pedestrians than HL soldiers.

**Neither existing system has continuous aim noise, mood/energy state, or overshoot-and-settle
movement.** MISHRI's mechanisms are a genuinely missing layer, not a duplicate of anything
SHANKPIT already has.

## The real design: a shared humanness layer, not a third AI system

Same real architectural shape MISHRI itself uses: `story_ai.c`'s combat FSM and `character-tick`'s
idle FSM each call INTO a shared, small humanness module for the primitives they're each
currently missing, rather than being replaced by a new, competing system. `bot_ai.h`'s deathmatch
bots stay out of scope here (matched-skill competitive combat has the opposite design goal from
"look human" — see "What this deliberately does not cover" below).

**New module: `packages/simulation/humanness.h`/`.c`** (real C, matching this repo's own
established `simulation/*.c` convention, not a PARENA layer — see "PARENA or plain C?" below):

```c
typedef struct {
    float energy, fatigue, curiosity, boredom;         // 0.0-1.0, MISHRI's own real fields
    NPCMood mood;                                       // enum, MISHRI's own real 8-state set
    unsigned int mood_change_at_ms;                     // next real mood re-roll, MISHRI's own
                                                          // "independent timers per field" pattern
} HumannessState;

unsigned int humanness_reaction_delay_ms(const HumannessState *s, unsigned int base_ms);
float humanness_aim_noise(const HumannessState *s, float skill_0_to_1);   // real Box-Muller Gaussian
void  humanness_smooth_turn_step(float *cur_yaw, float target_yaw, float dt, int *overshooting);
void  humanness_tick_mood(HumannessState *s, unsigned int now_ms);         // real timer-driven re-roll
```

- **`story_ai.c` integration**: `next_attack_ms` cooldowns become `humanness_reaction_delay_ms`
  calls (mood-modulated instead of a fixed per-role constant); `aim_error_deg` gets a real,
  continuous per-shot `humanness_aim_noise` term added on top of the existing fixed value (fixed
  = skill floor, noise = human inconsistency); turning to face a target routes through
  `humanness_smooth_turn_step` for real overshoot instead of a direct angle snap.
- **`character-tick` integration**: idle-state transition timers (how long an NPC lingers in
  LookAround before returning to Idle) become `humanness_reaction_delay_ms`-shaped instead of a
  single fixed duration; a `HumannessState` per character feeds real mood into which idle
  sub-state gets picked next (MISHRI's own utility-AI shape, scaled down to SHANKPIT's own
  smaller idle-state set).

## PARENA or plain C?

Plain C for the primitives themselves (same reasoning `gband.c`'s own "tiny, deterministic,
no engine dependency" sampler already established, and MISHRI's own `HumannessLayer` is itself
plain TypeScript utility code, not scripted) — these run every tick for every NPC and need to be
fast, not author-editable per-instance. What SHOULD be PARENA-scriptable, matching this session's
own established "data the designer tunes, not the hot-path math" split: **per-role personality
config** — the `HumannessState` starting values and mood-transition weights for a given NPC
archetype (a nervous shopkeeper vs. a battle-hardened soldier), the same real role-based split
`story_ai.h`'s own `ai_assign_role_defaults` already uses for `vision_range`/`aggression`/etc.
Real, deferred future work, not built in this pass — named here so the eventual PARENA contract
(something like `(defn npc-personality [] : PersonalityConfig ...)`) has a clear, real home to
land in later, matching the same "narrow function contract per kind" discipline `door-tick`/
`character-tick` already established.

## What this deliberately does not cover

- **Chat/typo simulation** (a real, significant chunk of MISHRI's own humanness work) — SHANKPIT
  has no in-game chat system at all, checked directly (`packages/common/protocol.h`/
  `apps/server/src/main.c`, no chat packet type exists). Not deferred, genuinely not applicable
  until/unless SHANKPIT ever gets chat.
- **`bot_ai.h`'s deathmatch/CTF bots** — deliberately excluded, not an oversight: those bots exist
  to be *good, fair opponents* in competitive matches (the same real, existing SHANKPIT-RL
  training pipeline this session already worked on extensively). "Looking human" and "playing at
  a calibrated, fair skill level for ranked competitive play" are different, sometimes opposing
  design goals — this doc is about story-mode/ambient NPCs (soldiers, pedestrians, travelers),
  not competitive bots.
- **MISHRI's own FOV-less perception model** — story_ai.c's existing real FOV-cone perception is
  already more sophisticated than MISHRI's own randomized-look-loop approach; this doc keeps
  `ai_gather_perception()`'s real cone/hearing math as-is and only adds timing/mood modulation on
  top, rather than replacing it with MISHRI's simpler pattern.
- **Skyrim-style routine/schedule NPCs** (the founder's own third reference point — "NPCs in
  Skyrim that travel") — real, related, but a separate concern from moment-to-moment humanness:
  routines are about WHERE an NPC goes over the course of a day, humanness is about HOW they move/
  react/decide once there. Genuinely connects to the Story System's own multi-entrance/exit level-
  chaining work (`STORY_SYSTEM_NORTHSTAR.md` Part 3) for "NPC walks from level A to level B on a
  schedule," but not scoped or built here.

## Real, phased build order

1. **Phase 1 -- DONE, 2026-09-17.** `packages/simulation/humanness.h`/`.c`: the four real
   primitives (`humanness_reaction_delay_ms`, `humanness_aim_noise`, `humanness_smooth_turn_step`,
   `humanness_tick_mood`/`humanness_get_startled`), plus `humanness_state_init`. Real Box-Muller
   Gaussian for aim noise (matching MISHRI's own `addNoise()` exactly, not an approximation);
   mood re-roll weighted toward NEUTRAL (MISHRI's own real weighting), rescaled from MISHRI's own
   5-20 *minute* window to a real 5-20 *second* one (SHANKPIT's own NPC encounters are short,
   moment-to-moment, not hours-long Minecraft sessions -- a deliberate unit-scaling choice, named
   in the header, not a typo). `packages/simulation/humanness_test.c`: 7 real, MISHRI-bar
   behavioral-contract assertions, not smoke tests -- reaction delay stays bounded over 200
   trials; STARTLED mood is verifiably, statistically faster than TIRED (249ms vs 866ms mean over
   500 trials); aim noise is exactly zero at perfect skill and a real, genuine zero-mean spread
   (variance 16.5) at zero skill; smooth-turn genuinely overshoots at least once AND always
   converges across 100 independent trials; mood reroll timing and the startled override are both
   exercised directly. `gcc -Wall -Wextra` clean, no warnings. No integration into `story_ai.c`/
   `character-tick` yet (Phase 2/3, below) -- this module doesn't get called by anything real
   yet, same honest "primitives proven in isolation first" ordering `gband.c`'s own sampler and
   `gseq.c`'s own sequencer both already used in this monorepo.
2. **Phase 2**: wire into `story_ai.c` — reaction delay, aim noise, and turn-overshoot on the
   combat FSM's existing hook points, live-verified against a real running server + connected
   client (same discipline every other Story System phase this session already used).
3. **Phase 3**: wire into `character-tick` — mood-driven idle-substate timing/selection.
4. **Phase 4**: PARENA-scriptable per-role personality config (deferred, see above).
