/* cozy_test.c -- the cosy verbs: the shared fixture, and the host's own arithmetic.
 *
 * Two jobs, the same two every merge slice has:
 *   1. PARITY against PARENA/stdlib/shankpit/cozy_parity.txt, which NOOBTOPIA's tests/cozy_test.lua asserts too.
 *   2. BEHAVIOUR: a dandelion is only kickable by day and off cooldown, a tree only if it is thick enough, the
 *      same tree gives less each time in one day, and comfort never overshoots or exceeds its bounds.
 *
 * Run: make test-cozy        (PARENA_DIR overrides where the fixture is read from)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cozy.h"

static int g_checks = 0;
static int g_fails = 0;

static void expect(int got, int want, const char *what) {
    g_checks++;
    if (got != want) {
        g_fails++;
        printf("FAIL: %s (got %d want %d)\n", what, got, want);
    }
}

static void expect_true(int cond, const char *what) {
    g_checks++;
    if (!cond) { g_fails++; printf("FAIL: %s\n", what); }
}

static int fixture(void) {
    static char path[512];
    const char *dir = getenv("PARENA_DIR");
    if (dir && *dir) snprintf(path, sizeof path, "%s/stdlib/shankpit/cozy_parity.txt", dir);
    else snprintf(path, sizeof path, "../PARENA/stdlib/shankpit/cozy_parity.txt");

    FILE *f = fopen(path, "r");
    if (!f) {
        printf("FAIL: cannot open %s (set PARENA_DIR to the PARENA checkout)\n", path);
        return 0;
    }
    char line[512];
    int rows = 0;
    while (fgets(line, sizeof line, f)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        /* A row is: question a b c | answer -- FIVE integers. The reader once demanded six and silently asserted
         * zero rows while reporting a green-looking check count; the "read at least 20" check is what caught it,
         * which is why it exists. */
        int v[5];
        int n = 0;
        char *p = line;
        while (n < 5) {
            char *end = NULL;
            long got = strtol(p, &end, 10);
            if (end == p) break;
            v[n++] = (int)got;
            p = end;
        }
        if (n < 5) continue;
        rows++;
        int q = v[0], a = v[1], b = v[2], c = v[3];
        char tag[128];
        snprintf(tag, sizeof tag, "cozy row %d q=%d (%d,%d,%d)", rows, q, a, b, c);
        switch (q) {
        case 0: expect(cozy_kick_cooldown_ms(), v[4], tag); break;
        case 1: expect(cozy_kick_lift_milli(), v[4], tag); break;
        case 2: expect(cozy_kick_puffs(a), v[4], tag); break;
        case 3: expect(cozy_kick_allowed(a, b, c), v[4], tag); break;
        case 4: expect(cozy_hug_cooldown_ms(), v[4], tag); break;
        case 5: expect(cozy_hug_comfort_milli(), v[4], tag); break;
        case 6: expect(cozy_hug_allowed(a, b), v[4], tag); break;
        case 7: expect(cozy_hug_diminish(a), v[4], tag); break;
        case 8: expect(cozy_comfort_target(a), v[4], tag); break;
        case 9: expect(cozy_comfort_drift(a, b, c), v[4], tag); break;
        case 10: expect(cozy_line_count(), v[4], tag); break;
        case 11: expect(cozy_line_index(a, b), v[4], tag); break;
        case 12: expect(cozy_flavor_daylight(a), v[4], tag); break;
        case 13: expect(cozy_trunk_huggable_milli(), v[4], tag); break;
        case 14: expect(cozy_daylight_milli(), v[4], tag); break;
        default: expect_true(0, "cozy row names a known question"); break;
        }
    }
    fclose(f);
    expect_true(rows >= 20, "read at least 20 cozy rows");
    return rows;
}

