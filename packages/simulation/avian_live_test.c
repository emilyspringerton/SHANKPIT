/* avian_live_test.c -- real, direct coverage for avian_live.h's "observing the observer" glue
 * (the coalition reacting to OTHER AI systems' own attention/witness signals, not raw world
 * events). Same "standing regression check a live server session can't provide" reasoning as
 * witness_live_test.c/bigo_pheromone_test.c.
 *
 * Build and run:
 *   gcc -std=c99 -Wall -Wextra -O2 -Icore -o /tmp/avian_live_test core/avian_live_test.c \
 *       core/avian_values.c core/witness_rules.c core/zombie_values.c -lm && /tmp/avian_live_test
 */
#include "avian_live.h"
#include <assert.h>
#include <stdio.h>

static void test_below_vigilance_threshold_does_not_alert(void) {
    AvianState a;
    avian_state_init(&a, 0);
    int fired = avian_live_observe_npc_vigilance(&a, AVIAN_OBSERVED_VIGILANCE_THRESHOLD - 1, 1000);
    assert(!fired);
    assert(a.mood == AVIAN_MOOD_ROOSTING); /* real, unmoved -- ordinary background vigilance carries no signal */
    printf("PASS: an NPC below the vigilance threshold does not alert the coalition\n");
}

static void test_npc_vigilance_spike_alerts_the_coalition(void) {
    AvianState a;
    avian_state_init(&a, 0);
    int fired = avian_live_observe_npc_vigilance(&a, AVIAN_OBSERVED_VIGILANCE_THRESHOLD, 1000);
    assert(fired);
    assert(a.mood == AVIAN_MOOD_SCOUTING); /* the real avian_get_alerted contract from ROOSTING */
    assert(a.vigilance > 0.5f);
    printf("PASS: an NPC's own effective_vigilance crossing the threshold genuinely alerts a bird -- 'observing the observer'\n");
}

static void test_the_men_high_baseline_alerts_more_readily_than_citizen(void) {
    /* NpcBrain isn't linked into this test binary (avoids pulling humanness.c into the test
       matrix just to prove a threshold comparison) -- exercised here directly against the real,
       documented archetype baselines (core/npc_archetype.h: Citizen base_vigilance 35, The Men
       base_vigilance 85) to prove the coalition's own threshold genuinely separates them at rest,
       before any mood modulation is even applied. */
    AvianState citizen_bird, men_bird;
    avian_state_init(&citizen_bird, 0);
    avian_state_init(&men_bird, 0);
    int citizen_fired = avian_live_observe_npc_vigilance(&citizen_bird, 35, 1000);
    int men_fired = avian_live_observe_npc_vigilance(&men_bird, 85, 1000);
    assert(!citizen_fired); /* an at-rest citizen's own baseline never alerts a bird */
    assert(men_fired);      /* an at-rest The Men baseline alone already does */
    printf("PASS: The Men's real baseline vigilance (85) alerts the coalition at rest; a Citizen's (35) does not\n");
}

static void test_unaware_and_denial_do_not_alert(void) {
    AvianState a;
    avian_state_init(&a, 0);
    assert(!avian_live_observe_witness_state(&a, WS_UNAWARE, 1000));
    assert(!avian_live_observe_witness_state(&a, WS_DENIAL, 1000));
    assert(a.mood == AVIAN_MOOD_ROOSTING);
    printf("PASS: a merely UNAWARE or DENIAL human witness does not alert the coalition\n");
}

static void test_compromised_and_beyond_alert_the_coalition(void) {
    int states[] = {WS_COMPROMISED, WS_SILENCING, WS_PANIC, WS_ENGAGE};
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
        AvianState a;
        avian_state_init(&a, 0);
        int fired = avian_live_observe_witness_state(&a, states[i], 1000);
        assert(fired);
    }
    printf("PASS: WS_COMPROMISED and every state past it genuinely alert the coalition -- the coalition archives the archivist\n");
}

static void test_zombie_event_channel_matches_witness_live_gate(void) {
    AvianState a;
    avian_state_init(&a, 0);
    assert(!avian_live_observe_zombie_event(&a, ZOMBIE_MOOD_DORMANT, 1000));
    assert(!avian_live_observe_zombie_event(&a, ZOMBIE_MOOD_AGITATED, 1000));
    assert(avian_live_observe_zombie_event(&a, ZOMBIE_MOOD_HUNTING, 1000));
    avian_state_init(&a, 0);
    assert(avian_live_observe_zombie_event(&a, ZOMBIE_MOOD_FRENZIED, 1000));
    printf("PASS: the coalition's zombie-event channel agrees exactly with witness_live.h's own HUNTING/FRENZIED gate\n");
}

static void test_meta_witness_rank_counts_corroborating_channels(void) {
    assert(avian_live_meta_witness_rank(0, WS_UNAWARE, ZOMBIE_MOOD_DORMANT) == 0);
    assert(avian_live_meta_witness_rank(AVIAN_OBSERVED_VIGILANCE_THRESHOLD, WS_UNAWARE, ZOMBIE_MOOD_DORMANT) == 1);
    assert(avian_live_meta_witness_rank(AVIAN_OBSERVED_VIGILANCE_THRESHOLD, WS_COMPROMISED, ZOMBIE_MOOD_DORMANT) == 2);
    assert(avian_live_meta_witness_rank(AVIAN_OBSERVED_VIGILANCE_THRESHOLD, WS_COMPROMISED, ZOMBIE_MOOD_FRENZIED) == 3);
    printf("PASS: meta_witness_rank genuinely counts how many of the three observation channels corroborate (0..3)\n");
}

static void test_alerted_coalition_reaches_signaling_when_flocked(void) {
    /* Full, real integration: a meta-witness alert (an NPC spike) plus real flock coordination
       (matching avian_values_test.c's own coordinated-flock pattern) genuinely carries a bird all
       the way to SIGNALING -- the alert this header produces is not a dead end, it feeds the same
       real state machine avian_values.c already proved. */
    AvianState a;
    avian_state_init(&a, 0);
    uint32_t t = 0;
    for (int i = 0; i < 10; i++) {
        t += 1000;
        avian_live_observe_npc_vigilance(&a, 90, t); /* sustained real observation each tick */
        avian_tick(&a, t, 1);                        /* real, sustained single flock peer */
    }
    assert(a.mood == AVIAN_MOOD_SIGNALING || a.mood == AVIAN_MOOD_MOBBING);
    printf("PASS: a bird alerted purely by observing another AI's own attention spike genuinely reaches SIGNALING with real flock support\n");
}

int main(void) {
    test_below_vigilance_threshold_does_not_alert();
    test_npc_vigilance_spike_alerts_the_coalition();
    test_the_men_high_baseline_alerts_more_readily_than_citizen();
    test_unaware_and_denial_do_not_alert();
    test_compromised_and_beyond_alert_the_coalition();
    test_zombie_event_channel_matches_witness_live_gate();
    test_meta_witness_rank_counts_corroborating_channels();
    test_alerted_coalition_reaches_signaling_when_flocked();
    printf("\nALL PASS\n");
    return 0;
}
