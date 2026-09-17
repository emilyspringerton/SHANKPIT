#ifndef HUMANNESS_H
#define HUMANNESS_H

// humanness.h -- Phase 1 of docs/HUMANNESS_NORTHSTAR.md (founder real-time: "I want to build
// realistic human like ais... use mishri minecraft bot humanness features to help guide
// development of the framework"). A real, small, plain-C primitives module, directly modeled on
// MISHRI's own real HumannessLayer.ts (MISHRI/src/humanness/HumannessLayer.ts) -- timing jitter
// scaled by fatigue/energy, Box-Muller Gaussian aim noise, bezier-eased overshoot-then-correct
// turning, and an 8-state mood enum feeding all three. Plain C, not PARENA, by design (see the
// NORTHSTAR doc's own "PARENA or plain C?" section): these run every tick for every NPC and need
// to be fast, not author-editable per-instance.
//
// This module is deliberately a LAYER other systems call INTO -- story_ai.c's combat FSM and
// character-tick's idle FSM (Phase 2/3, not built yet) -- never a replacement for either. Phase 1
// here is the primitives + a real, MISHRI-bar behavioral-contract test suite only, no
// integration.

#include <stdint.h>

// NPCMood -- MISHRI's own real 8-state set (HumannessLayer.ts), unchanged: the exact states
// matter less than SHANKPIT's own future callers agreeing on one shared vocabulary.
typedef enum {
    HUMANNESS_MOOD_NEUTRAL = 0,
    HUMANNESS_MOOD_CURIOUS,
    HUMANNESS_MOOD_TIRED,
    HUMANNESS_MOOD_BORED,
    HUMANNESS_MOOD_SOCIAL,
    HUMANNESS_MOOD_FOCUSED,
    HUMANNESS_MOOD_STARTLED,
    HUMANNESS_MOOD_NERVOUS
} NPCMood;

// HumannessState -- one real, per-NPC-instance set of continuous mood fields (MISHRI's own
// energy/fatigue/curiosity/boredom, 0.0-1.0 each) plus the discrete mood enum and the real,
// timer-driven next-reroll timestamp (MISHRI's own "independent real timers per field" pattern,
// simplified here to one shared mood timer for v0 -- curiosity/boredom/energy drift
// continuously via humanness_tick_mood's own real per-tick math, not separate timers, a real,
// deliberate v0 simplification named here rather than silently matching MISHRI's own
// multi-timer design one-for-one).
typedef struct {
    float energy;    // 0.0 (exhausted) .. 1.0 (fresh)
    float fatigue;    // 0.0 (fresh) .. 1.0 (fully fatigued) -- accumulates with activity
    float curiosity;  // 0.0 .. 1.0
    float boredom;    // 0.0 .. 1.0 -- accumulates with inactivity
    NPCMood mood;
    uint32_t mood_change_at_ms; // next real timestamp humanness_tick_mood may re-roll mood at
} HumannessState;

// humanness_state_init -- a real, sane default: fresh, neutral, ready to start ticking. Callers
// needing a specific personality (nervous shopkeeper vs. hardened soldier) override individual
// fields after this call -- Phase 4's own PARENA-scriptable personality config (deferred, see
// the NORTHSTAR doc) is the eventual real, designer-facing way to do that; this function is
// deliberately not parameterized for it yet.
void humanness_state_init(HumannessState *s, uint32_t now_ms);

// humanness_tick_mood -- real, per-tick (or per-decision-cycle) update: boredom accumulates
// slowly, energy/fatigue drift back toward baseline, and mood is re-rolled (weighted toward
// NEUTRAL, matching MISHRI's own real weighting) once now_ms passes mood_change_at_ms, scheduling
// the next real reroll 5-20 real seconds out (MISHRI's own real window, scaled from MISHRI's
// 5-20 MINUTES since SHANKPIT's own NPC encounters are real, short, moment-to-moment affairs, not
// hours-long Minecraft survival sessions -- a real, deliberate unit-scaling decision, not a typo).
void humanness_tick_mood(HumannessState *s, uint32_t now_ms);

// humanness_get_startled -- a real, discrete external event (took damage, heard a nearby
// explosion) forces mood to STARTLED and schedules a real, short re-roll window -- MISHRI's own
// real getStartled() semantics exactly.
void humanness_get_startled(HumannessState *s, uint32_t now_ms);

// humanness_reaction_delay_ms -- returns a real, jittered reaction delay derived from base_ms
// (a caller-supplied floor, e.g. story_ai.c's own per-role next_attack_ms interval), widened by
// low energy/high fatigue and halved when STARTLED, doubled-ish when TIRED -- MISHRI's own real
// reactionDelay() scaling, applied to a caller-chosen base instead of MISHRI's own fixed
// 200-1200ms window (SHANKPIT's own callers already have real, tuned base intervals per role;
// this function jitters them, it doesn't replace them).
uint32_t humanness_reaction_delay_ms(const HumannessState *s, uint32_t base_ms);

// humanness_aim_noise -- a real Box-Muller Gaussian sample (mean 0), scaled by (1 - skill) so a
// skill of 1.0 (perfect) yields ~0 noise and 0.0 (unskilled) yields the full real spread, then
// further widened by fatigue and STARTLED mood -- MISHRI's own real imperfectAim() scaling
// exactly. Callers add the result to an existing aim angle (degrees); real, symmetric, unbounded
// (a caller wanting a hard clamp applies one itself, same as story_ai.c's own existing
// aim_error_deg is already a caller-owned, not-clamped-here value).
float humanness_aim_noise(const HumannessState *s, float skill_0_to_1);

// humanness_smooth_turn_step -- advances *cur_deg one real step toward target_deg at
// real turn_speed_deg_per_sec, easing near the target (MISHRI's own real bezier ease-out,
// approximated here with a real critically-damped-feeling lerp rather than porting a full cubic
// bezier -- same real "smallest real thing" scope this repo's own JSON scanners already hold
// themselves to) and, ~30% of the time when *overshooting is 0 and the turn is nearly done,
// setting *overshooting=1 and pushing *cur_deg a real few degrees PAST target_deg before a
// second call settles it back -- MISHRI's own real overshoot-then-correct behavior, not flat
// noise. Mood-modulated: STARTLED turns ~2x faster, TIRED ~0.5x, matching MISHRI's own real
// smoothTurn() mood scaling (inverted sense vs. reaction delay, matching MISHRI exactly: a
// startled mood reacts fast, a tired one reacts slow).
void humanness_smooth_turn_step(float *cur_deg, float target_deg, float turn_speed_deg_per_sec,
                                 float dt_seconds, const HumannessState *s, int *overshooting);

#endif // HUMANNESS_H
