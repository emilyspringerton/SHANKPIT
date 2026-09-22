#ifndef ZOMBIE_VALUES_H
#define ZOMBIE_VALUES_H

/* zombie_values.h -- BIG_O engine merge phase 3 (EMILY/BACKLOG.md SECTION 536). Ported in from
 * BIG_O's core/zombie_values.h, unchanged in shape: founder real-time, "ZOMBIES need to also have
 * their own values and attention mechanisms but they are more zombie values and behaving." Real,
 * deliberate answer: zombies do NOT get an NpcBrain (npc_archetype.h) with different tuning
 * numbers plugged into the same human mood enum. They get a genuinely different vocabulary --
 * hunger/aggression/decay instead of energy/curiosity/boredom, a 4-state mood arc (DORMANT/
 * AGITATED/HUNTING/FRENZIED) instead of MISHRI's 8-state human set, reaction timing that gets
 * SLOWER at rest and FASTER (with real, growing erraticism, not just speed) under frenzy rather
 * than humanness.c's own startled/tired human curve, and a one-way "decay" value with no human
 * equivalent at all. The underlying MATH primitives (a jittered delay window, Box-Muller Gaussian
 * noise, a timer-driven mood reroll) are the same real shape humanness.c already proved --
 * reimplemented here with zombie-flavored inputs/curves, not literally shared code.
 *
 * Real integration boundary, stated plainly: this module is NOT wired into
 * packages/simulation/witness_sim.h. Zombies are the thing citizens/The Men witness (a "zombie
 * event" flag, BIG_O's own terrain/tactic state machine), never a witness themselves --
 * zombie_effective_alertness is a real, separate "how far away can this zombie sense a target"
 * analog to witness_sim's vigilance, a future perception-radius hook, not a second implementation
 * of the same system.
 *
 * zombie_effective_alertness's own formula moved to the same real PARENA rules module as
 * npc_archetype's vigilance formula (PARENA/stdlib/shankpit/ai_brain_rules.prn) per "use parena
 * duh" -- it's a pure scalar decision formula. Everything else (mood ticking, RNG-jittered
 * timers, Box-Muller sampling) stays host C, matching humanness_tick_mood's own precedent. */

#include <stdint.h>

typedef enum {
    ZOMBIE_MOOD_DORMANT = 0,  /* no target, ambient shamble */
    ZOMBIE_MOOD_AGITATED,     /* sensed something (nearby noise/movement), not yet locked on */
    ZOMBIE_MOOD_HUNTING,      /* has a sustained target, actively pursuing */
    ZOMBIE_MOOD_FRENZIED      /* high aggression/hunger override -- fastest, most erratic */
} ZombieMood;

typedef struct {
    float hunger;      /* 0.0-1.0, rises steadily while DORMANT/no target; pushes DORMANT->AGITATED */
    float aggression;  /* 0.0-1.0, rises while a target is held, decays when lost; drives HUNTING->FRENZIED */
    float decay;       /* 0.0-1.0, ONE-WAY -- never decreases across any real tick count (a physical-
                           deterioration clock, not a mood), slowly widens this zombie's own reaction
                           delay ceiling and lunge inaccuracy over its lifetime */
    ZombieMood mood;
    uint32_t mood_change_at_ms; /* next real timer-driven mood re-evaluation, same pattern humanness.c uses */
} ZombieState;

/* Fresh spawn: DORMANT, hunger/aggression/decay all 0.0. */
void zombie_state_init(ZombieState *z, uint32_t now_ms);

/* Real, per-tick update. has_target (bool-ish int) drives hunger/aggression drift and the
 * DORMANT<->AGITATED<->HUNTING mood arc; FRENZIED is only ever entered via zombie_get_agitated's
 * own real trigger (a sudden stimulus) or aggression crossing a real, high threshold while
 * already HUNTING, never a plain tick-driven drift into it. */
void zombie_tick(ZombieState *z, uint32_t now_ms, int has_target);

/* A real, discrete external stimulus (gunfire, an explosion, a fresh wound) forces at least
 * AGITATED (escalates existing HUNTING to FRENZIED instead of downgrading it) and schedules a
 * real, short re-evaluation window -- the zombie-flavored analog to humanness_get_startled. */
void zombie_get_agitated(ZombieState *z, uint32_t now_ms);

/* Returns a real, jittered reaction delay derived from base_ms, SLOWER than base_ms at DORMANT/
 * AGITATED (sluggish shamble -- up to 2.5x), roughly base_ms at HUNTING, and FASTER but genuinely
 * MORE VARIABLE at FRENZIED (mean below base_ms, real spread wider than any other mood). decay
 * widens every mood's own window multiplicatively, never narrows it. */
uint32_t zombie_reaction_delay_ms(const ZombieState *z, uint32_t base_ms);

/* A real Box-Muller Gaussian sample (mean 0) added to an attack-lunge angle (degrees), scaled UP
 * by (1 - aggression) and further widened by decay. */
float zombie_lunge_noise(const ZombieState *z, float aggression_skill_0_to_1);

/* 0..100, this module's own real "attention" analog to witness_sim's vigilance -- NOT fed into
 * witness_sim.c (see this header's own top doc comment on why). PARENA-computed, see this
 * header's own top doc comment. */
int zombie_effective_alertness(const ZombieState *z);

#endif
