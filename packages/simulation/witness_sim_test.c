/* witness_sim_test.c -- real tests for witness_sim.c (BIG_O engine merge, phase 2), replaying
 * three of BIG_O's own canonical scenarios (scenarios/01_lone_witness_denial.txt,
 * 02_five_witness_silencing.txt, 04_decorum_is_per_player.txt) as direct C assertions, so this
 * port is checked against BIG_O's own real, already-proven expected outcomes rather than just
 * "it compiles." Plain assert() harness, same convention as day_night_clock_test.c.
 *
 * Build and run:
 *   gcc -Wall -Wextra -O2 -o /tmp/witness_sim_test packages/simulation/witness_sim_test.c \
 *       packages/simulation/witness_sim.c packages/simulation/witness_rules.c && /tmp/witness_sim_test
 */
#include "witness_sim.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    /* scenario 01_lone_witness_denial: one witness cannot acknowledge what they saw --
       catatonic denial, no consequence for the crew. */
    {
        WitnessSim s;
        witness_sim_init(&s, 1, 1, stdout);
        int npc = witness_sim_add_npc(&s, ZONE_PUBLIC, 0, 50);
        assert(npc == 0);
        witness_sim_release(&s, 0, 0);
        assert(s.n[0].state == WS_DENIAL);
        assert(s.p[0].hunted == 0);
        assert(s.p[0].decorum == 80);
        assert(witness_sim_decorum_band(s.p[0].decorum) == BAND_OK);
        printf("PASS: scenario 01 -- lone witness -> DENIAL, no consequence\n");
    }

    /* scenario 02_five_witness_silencing: five witnesses can no longer deny it -- aggressive
       silencing, hunt the attributed player. */
    {
        WitnessSim s;
        witness_sim_init(&s, 1, 1, stdout);
        for (int i = 0; i < 5; i++) assert(witness_sim_add_npc(&s, ZONE_PUBLIC, 0, 50) == i);
        witness_sim_release(&s, 0, 1);
        assert(s.n[0].state == WS_SILENCING);
        assert(s.n[4].state == WS_SILENCING);
        assert(s.p[0].hunted == 1);
        assert(s.p[0].decorum == 40);
        assert(witness_sim_decorum_band(s.p[0].decorum) == BAND_SUSPICION);
        printf("PASS: scenario 02 -- five witnesses -> SILENCING, hunted, decorum hit\n");
    }

    /* scenario 04_decorum_is_per_player: one player talks about the apocalypse until cancelled;
       their crewmate's standing is untouched. */
    {
        WitnessSim s;
        witness_sim_init(&s, 1, 2, stdout);
        witness_sim_add_npc(&s, ZONE_PUBLIC, 100, 50);
        witness_sim_say_apocalypse(&s, 0);
        assert(s.p[0].decorum == 55);
        assert(witness_sim_decorum_band(s.p[0].decorum) == BAND_SUSPICION);
        witness_sim_say_apocalypse(&s, 0);
        assert(s.p[0].decorum == 30);
        witness_sim_say_apocalypse(&s, 0);
        assert(s.p[0].decorum == 5);
        assert(witness_sim_decorum_band(s.p[0].decorum) == BAND_HYSTERIC);
        witness_sim_say_apocalypse(&s, 0);
        assert(s.p[0].decorum == 0);
        assert(witness_sim_decorum_band(s.p[0].decorum) == BAND_CANCELLED);
        assert(s.p[0].cancelled == 1);
        assert(s.p[1].decorum == 80);
        assert(witness_sim_decorum_band(s.p[1].decorum) == BAND_OK);
        assert(s.p[1].cancelled == 0);
        printf("PASS: scenario 04 -- decorum is per-player, crewmate untouched\n");
    }

    /* real, honest host-wiring behavior BIG_O's own scenarios also cover: SILENCING persists
       through a lost line of sight (resolved=0), and only resolves on a real memory wipe. */
    {
        WitnessSim s;
        witness_sim_init(&s, 1, 1, stdout);
        for (int i = 0; i < 5; i++) witness_sim_add_npc(&s, ZONE_PUBLIC, 0, 50);
        witness_sim_release(&s, 0, 1);
        assert(s.n[0].state == WS_SILENCING);
        witness_sim_los_lost(&s);
        assert(s.n[0].state == WS_SILENCING); /* a hunt never ends just because sight is lost */
        witness_sim_memory_wipe(&s, ZONE_PUBLIC);
        assert(s.n[0].state == WS_DENIAL); /* only a real memory wipe resolves it */
        printf("PASS: SILENCING persists through loslost, only a memory wipe resolves it\n");
    }

    /* name helpers never return null / handle real enum range. */
    {
        assert(strcmp(witness_sim_ws_name(WS_SILENCING), "SILENCING") == 0);
        assert(strcmp(witness_sim_band_name(BAND_HYSTERIC), "HYSTERIC") == 0);
        assert(strcmp(witness_sim_zone_name(ZONE_VAULT), "vault") == 0);
        assert(strcmp(witness_sim_costume_name(COS_JANITOR), "janitor") == 0);
        printf("PASS: name helpers\n");
    }

    printf("ALL PASS\n");
    return 0;
}
