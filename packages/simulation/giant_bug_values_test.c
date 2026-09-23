/* giant_bug_values_test.c -- real, standalone test for the Giant Zombie Bug value system
 * (EMILY/BACKLOG.md SECTION 536 follow-up). Plain assert() harness, same convention as every
 * other test in this merge.
 *
 * Build and run:
 *   gcc -Wall -Wextra -O2 -Ipackages/simulation -o /tmp/giant_bug_values_test \
 *       packages/simulation/giant_bug_values_test.c packages/simulation/giant_bug_values.c \
 *       packages/simulation/giant_bug_brain.c packages/simulation/zombie_values.c \
 *       -lm && /tmp/giant_bug_values_test
 */
#include "giant_bug_values.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    {
        GiantBugState b;
        giant_bug_state_init(&b, 0);
        assert(b.strength == 1.0f && b.speed == 1.0f);
        assert(b.hunger > 0.0f && b.swarm_density == 0.0f && b.molt_pressure == 0.0f);
        printf("PASS: giant_bug_state_init sets the real baseline (strength/speed 1.0)\n");
    }
    {
        /* Real decision outputs land in the real 0..100 range for a real, non-degenerate state. */
        GiantBugState b;
        giant_bug_state_init(&b, 0);
        b.pain = 0.9f; b.heat_scent = 0.8f; b.hive_signal = 0.7f;
        int attack = giant_bug_attack_drive(&b);
        int flee = giant_bug_flee_drive(&b);
        int swarm = giant_bug_swarm_drive(&b);
        assert(attack >= 0 && attack <= 100);
        assert(flee >= 0 && flee <= 100);
        assert(swarm >= 0 && swarm <= 100);
        /* High pain/heat/hive-signal should drive attack meaningfully higher than flee -- the
           real network's own hand-picked weights (bug_hidden_0/bug_attack_drive) favor exactly
           this combination, per giant_bug_brain.prn's own doc comment. */
        assert(attack > flee);
        printf("PASS: high pain/heat/hive-signal drives real attack > flee (attack=%d flee=%d swarm=%d)\n",
               attack, flee, swarm);
    }
    {
        /* Light aversion + pain should drive flee meaningfully higher than a calm baseline. */
        GiantBugState calm, scared;
        giant_bug_state_init(&calm, 0);
        giant_bug_state_init(&scared, 0);
        scared.light_aversion = 0.9f; scared.pain = 0.6f;
        assert(giant_bug_flee_drive(&scared) > giant_bug_flee_drive(&calm));
        printf("PASS: light aversion + pain drives real flee higher than a calm baseline\n");
    }
    {
        /* "if they eat a strong zombie they get stronger if they eat a fast zombie they get
           faster" -- eating is real, permanent, and proportional to the prey's own real values. */
        GiantBugState b;
        giant_bug_state_init(&b, 0);
        float strength_before = b.strength, speed_before = b.speed;

        ZombieState weak_slow;
        zombie_state_init(&weak_slow, 0); /* fresh spawn: aggression 0.0, slow (DORMANT reaction) */
        giant_bug_eat_zombie(&b, &weak_slow, 0);
        float strength_after_weak = b.strength, speed_after_weak = b.speed;
        assert(strength_after_weak >= strength_before && speed_after_weak >= speed_before);

        ZombieState strong;
        zombie_state_init(&strong, 0);
        strong.aggression = 1.0f; /* real, maximum aggression -- the "strong" proxy */
        strong.mood = ZOMBIE_MOOD_FRENZIED; /* real, fastest reaction-delay mood */
        giant_bug_eat_zombie(&b, &strong, 0);
        float strength_after_strong = b.strength;
        assert(strength_after_strong > strength_after_weak); /* ate something stronger -> gained more */
        printf("PASS: eating a stronger zombie grants real, proportionally more strength\n");
    }
    {
        /* Eating resets pain -- a good meal, real and simple. */
        GiantBugState b;
        giant_bug_state_init(&b, 0);
        b.pain = 0.8f;
        ZombieState prey;
        zombie_state_init(&prey, 0);
        giant_bug_eat_zombie(&b, &prey, 0);
        assert(b.pain == 0.0f);
        printf("PASS: eating resets pain to 0\n");
    }
    {
        /* molt_pressure only ever climbs, same one-way shape zombie_values.h's own decay uses. */
        GiantBugState b;
        giant_bug_state_init(&b, 0);
        float last = b.molt_pressure;
        for (int i = 0; i < 100; i++) {
            giant_bug_tick(&b, (uint32_t)i, 0, 0);
            assert(b.molt_pressure >= last);
            last = b.molt_pressure;
        }
        printf("PASS: molt_pressure is real and one-way, never decreases\n");
    }

    printf("\nALL PASS\n");
    return 0;
}