int main(void) {
    int rows = fixture();

    /* ---- kicking a dandelion ------------------------------------------------------------------- */
    CozyActor a;
    cozy_actor_init(&a, COZY_IDLE, 7);
    expect(a.comfort, cozy_comfort_target(COZY_IDLE), "an actor starts at its mood's comfort target");

    int puffs = cozy_try_kick(&a, 1, 1000, 1000);
    expect_true(puffs >= 3 && puffs <= 6, "a daylight kick gives 3..6 seeds");
    expect(puffs, cozy_kick_puffs(7), "and the seeds come from the actor's own seed");
    expect(a.comfort, cozy_comfort_target(COZY_IDLE) + cozy_kick_lift_milli(), "the kick lifts comfort");
    expect(a.kicks, 1, "and is counted");

    expect(cozy_try_kick(&a, 1, 1100, 1000), 0, "a second kick inside the cooldown is refused");
    expect_true(cozy_try_kick(&a, 1, 1000 + cozy_kick_cooldown_ms(), 1000) > 0,
                "after the cooldown it works again");
    expect(cozy_try_kick(&a, 0, 999999, 1000), 0, "something that is not a dandelion cannot be kicked");
    expect(cozy_try_kick(&a, 1, 99999999, 100), 0, "and at night the dandelion is closed");
    expect_true(cozy_try_kick(&a, 1, 99999999, cozy_daylight_milli()) > 0,
                "the daylight threshold is inclusive: at exactly 250 permille the dandelion is open");

    /* comfort is capped, not unbounded: a hundred dandelions do not make a noob blissful by arithmetic alone */
    CozyActor cap;
    cozy_actor_init(&cap, COZY_CURIOUS, 1);
    cap.comfort = 990;
    cozy_try_kick(&cap, 1, 1000, 1000);
    expect(cap.comfort, 1000, "comfort clamps at 1000");

    /* ---- hugging a tree ------------------------------------------------------------------------ */
    CozyActor h;
    cozy_actor_init(&h, COZY_TIRED, 3);
    expect(cozy_try_hug(&h, 100, 5000), 0, "a sapling is too thin to hug");
    expect(cozy_try_hug(&h, cozy_trunk_huggable_milli(), 5000), cozy_hug_comfort_milli(),
           "a trunk at the threshold is huggable at full worth");
    expect(cozy_try_hug(&h, 900, 5100), 0, "a second hug inside the cooldown is refused");
    int second = cozy_try_hug(&h, 900, 5000 + cozy_hug_cooldown_ms());
    expect(second, (cozy_hug_comfort_milli() * 700) / 1000, "the second hug of the day is worth less");
    expect(h.hugs_today, 2, "hugs are counted for the day");

    int at = 5000;
    for (int i = 2; i < 8; i++) {
        at += cozy_hug_cooldown_ms();
        cozy_try_hug(&h, 900, at);
    }
    int floor_gain = (cozy_hug_comfort_milli() * 200) / 1000;
    expect(cozy_try_hug(&h, 900, at + cozy_hug_cooldown_ms()), floor_gain, "the hundredth hug keeps a floor");
    cozy_new_day(&h);
    expect(h.hugs_today, 0, "morning resets the tree's count");
    expect(cozy_try_hug(&h, 900, at + 2 * cozy_hug_cooldown_ms()), cozy_hug_comfort_milli(),
           "so the first hug of a new day is worth full again");

    /* ---- comfort drifts, and never overshoots -------------------------------------------------- */
    CozyActor d;
    cozy_actor_init(&d, COZY_STARTLED, 0);
    d.comfort = 1000;
    cozy_tick(&d, 10000);
    expect(d.comfort, cozy_comfort_target(COZY_STARTLED), "a long tick lands exactly on the mood's target");
    d.comfort = 1000;
    cozy_tick(&d, 10);
    expect(d.comfort, 990, "a short tick moves one permille per tick");
    d.comfort = cozy_comfort_target(COZY_STARTLED);
    cozy_tick(&d, 500);
    expect(d.comfort, cozy_comfort_target(COZY_STARTLED), "drift does not overshoot its target");
    expect_true(cozy_comfort_drift(-50, 500, 10) >= 0, "a negative comfort is clamped into range");
    expect_true(cozy_comfort_drift(5000, 500, 10) <= 1000, "and so is one above the maximum");

    if (g_fails) {
        printf("cozy_test: %d of %d FAILED\n", g_fails, g_checks);
        return 1;
    }
    printf("cozy_test OK (%d checks, %d parity rows)\n", g_checks, rows);
    return 0;
}
