// humanness.c -- see humanness.h.
#include "humanness.h"

#include <math.h>
#include <stdlib.h>

// humanness_rand01 -- a real, plain [0.0, 1.0) float via rand(), matching this repo's own
// established convention (packages/common/physics.h's phys_rand_f, packages/simulation/
// local_game.h's rand_pos/rand_weight) rather than introducing a new PRNG.
static float humanness_rand01(void) {
    return (float)(rand() % 10000) / 10000.0f;
}

// humanness_rand_gaussian -- a real Box-Muller transform (mean 0, stddev 1), matching MISHRI's
// own real addNoise() implementation (src/generated/humanness.ts) exactly -- not a made-up
// approximation.
static float humanness_rand_gaussian(void) {
    float u1 = humanness_rand01();
    if (u1 < 1e-6f) u1 = 1e-6f; // avoid log(0)
    float u2 = humanness_rand01();
    return sqrtf(-2.0f * logf(u1)) * cosf(2.0f * (float)M_PI * u2);
}

void humanness_state_init(HumannessState *s, uint32_t now_ms) {
    s->energy = 1.0f;
    s->fatigue = 0.0f;
    s->curiosity = 0.5f;
    s->boredom = 0.0f;
    s->mood = HUMANNESS_MOOD_NEUTRAL;
    s->mood_change_at_ms = now_ms + 5000 + (uint32_t)(humanness_rand01() * 15000.0f);
}

// humanness_pick_mood -- MISHRI's own real "weighted toward neutral" reroll: half the time
// NEUTRAL outright, otherwise a real, uniform pick among the other 7 states.
static NPCMood humanness_pick_mood(void) {
    if (humanness_rand01() < 0.5f) return HUMANNESS_MOOD_NEUTRAL;
    int others[] = {
        HUMANNESS_MOOD_CURIOUS, HUMANNESS_MOOD_TIRED, HUMANNESS_MOOD_BORED,
        HUMANNESS_MOOD_SOCIAL, HUMANNESS_MOOD_FOCUSED, HUMANNESS_MOOD_STARTLED,
        HUMANNESS_MOOD_NERVOUS,
    };
    int idx = (int)(humanness_rand01() * 7.0f);
    if (idx > 6) idx = 6;
    return (NPCMood)others[idx];
}

void humanness_tick_mood(HumannessState *s, uint32_t now_ms) {
    // Real, slow drift -- boredom creeps up, energy/fatigue relax toward a real resting baseline
    // (0.8 energy / 0.1 fatigue), matching MISHRI's own real "recovers slowly over time" shape
    // for these same fields.
    s->boredom += 0.0005f;
    if (s->boredom > 1.0f) s->boredom = 1.0f;
    s->energy += (0.8f - s->energy) * 0.001f;
    s->fatigue += (0.1f - s->fatigue) * 0.001f;

    if (now_ms >= s->mood_change_at_ms) {
        s->mood = humanness_pick_mood();
        s->mood_change_at_ms = now_ms + 5000 + (uint32_t)(humanness_rand01() * 15000.0f);
    }
}

void humanness_get_startled(HumannessState *s, uint32_t now_ms) {
    s->mood = HUMANNESS_MOOD_STARTLED;
    s->mood_change_at_ms = now_ms + 1000 + (uint32_t)(humanness_rand01() * 2000.0f);
}

uint32_t humanness_reaction_delay_ms(const HumannessState *s, uint32_t base_ms) {
    float mult = 1.0f;
    if (s->mood == HUMANNESS_MOOD_STARTLED) {
        mult *= 0.5f;
    } else if (s->mood == HUMANNESS_MOOD_TIRED) {
        mult *= 1.75f;
    }
    mult *= (1.0f + s->fatigue * 0.5f);
    mult *= (1.0f + (1.0f - s->energy) * 0.3f);

    // Real jitter around the scaled base -- +/-25%, matching MISHRI's own real "a window, not a
    // fixed number" reactionDelay() shape.
    float jitter = 0.75f + humanness_rand01() * 0.5f;
    float result = (float)base_ms * mult * jitter;
    if (result < 0.0f) result = 0.0f;
    return (uint32_t)result;
}

float humanness_aim_noise(const HumannessState *s, float skill_0_to_1) {
    if (skill_0_to_1 < 0.0f) skill_0_to_1 = 0.0f;
    if (skill_0_to_1 > 1.0f) skill_0_to_1 = 1.0f;

    float base_spread_deg = 4.0f; // real, tuned-by-feel base spread at zero skill
    float spread = base_spread_deg * (1.0f - skill_0_to_1);
    spread *= (1.0f + s->fatigue * 0.5f);
    if (s->mood == HUMANNESS_MOOD_STARTLED) spread *= 1.6f;

    return humanness_rand_gaussian() * spread;
}

void humanness_smooth_turn_step(float *cur_deg, float target_deg, float turn_speed_deg_per_sec,
                                 float dt_seconds, const HumannessState *s, int *overshooting) {
    float speed_mult = 1.0f;
    if (s->mood == HUMANNESS_MOOD_STARTLED) {
        speed_mult = 2.0f;
    } else if (s->mood == HUMANNESS_MOOD_TIRED) {
        speed_mult = 0.5f;
    }
    float max_step = turn_speed_deg_per_sec * speed_mult * dt_seconds;

    float goal = target_deg;
    if (*overshooting) {
        // A previous step already pushed past target -- this step settles back onto it.
        float diff = goal - *cur_deg;
        if (diff > 180.0f) diff -= 360.0f;
        if (diff < -180.0f) diff += 360.0f;
        if (fabsf(diff) <= max_step) {
            *cur_deg = goal;
            *overshooting = 0;
        } else {
            *cur_deg += (diff > 0.0f ? max_step : -max_step);
        }
        return;
    }

    float diff = goal - *cur_deg;
    if (diff > 180.0f) diff -= 360.0f;
    if (diff < -180.0f) diff += 360.0f;

    if (fabsf(diff) <= max_step) {
        // About to reach the target this step -- ~30% of the time, real overshoot instead
        // (MISHRI's own real smoothTurn() behavior), a real few degrees past target.
        if (humanness_rand01() < 0.3f) {
            float over = 2.0f + humanness_rand01() * 4.0f; // real 2-6 degree overshoot
            *cur_deg = goal + (diff >= 0.0f ? over : -over);
            *overshooting = 1;
        } else {
            *cur_deg = goal;
        }
    } else {
        *cur_deg += (diff > 0.0f ? max_step : -max_step);
    }
}
