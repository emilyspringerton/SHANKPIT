/* telecrystal_test.c -- the cast state machine over the shared rules, with the real city.wmap crystals.   make test-telecrystal */
#include "telecrystal.h"
#include <stdio.h>
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

int main(void) {
    /* TOWN_TO_MINES from data/maps/city.wmap: ring at (270,60) r14, cast 1000 total / 600 commit */
    TcCrystal c[1] = {{270, 60, 14, 1000, 600}};
    TcState s; int jump = -1;

    tc_init(&s);
    CHECK(tc_near(c, 1, 270, 60) == 0 && tc_near(c, 1, 284.5f, 60) == -1);
    CHECK(tc_tick(&s, c, 1, 0, 0, 1, 1000, &jump) == TC_NONE);            /* G far away: nothing */
    CHECK(tc_tick(&s, c, 1, 270, 60, 0, 1000, &jump) == TC_NONE);          /* in the ring, no G: nothing */

    /* clean cast: G, wait, the jump fires AT the commit time, then the cast winds down at total */
    CHECK(tc_tick(&s, c, 1, 270, 60, 1, 2000, &jump) == TC_STARTED);
    CHECK(tc_tick(&s, c, 1, 270, 60, 0, 2599, &jump) == TC_NONE);
    CHECK(tc_tick(&s, c, 1, 270, 60, 0, 2600, &jump) == TC_JUMP && jump == 0);
    CHECK(tc_tick(&s, c, 1, 270, 60, 0, 2900, &jump) == TC_NONE);
    CHECK(tc_tick(&s, c, 1, 270, 60, 0, 3000, &jump) == TC_FINISHED && s.phase == TC_IDLE);

    /* a G within 250 ms of a jump is not a new cast (cast ends 100 ms after commit here); 250 ms on, it is */
    {
        TcCrystal q[1] = {{270, 60, 14, 700, 600}};
        tc_init(&s);
        CHECK(tc_tick(&s, q, 1, 270, 60, 1, 10000, &jump) == TC_STARTED);
        CHECK(tc_tick(&s, q, 1, 270, 60, 0, 10600, &jump) == TC_JUMP);
        CHECK(tc_tick(&s, q, 1, 270, 60, 0, 10700, &jump) == TC_FINISHED);
        CHECK(tc_tick(&s, q, 1, 270, 60, 1, 10800, &jump) == TC_NONE);
        CHECK(tc_tick(&s, q, 1, 270, 60, 1, 10850, &jump) == TC_STARTED);
    }

    /* interrupt: step out (past the 1.5 slack) before the commit */
    tc_init(&s);
    CHECK(tc_tick(&s, c, 1, 270, 60, 1, 5000, &jump) == TC_STARTED);
    CHECK(tc_tick(&s, c, 1, 285.4f, 60, 0, 5100, &jump) == TC_NONE);         /* 15.4 < 15.5: inside the slack, still casting */
    CHECK(tc_tick(&s, c, 1, 285.6f, 60, 0, 5200, &jump) == TC_INTERRUPTED && s.phase == TC_IDLE);

    /* after the commit, leaving the ring no longer matters (the jump already happened) */
    tc_init(&s);
    CHECK(tc_tick(&s, c, 1, 270, 60, 1, 8000, &jump) == TC_STARTED);
    CHECK(tc_tick(&s, c, 1, 270, 60, 0, 8600, &jump) == TC_JUMP);
    CHECK(tc_tick(&s, c, 1, 400, 400, 0, 8700, &jump) == TC_NONE && s.phase == TC_CASTING);

    if (g_fail) { printf("test-telecrystal: %d FAILED\n", g_fail); return 1; }
    printf("test-telecrystal: ok\n");
    return 0;
}
