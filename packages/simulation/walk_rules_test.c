/* walk_rules_test.c -- the movement half of the NOOBTOPIA three-way merge, headless.
 *
 * What this pins:
 *   * PARITY -- every row of ../PARENA/stdlib/noobtopia/walk_parity.txt, the same file NOOBTOPIA's
 *     tests/walk_test.lua asserts, so the C and the Luau port cannot drift without one going red;
 *   * the compass: 8 headings, all reachable, every heading a real unit vector;
 *   * the moods a designer would notice: a STARTLED noob is frozen (not merely slow) and stands still longest,
 *     a CURIOUS one pauses least and roams furthest, a TIRED one shuffles;
 *   * the leash holds over a long run, for every mood, including diagonals;
 *   * a leg terminates (the walk cannot creep forever at a fraction of a unit per tick);
 *   * same seed -> same path.
 *
 * make test-walk-rules
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "walk_rules.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static const char *parity_path(void) {
    const char *dir = getenv("PARENA_DIR");
    static char buf[1024];
    if (dir && *dir) snprintf(buf, sizeof buf, "%s/stdlib/noobtopia/walk_parity.txt", dir);
    else snprintf(buf, sizeof buf, "%s", "../PARENA/stdlib/noobtopia/walk_parity.txt");
    return buf;
}

/* Run one agent. Returns how far from home it ever got, and how many legs it took. */
static int run_one(int mood, int seed, int leash, int ticks, int *legs_out, WalkAgent *out) {
    WalkAgent a;
    walk_agent_init(&a, 0, 0, mood, seed, leash);
    int furthest = 0, legs = 0, was_walking = 0;
    for (int i = 0; i < ticks; i++) {
        (void)walk_agent_tick(&a);
        int d = abs(a.x) > abs(a.y) ? abs(a.x) : abs(a.y);
        if (d > furthest) furthest = d;
        CHECK(walk_within_leash(a.x, a.y, leash) == 1);
        if (a.walking && !was_walking) legs++;
        was_walking = a.walking;
    }
    if (legs_out) *legs_out = legs;
    if (out) *out = a;
    return furthest;
}

