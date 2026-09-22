/* ai_brain_test.c -- real tests for npc_archetype.c + zombie_values.c (BIG_O engine merge, phase
 * 3), checking the port against BIG_O's own real, already-proven test assertions
 * (core/npc_archetype_test.c, core/zombie_values_test.c) rather than just "it compiles" --
 * important here specifically because the effective-vigilance/alertness formulas were moved
 * through a real PARENA rules module (ai_brain_rules.c) along the way, a real behavior-preserving
 * risk this test exists to catch. Plain assert() harness, same convention as
 * day_night_clock_test.c/witness_sim_test.c.
 *
 * Build and run:
 *   gcc -Wall -Wextra -O2 -o /tmp/ai_brain_test packages/simulation/ai_brain_test.c \
 *       packages/simulation/npc_archetype.c packages/simulation/zombie_values.c \
 *       packages/simulation/humanness.c packages/simulation/ai_brain_rules.c -lm && /tmp/ai_brain_test
 */
#include "npc_archetype.h"
#include "zombie_values.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    /* npc_archetype: archetype-differentiated base_vigilance defaults. */
    {
        NpcBrain citizen, men;
        npc_brain_init(&citizen, NPC_ARCHETYPE_CITIZEN, 1000);
        npc_brain_init(&men, NPC_ARCHETYPE_THE_MEN, 1000);
        assert(citizen.base_vigilance == 35);
        assert(men.base_vigilance == 85);
        printf("PASS: npc_brain_init gives archetype-differentiated base_vigilance (citizen=35, the_men=85)\n");
    }

    /* npc_archetype: effective vigilance stays within [0,100] across 500 fatigue/energy trials --
       the real bar the port's own PARENA clamp must still clear. */
    {
        NpcBrain b;
        npc_brain_init(&b, NPC_ARCHETYPE_CITIZEN, 1000);
        for (int trial = 0; trial < 500; trial++) {
            b.humanness.fatigue = (float)trial / 500.0f;
            b.humanness.energy = 1.0f - (float)trial / 500.0f;
            int v = npc_brain_effective_vigilance(&b);
            assert(v >= 0 && v <= 100);
        }
        printf("PASS: npc_brain_effective_vigilance stays within [0,100] across 500 fatigue/energy trials\n");
    }

    /* npc_archetype: STARTLED raises effective vigilance by exactly +25 (the real, documented
       delta, now computed through PARENA -- checked exactly, not just "higher"). */
    {
        NpcBrain baseline, startled;
        npc_brain_init(&baseline, NPC_ARCHETYPE_CITIZEN, 1000);
        npc_brain_init(&startled, NPC_ARCHETYPE_CITIZEN, 1000);
        baseline.humanness.mood = HUMANNESS_MOOD_NEUTRAL;
        startled.humanness.mood = HUMANNESS_MOOD_STARTLED;
        int v_baseline = npc_brain_effective_vigilance(&baseline);
        int v_startled = npc_brain_effective_vigilance(&startled);
        assert(v_startled - v_baseline == 25);
        printf("PASS: STARTLED mood raises effective vigilance by exactly +25 (%d -> %d)\n", v_baseline, v_startled);
    }

    /* npc_archetype: TIRED + high fatigue + low energy genuinely lowers vigilance. */
    {
        NpcBrain fresh, tired;
        npc_brain_init(&fresh, NPC_ARCHETYPE_THE_MEN, 1000);
        npc_brain_init(&tired, NPC_ARCHETYPE_THE_MEN, 1000);
        fresh.humanness.mood = HUMANNESS_MOOD_NEUTRAL;
        fresh.humanness.fatigue = 0.0f;
        fresh.humanness.energy = 1.0f;
        tired.humanness.mood = HUMANNESS_MOOD_TIRED;
        tired.humanness.fatigue = 0.9f;
        tired.humanness.energy = 0.1f;
        int v_fresh = npc_brain_effective_vigilance(&fresh);
        int v_tired = npc_brain_effective_vigilance(&tired);
        assert(v_tired < v_fresh);
        printf("PASS: TIRED + high fatigue + low energy lowers effective vigilance (%d -> %d)\n", v_fresh, v_tired);
    }

    /* zombie_values: fresh spawn is DORMANT with all-zero drives. */
    {
        ZombieState z;
        zombie_state_init(&z, 500);
        assert(z.mood == ZOMBIE_MOOD_DORMANT);
        assert(z.hunger == 0.0f && z.aggression == 0.0f && z.decay == 0.0f);
        printf("PASS: zombie_state_init gives a real, sane DORMANT spawn\n");
    }

    /* zombie_values: zombie_effective_alertness is bounded and strictly increases across the real
       DORMANT->AGITATED->HUNTING->FRENZIED mood arc at equal hunger/aggression -- the exact real
       assertion BIG_O's own core/zombie_values_test.c makes, now routed through PARENA. */
    {
        ZombieState z;
        zombie_state_init(&z, 0);
        int prev = -1;
        ZombieMood order[4] = {ZOMBIE_MOOD_DORMANT, ZOMBIE_MOOD_AGITATED, ZOMBIE_MOOD_HUNTING, ZOMBIE_MOOD_FRENZIED};
        for (int i = 0; i < 4; i++) {
            z.mood = order[i];
            z.hunger = 0.0f; z.aggression = 0.0f;
            int a = zombie_effective_alertness(&z);
            assert(a >= 0 && a <= 100);
            assert(a > prev);
            prev = a;
        }
        printf("PASS: zombie_effective_alertness is bounded and strictly increases across the real mood arc\n");
    }

    /* zombie_values: real per-tick drift -- no target raises hunger, having a target raises
       aggression, matching the module's own documented direction. */
    {
        ZombieState z;
        zombie_state_init(&z, 0);
        zombie_tick(&z, 1000, 0);
        assert(z.hunger > 0.0f);
        float hunger_after_idle = z.hunger;
        zombie_tick(&z, 2000, 1);
        assert(z.aggression > 0.0f);
        assert(z.hunger <= hunger_after_idle); /* pursuing relieves hunger pressure, never raises it */
        printf("PASS: zombie_tick drifts hunger up while idle, aggression up while target-locked\n");
    }

    printf("ALL PASS\n");
    return 0;
}
