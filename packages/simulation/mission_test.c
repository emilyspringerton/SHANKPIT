/* mission_test.c -- replays BIG_O scenarios/20_a1m1_clean_run.txt and 21_a1m1_supervisor_returns.txt
 * against the SHANKPIT port of mission.c (BIG_O engine merge). Plain assert() harness.
 *
 *   gcc -Wall -Wextra -O2 -o /tmp/mission_test packages/simulation/mission_test.c \
 *       packages/simulation/mission.c packages/simulation/witness_sim.c packages/simulation/witness_rules.c
 */
#include "mission.h"
#include <assert.h>
#include <stdio.h>

static void setup(WitnessSim *s, Mission *m) {
    witness_sim_init(s, 1, 1, NULL);
    witness_sim_add_npc(s, ZONE_LAB, 10, 50);
    witness_sim_add_npc(s, ZONE_GENERATOR, 30, 50);
    s->p[0].present = 1; s->p[0].zone = ZONE_GENERATOR; s->p[0].costume = COS_JANITOR;
    mission_start(m);
}

int main(void) {
    WitnessSim s; Mission m;

    /* 20: clean run */
    setup(&s, &m);
    assert(mission_phase(&m) == 1);
    assert(mission_surf(&m, &s, 0) == -1);           /* not in Sector-2 yet */
    assert(mission_acquire_smock(&m, &s, 0) == 0);
    assert(mission_enter_sector2(&m, &s, 0) == 0);
    assert(mission_phase(&m) == 2);
    assert(s.p[0].decorum >= 60);
    int surfed = -1;
    for (int tries = 0; tries < 20 && surfed != 0; tries++) surfed = mission_surf(&m, &s, 0);
    assert(surfed == 0 && m.pin_known);
    assert(mission_insert_drive(&m, &s, 0) == -1);   /* supervisor at the terminal */
    assert(mission_supervisor(&m, &s, 0) == 0);
    assert(mission_insert_drive(&m, &s, 0) == 0);
    mission_tick(&m, &s, 5);
    assert(m.transfer_pct == 50);
    assert(mission_swap_drive(&m, &s, 0) == -1);     /* transfer not finished */
    mission_tick(&m, &s, 5);
    assert(m.transfer_pct == 100);
    assert(mission_swap_drive(&m, &s, 0) == 0);
    assert(mission_phase(&m) == 3);
    assert(mission_grab_mop(&m, &s, 0) == 0);
    assert(mission_exit(&m, &s, 0) == 0);
    assert(mission_upload(&m, &s, 0) == 0);
    assert(m.state == M_COMPLETE);
    printf("PASS: a1m1 clean run -> COMPLETE\n");

    /* 21: supervisor returns to a foreign drive -> FAILED */
    setup(&s, &m);
    mission_acquire_smock(&m, &s, 0); mission_enter_sector2(&m, &s, 0);
    for (int tries = 0; tries < 20 && !m.pin_known; tries++) mission_surf(&m, &s, 0);
    assert(m.pin_known);
    mission_supervisor(&m, &s, 0);
    assert(mission_insert_drive(&m, &s, 0) == 0);
    mission_supervisor(&m, &s, 1);
    mission_tick(&m, &s, 1);
    assert(m.state == M_FAILED);
    printf("PASS: a1m1 supervisor returns -> FAILED\n");

    /* 22: window closes */
    setup(&s, &m);
    mission_tick(&m, &s, M_WINDOW_MIN);
    assert(m.state == M_FAILED);
    printf("PASS: a1m1 window closes -> FAILED\n");

    printf("ALL PASS\n");
    return 0;
}