int main(void) {
    /* ---- 1. parity vectors: the same file the Luau test reads --------------------------------------------- */
    FILE *f = fopen(parity_path(), "r");
    if (!f) {
        printf("SKIP: no parity vectors at %s (a standalone SHANKPIT checkout; set PARENA_DIR)\n", parity_path());
    } else {
        char line[512];
        int rows = 0;
        while (fgets(line, sizeof line, f)) {
            if (line[0] == '#') continue;
            int mood, since, seed, idle, choose, heading, leg, speed, tdx, tdy;
            if (sscanf(line, "%d %d %d %d %d %d %d %d %d %d",
                       &mood, &since, &seed, &idle, &choose, &heading, &leg, &speed, &tdx, &tdy) != 10)
                continue;
            rows++;
            CHECK(walk_idle_ticks(mood, seed) == idle);
            CHECK(walk_should_choose(mood, since, seed) == choose);
            CHECK(walk_heading(seed) == heading);
            CHECK(walk_leg_length(mood) == leg);
            CHECK(walk_mood_speed_permille(mood) == speed);
            CHECK(walk_heading_dx(heading) == tdx);
            CHECK(walk_heading_dy(heading) == tdy);
        }
        fclose(f);
        CHECK(rows >= 12);
        printf("walk: %d parity vectors from PARENA\n", rows);
    }

    /* ---- 2. the compass ----------------------------------------------------------------------------------- */
    for (int seed = 0; seed <= 500; seed++) {
        int h = walk_heading(seed);
        CHECK(h >= 0 && h < walk_heading_count());
    }
    CHECK(walk_heading(-1) == 0);
    CHECK(walk_heading_count() == 8);

    int seen_heading[8] = { 0 };
    for (int seed = 0; seed <= 500; seed++) seen_heading[walk_heading(seed)] = 1;
    for (int h = 0; h < 8; h++) CHECK(seen_heading[h]);

    for (int h = 0; h < walk_heading_count(); h++) {
        int dx = walk_heading_dx(h), dy = walk_heading_dy(h);
        CHECK(dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1);
        CHECK(!(dx == 0 && dy == 0));
    }

    /* ---- 3. the moods a designer would notice -------------------------------------------------------------- */
    CHECK(walk_mood_speed_permille(WALK_STARTLED) == 0);
    CHECK(walk_idle_ticks(WALK_STARTLED, 1234) > walk_idle_ticks(WALK_IDLE, 1234));
    CHECK(walk_idle_ticks(WALK_CURIOUS, 1234) < walk_idle_ticks(WALK_IDLE, 1234));
    CHECK(walk_leg_length(WALK_CURIOUS) > walk_leg_length(WALK_IDLE));
    CHECK(walk_leg_length(WALK_TIRED) < walk_leg_length(WALK_IDLE));
    CHECK(walk_should_choose(0, -5, 5) == 0);
    CHECK(walk_should_choose(0, walk_idle_ticks(0, 5), 5) == 1);

    /* ---- 4. the leash ------------------------------------------------------------------------------------- */
    CHECK(walk_within_leash(0, 0, 0) == 1);
    CHECK(walk_within_leash(3, 4, 5) == 1);
    CHECK(walk_within_leash(4, 4, 20) == 1);
    CHECK(walk_within_leash(21, 0, 20) == 0);
    CHECK(walk_within_leash(-1, -1, -1) == 0);

    /* ---- 5. wandering: it terminates, it stays home, and it is repeatable ---------------------------------- */
    const int moods[] = { WALK_IDLE, WALK_CURIOUS, WALK_BORED, WALK_TIRED };
    for (int i = 0; i < 4; i++) {
        int legs = 0;
        int furthest = run_one(moods[i], 4242, 40, 3000, &legs, 0);
        CHECK(legs >= 2);
        CHECK(furthest > 0);
    }

    /* a startled noob never takes a step, over a long run, even though it keeps choosing legs: the state both
     * worlds share, and the reason tick() returns "stepped" rather than "is walking" */
    {
        WalkAgent a;
        walk_agent_init(&a, 0, 0, WALK_STARTLED, 99, 40);
        int stepped_any = 0, chose_any = 0;
        for (int i = 0; i < 5000; i++) {
            if (walk_agent_tick(&a)) stepped_any = 1;
            if (a.walking) chose_any = 1;
            CHECK(a.x == 0 && a.y == 0);
        }
        CHECK(!stepped_any);
        CHECK(chose_any);
    }

    /* a tired noob covers less ground than a curious one over the same window (400 vs 1100 permille) */
    {
        WalkAgent tired, curious;
        run_one(WALK_TIRED, 777, 40, 600, 0, &tired);
        run_one(WALK_CURIOUS, 777, 40, 600, 0, &curious);
        int tired_steps = abs(tired.x) + abs(tired.y);
        int curious_steps = abs(curious.x) + abs(curious.y);
        CHECK(curious_steps > tired_steps);
        printf("walk: 600 ticks -- curious %d units, tired %d units\n", curious_steps, tired_steps);
    }

    /* same seed, same path */
    {
        WalkAgent a, b;
        walk_agent_init(&a, 0, 0, WALK_IDLE, 2026, 40);
        walk_agent_init(&b, 0, 0, WALK_IDLE, 2026, 40);
        for (int i = 0; i < 2000; i++) {
            walk_agent_tick(&a);
            walk_agent_tick(&b);
            if (a.x != b.x || a.y != b.y) { CHECK(0); break; }
        }
    }

    /* a leash shorter than the leg means it never moves -- declared, not a silent hang */
    {
        WalkAgent a;
        walk_agent_init(&a, 0, 0, WALK_IDLE, 5, 1);
        int moved_any = 0;
        for (int i = 0; i < 200; i++) if (walk_agent_tick(&a)) moved_any = 1;
        CHECK(!moved_any);
    }

    if (g_fail) { printf("walk_rules_test: %d FAILED\n", g_fail); return 1; }
    printf("walk_rules_test OK\n");
    return 0;
}
