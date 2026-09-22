/* witness_live_test.c -- real, direct coverage for witness_live.h's pure event-witnessing glue
 * (BIG_O engine merge phase 7a). Faithful replay of BIG_O's own core/witness_live_test.c
 * assertions (bigo_* -> witness_live_*), same "standing regression check a live server session
 * can't provide" reasoning as pheromone_test.c/ai_brain_test.c.
 *
 * Build and run:
 *   gcc -std=c99 -Wall -Wextra -O2 -Ipackages/simulation -o /tmp/witness_live_test \
 *       packages/simulation/witness_live_test.c packages/simulation/witness_rules.c \
 *       packages/simulation/zombie_values.c packages/simulation/ai_brain_rules.c -lm && \
 *       /tmp/witness_live_test
 */
#include "witness_live.h"
#include "witness_sim.h" /* WS_* state enum */

#include <assert.h>
#include <stdio.h>

static void test_zombie_witnessable_only_when_hunting_or_frenzied(void) {
    assert(!witness_live_zombie_is_witnessable_event(ZOMBIE_MOOD_DORMANT));
    assert(!witness_live_zombie_is_witnessable_event(ZOMBIE_MOOD_AGITATED));
    assert(witness_live_zombie_is_witnessable_event(ZOMBIE_MOOD_HUNTING));
    assert(witness_live_zombie_is_witnessable_event(ZOMBIE_MOOD_FRENZIED));
    printf("PASS: only HUNTING/FRENZIED zombies count as a witnessable event\n");
}

static void test_in_range_true_and_false(void) {
    assert(witness_live_in_range(0.0f, 0.0f, 3.0f, 4.0f, 5.0f));   /* exactly at the radius */
    assert(witness_live_in_range(0.0f, 0.0f, 3.0f, 4.0f, 10.0f));  /* well within */
    assert(!witness_live_in_range(0.0f, 0.0f, 30.0f, 40.0f, 5.0f)); /* well outside */
    printf("PASS: witness_live_in_range correctly bounds a flat (x,z) distance check\n");
}

static void test_unwitnessed_event_keeps_unaware(void) {
    /* count=0 -- no humans in range -- must stay UNAWARE regardless of arrogance. */
    int nx = witness_live_next_state_for_event(WS_UNAWARE, 0, 50, 0);
    assert(nx == WS_UNAWARE);
    printf("PASS: a zero-witness event keeps a human NPC UNAWARE\n");
}

static void test_single_witness_goes_to_denial(void) {
    int nx = witness_live_next_state_for_event(WS_UNAWARE, 1, 50, 0);
    assert(nx == WS_DENIAL);
    assert(is_legal_transition(WS_UNAWARE, nx));
    printf("PASS: a single witness of a loud zombie event goes to DENIAL (1 witness -> catatonic denial)\n");
}

static void test_five_plus_witnesses_go_to_silencing(void) {
    int nx = witness_live_next_state_for_event(WS_UNAWARE, 5, 50, 0);
    assert(nx == WS_SILENCING);
    assert(is_legal_transition(WS_UNAWARE, nx));
    printf("PASS: 5+ witnesses of a loud zombie event escalate straight to SILENCING\n");
}

static void test_high_arrogance_engages_instead_of_silencing(void) {
    int nx = witness_live_next_state_for_event(WS_UNAWARE, 5, 90, 0);
    assert(nx == WS_ENGAGE);
    printf("PASS: high arrogance (>=70) at 5+ witnesses ENGAGEs the zombie instead of silencing the player\n");
}

static void test_resolved_memory_wipe_returns_hunt_to_denial(void) {
    /* A hunt (SILENCING) persists even at count=0 until resolved -- exercised exactly as The Men's
       own future dispatch loop will use it: arrival triggers resolved=1. */
    int still_hunting = witness_live_next_state_for_event(WS_SILENCING, 0, 50, 0);
    assert(still_hunting == WS_SILENCING); /* losing witnesses alone does not end a hunt */

    int resolved = witness_live_next_state_for_event(WS_SILENCING, 0, 50, 1);
    assert(resolved == WS_DENIAL);
    assert(is_legal_transition(WS_SILENCING, resolved));
    printf("PASS: a hunt persists until resolved=1 (The Men's memory wipe), then returns to DENIAL\n");
}

static void test_compromised_state_is_absorbing_even_under_a_new_event(void) {
    /* This glue never passes compromised=1 (witness_sim.c's own scenario-only forced-witness
       feature, not built live here) -- but a human NPC that reached COMPROMISED some other way
       must stay there when this glue evaluates a later event. */
    int nx = witness_live_next_state_for_event(WS_COMPROMISED, 5, 50, 0);
    assert(nx == WS_COMPROMISED);
    printf("PASS: COMPROMISED stays absorbing even when this glue evaluates a later loud event\n");
}

int main(void) {
    test_zombie_witnessable_only_when_hunting_or_frenzied();
    test_in_range_true_and_false();
    test_unwitnessed_event_keeps_unaware();
    test_single_witness_goes_to_denial();
    test_five_plus_witnesses_go_to_silencing();
    test_high_arrogance_engages_instead_of_silencing();
    test_resolved_memory_wipe_returns_hunt_to_denial();
    test_compromised_state_is_absorbing_even_under_a_new_event();
    printf("\nALL PASS\n");
    return 0;
}
