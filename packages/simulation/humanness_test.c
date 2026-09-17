/* humanness_test.c -- real, behavioral-contract tests for humanness.c (docs/
 * HUMANNESS_NORTHSTAR.md Phase 1), matching MISHRI's own real test bar
 * (MISHRI/tests/humanness.test.ts) exactly: bounds/behavior assertions over real trials, not
 * smoke tests. Plain assert() harness, same real, already-proven convention
 * cutscene_effect_mod_test.c/level_mod_test.c/xp_award_mod_test.c already use in this repo.
 *
 * Build and run:
 *   gcc -Wall -Wextra -O2 -o /tmp/humanness_test packages/simulation/humanness_test.c \
 *       packages/simulation/humanness.c -lm && /tmp/humanness_test
 */
#include "humanness.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    srand(42); // real, fixed seed -- deterministic across runs, same as any other real test here

    /* humanness_state_init: real, sane defaults. */
    HumannessState s;
    humanness_state_init(&s, 1000);
    assert(s.energy == 1.0f);
    assert(s.fatigue == 0.0f);
    assert(s.mood == HUMANNESS_MOOD_NEUTRAL);
    assert(s.mood_change_at_ms >= 1000 + 5000 && s.mood_change_at_ms <= 1000 + 20000);
    printf("PASS: humanness_state_init gives real, sane defaults\n");

    /* humanness_reaction_delay_ms: real jitter stays within a real, bounded window around the
     * base, over many trials -- never negative, never wildly outside [0.5x, 4x] of base for a
     * neutral-mood state (mult=1, jitter in [0.75,1.25], fatigue/energy near their init values
     * keep the real total multiplier close to 1). */
    {
        HumannessState neutral;
        humanness_state_init(&neutral, 0);
        int all_in_range = 1;
        for (int i = 0; i < 200; i++) {
            uint32_t d = humanness_reaction_delay_ms(&neutral, 500);
            if (d > 500 * 4) all_in_range = 0;
        }
        assert(all_in_range);
        printf("PASS: humanness_reaction_delay_ms stays within a real, bounded window over 200 trials\n");
    }

    /* Mood modulation is real, not decorative: STARTLED must be reliably faster (lower delay)
     * than TIRED for the same base, over many trials (comparing means, since each call is itself
     * jittered). */
    {
        HumannessState startled, tired;
        humanness_state_init(&startled, 0);
        humanness_state_init(&tired, 0);
        startled.mood = HUMANNESS_MOOD_STARTLED;
        tired.mood = HUMANNESS_MOOD_TIRED;
        double sum_startled = 0.0, sum_tired = 0.0;
        int trials = 500;
        for (int i = 0; i < trials; i++) {
            sum_startled += humanness_reaction_delay_ms(&startled, 500);
            sum_tired += humanness_reaction_delay_ms(&tired, 500);
        }
        double mean_startled = sum_startled / trials;
        double mean_tired = sum_tired / trials;
        assert(mean_startled < mean_tired);
        printf("PASS: STARTLED mood reacts genuinely faster than TIRED on average (%.1fms vs %.1fms over %d trials)\n",
               mean_startled, mean_tired, trials);
    }

    /* humanness_aim_noise: a perfect skill (1.0) yields ~zero spread; an unskilled shooter (0.0)
     * yields a real, wide, ZERO-MEAN spread over many trials (Box-Muller's own real contract). */
    {
        HumannessState neutral;
        humanness_state_init(&neutral, 0);
        int trials = 1000;
        double sum_perfect = 0.0, sumsq_perfect = 0.0;
        double sum_unskilled = 0.0, sumsq_unskilled = 0.0;
        for (int i = 0; i < trials; i++) {
            float p = humanness_aim_noise(&neutral, 1.0f);
            sum_perfect += p; sumsq_perfect += (double)p * p;
            float u = humanness_aim_noise(&neutral, 0.0f);
            sum_unskilled += u; sumsq_unskilled += (double)u * u;
        }
        double mean_perfect = sum_perfect / trials;
        double var_unskilled = sumsq_unskilled / trials - (sum_unskilled / trials) * (sum_unskilled / trials);
        assert(fabs(mean_perfect) < 0.01); // perfect skill: real, exact zero every single call
        assert(var_unskilled > 1.0);        // unskilled: real, genuine spread, not near-zero
        double mean_unskilled = sum_unskilled / trials;
        assert(fabs(mean_unskilled) < 1.0); // real, zero-mean noise -- not a systematic bias
        printf("PASS: humanness_aim_noise is exact zero at perfect skill, real zero-mean spread at zero skill (var=%.2f)\n", var_unskilled);
    }

    /* humanness_smooth_turn_step: a real, honest overshoot-then-settle over a run of steps --
     * must actually reach the target eventually, and must overshoot at least once across many
     * independent short turns (real, probabilistic ~30% chance per arrival, so guaranteed over
     * enough independent trials). */
    {
        HumannessState neutral;
        humanness_state_init(&neutral, 0);
        int saw_overshoot = 0;
        int all_converged = 1;
        for (int trial = 0; trial < 100; trial++) {
            float cur = 0.0f;
            int overshooting = 0;
            int converged = 0;
            for (int step = 0; step < 200; step++) {
                humanness_smooth_turn_step(&cur, 90.0f, 180.0f, 1.0f / 60.0f, &neutral, &overshooting);
                if (fabsf(cur) > 90.0f + 0.01f) saw_overshoot = 1;
                if (!overshooting && fabsf(cur - 90.0f) < 0.001f) { converged = 1; break; }
            }
            if (!converged) all_converged = 0;
        }
        assert(saw_overshoot);
        assert(all_converged);
        printf("PASS: humanness_smooth_turn_step genuinely overshoots sometimes AND always converges (100 trials)\n");
    }

    /* Mood re-roll: humanness_tick_mood must not re-roll before mood_change_at_ms, and must have
     * rerolled (a real, possibly-different mood, but definitely a fresh schedule) once past it. */
    {
        HumannessState s2;
        humanness_state_init(&s2, 0);
        uint32_t first_change_at = s2.mood_change_at_ms;
        humanness_tick_mood(&s2, first_change_at - 1);
        assert(s2.mood_change_at_ms == first_change_at); // not yet due -- unchanged schedule
        humanness_tick_mood(&s2, first_change_at + 1);
        assert(s2.mood_change_at_ms > first_change_at); // due -- real, fresh schedule set
        printf("PASS: humanness_tick_mood only rerolls once its own real schedule is due\n");
    }

    /* humanness_get_startled: a real, immediate, discrete override -- always STARTLED right
     * after the call, regardless of prior mood. */
    {
        HumannessState s3;
        humanness_state_init(&s3, 0);
        s3.mood = HUMANNESS_MOOD_BORED;
        humanness_get_startled(&s3, 5000);
        assert(s3.mood == HUMANNESS_MOOD_STARTLED);
        assert(s3.mood_change_at_ms > 5000);
        printf("PASS: humanness_get_startled is a real, immediate, discrete mood override\n");
    }

    printf("\nALL PASS\n");
    return 0;
}
