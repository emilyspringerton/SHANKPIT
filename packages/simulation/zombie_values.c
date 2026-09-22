#include "zombie_values.h"

#include <math.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Prototype for the PARENA-generated formula (ai_brain_rules.c, do-not-edit-by-hand). No shared
 * .h between generated units in this codebase (see npc_archetype.c's own identical comment). */
int zombie_alertness_formula(int mood, int hunger_pct, int aggression_pct);

static float zv_rand01(void) {
    return (float)(rand() % 10000) / 10000.0f;
}

/* Box-Muller Gaussian, mean 0 stddev 1 -- same real shape as humanness.c's own private
 * humanness_rand_gaussian, deliberately re-implemented rather than shared (see zombie_values.h's
 * own top doc comment: the two modules stay independent on purpose). */
static float zv_rand_gaussian(void) {
    float u1 = zv_rand01();
    if (u1 < 1e-6f) u1 = 1e-6f;
    float u2 = zv_rand01();
    return sqrtf(-2.0f * logf(u1)) * cosf(2.0f * (float)M_PI * u2);
}

#define ZV_HUNGER_RISE_PER_SEC 0.02f      /* ~50s DORMANT->hungry without a target */
#define ZV_HUNGER_AGITATE_THRESHOLD 0.4f
#define ZV_AGGRO_RISE_PER_SEC 0.35f       /* fast climb once a target is held */
#define ZV_AGGRO_DECAY_PER_SEC 0.15f      /* slower fade once lost -- lingering wariness */
#define ZV_AGGRO_FRENZY_THRESHOLD 0.85f
#define ZV_DECAY_RISE_PER_SEC 0.0015f     /* ~11 real minutes to fully decayed -- a long lifetime clock */

void zombie_state_init(ZombieState *z, uint32_t now_ms) {
    z->hunger = 0.0f;
    z->aggression = 0.0f;
    z->decay = 0.0f;
    z->mood = ZOMBIE_MOOD_DORMANT;
    z->mood_change_at_ms = now_ms + 1000u; /* first real evaluation one second out */
}

void zombie_tick(ZombieState *z, uint32_t now_ms, int has_target) {
    /* Real, fixed-ish per-call step -- see this header's own zombie_tick doc comment for the
     * real, honest v0 "flat ~1 real second per call" simplification (inherited from BIG_O
     * unchanged; a real dt_sec parameter is a named, separate follow-up, same one BIG_O's own
     * NORTHSTAR §10 already flagged and deliberately deferred). */
    const float dt_sec = 1.0f;

    if (has_target) {
        z->hunger -= 0.05f * dt_sec; /* pursuing/feeding relieves hunger pressure */
        if (z->hunger < 0.0f) z->hunger = 0.0f;
        z->aggression += ZV_AGGRO_RISE_PER_SEC * dt_sec;
    } else {
        z->hunger += ZV_HUNGER_RISE_PER_SEC * dt_sec;
        z->aggression -= ZV_AGGRO_DECAY_PER_SEC * dt_sec;
    }
    if (z->hunger > 1.0f) z->hunger = 1.0f;
    if (z->aggression < 0.0f) z->aggression = 0.0f;
    if (z->aggression > 1.0f) z->aggression = 1.0f;

    /* Decay is real, one-way, unconditional -- never gated on has_target, never decreases. */
    z->decay += ZV_DECAY_RISE_PER_SEC * dt_sec;
    if (z->decay > 1.0f) z->decay = 1.0f;

    if (now_ms < z->mood_change_at_ms) return;

    /* Frenzy is only ever entered as a real spike -- never a plain drift target here. A
     * FRENZIED zombie decays back to HUNTING once aggression drops back under the threshold. */
    if (z->mood == ZOMBIE_MOOD_FRENZIED) {
        if (z->aggression < ZV_AGGRO_FRENZY_THRESHOLD) z->mood = ZOMBIE_MOOD_HUNTING;
    } else if (has_target && z->aggression >= ZV_AGGRO_FRENZY_THRESHOLD) {
        z->mood = ZOMBIE_MOOD_FRENZIED;
    } else if (has_target) {
        z->mood = ZOMBIE_MOOD_HUNTING;
    } else if (z->hunger >= ZV_HUNGER_AGITATE_THRESHOLD) {
        z->mood = ZOMBIE_MOOD_AGITATED;
    } else {
        z->mood = ZOMBIE_MOOD_DORMANT;
    }
    z->mood_change_at_ms = now_ms + 1000u + (uint32_t)(zv_rand01() * 2000.0f); /* 1-3s next check */
}

void zombie_get_agitated(ZombieState *z, uint32_t now_ms) {
    if (z->mood == ZOMBIE_MOOD_HUNTING || z->mood == ZOMBIE_MOOD_FRENZIED) {
        z->mood = ZOMBIE_MOOD_FRENZIED;
        z->aggression = 1.0f;
    } else {
        z->mood = ZOMBIE_MOOD_AGITATED;
    }
    z->mood_change_at_ms = now_ms + 500u; /* short real window -- a stimulus demands a quick re-check */
}

uint32_t zombie_reaction_delay_ms(const ZombieState *z, uint32_t base_ms) {
    float mean_mult, spread_mult;
    switch (z->mood) {
        case ZOMBIE_MOOD_DORMANT:  mean_mult = 2.5f; spread_mult = 0.3f; break;
        case ZOMBIE_MOOD_AGITATED: mean_mult = 1.6f; spread_mult = 0.4f; break;
        case ZOMBIE_MOOD_HUNTING:  mean_mult = 1.0f; spread_mult = 0.3f; break;
        case ZOMBIE_MOOD_FRENZIED: default: mean_mult = 0.55f; spread_mult = 0.9f; break; /* fast mean, wide real spread -- erratic, not just quick */
    }
    /* Decay widens every mood's own window multiplicatively -- a decaying zombie is slower
     * across the board, regardless of current mood. */
    float decay_mult = 1.0f + z->decay * 0.8f;
    float mean = (float)base_ms * mean_mult * decay_mult;
    /* Spread scales off base_ms directly, NOT off the mood-adjusted mean -- DORMANT's much larger
     * mean_mult would otherwise inflate its absolute spread past FRENZIED's despite FRENZIED's
     * far higher spread_mult, exactly backwards from the real "fast mean, wide spread, erratic"
     * contract this function documents. */
    float spread = (float)base_ms * spread_mult * decay_mult;
    float sample = mean + zv_rand_gaussian() * spread;
    if (sample < (float)base_ms * 0.15f) sample = (float)base_ms * 0.15f; /* real floor -- never instant */
    return (uint32_t)sample;
}

float zombie_lunge_noise(const ZombieState *z, float aggression_skill_0_to_1) {
    float base_spread = (1.0f - aggression_skill_0_to_1) * 30.0f; /* degrees, wide at low aggression */
    float spread = base_spread * (1.0f + z->decay * 1.5f); /* decayed zombies lunge worse still */
    return zv_rand_gaussian() * spread;
}

int zombie_effective_alertness(const ZombieState *z) {
    int hunger_pct = (int)(z->hunger * 100.0f + 0.5f);
    int aggression_pct = (int)(z->aggression * 100.0f + 0.5f);
    return zombie_alertness_formula((int)z->mood, hunger_pct, aggression_pct);
}
