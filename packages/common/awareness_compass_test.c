/* awareness_compass_test.c -- real, direct coverage for awareness_compass.h's own direction/
 * compass/intensity math (BIG_O engine merge phase 8, BIG_O/NORTHSTAR.md §35). Pure math, no
 * network/GL -- same discipline pheromone_test.c already establishes. Direction/compass cases are
 * a faithful port of BIG_O's own real bigo_awareness_test.c; intensity cases are new, matching
 * this header's own simpler single-arg clamp (see awareness_compass.h's doc comment on why the
 * witness-count scaling formula wasn't ported).
 *
 * Build:
 *   gcc -Wall -Wextra -O2 -o /tmp/awareness_compass_test packages/common/awareness_compass_test.c -lm \
 *       && /tmp/awareness_compass_test
 */
#include "awareness_compass.h"
#include <math.h>
#include <stdio.h>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
#define NEAR(a, b) (fabsf((a) - (b)) < 0.01f)

int main(void) {
    float dx, dz;

    /* direction: normalizes, preserves sign/ratio */
    awareness_direction(0.0f, 5.0f, &dx, &dz); CHECK(NEAR(dx, 0.0f) && NEAR(dz, 1.0f));
    awareness_direction(3.0f, 4.0f, &dx, &dz); CHECK(NEAR(dx, 0.6f) && NEAR(dz, 0.8f));   /* 3-4-5 triangle */
    awareness_direction(0.0f, 0.0f, &dx, &dz); CHECK(NEAR(dx, 0.0f) && NEAR(dz, 1.0f));   /* zero-length -> north, no NaN */

    /* compass: the 8 cardinal/ordinal directions land where the doc comment says */
    CHECK(awareness_compass(0.0f, 1.0f) == 0);    /* N */
    CHECK(awareness_compass(1.0f, 1.0f) == 1);    /* NE */
    CHECK(awareness_compass(1.0f, 0.0f) == 2);    /* E */
    CHECK(awareness_compass(1.0f, -1.0f) == 3);   /* SE */
    CHECK(awareness_compass(0.0f, -1.0f) == 4);   /* S */
    CHECK(awareness_compass(-1.0f, -1.0f) == 5);  /* SW */
    CHECK(awareness_compass(-1.0f, 0.0f) == 6);   /* W */
    CHECK(awareness_compass(-1.0f, 1.0f) == 7);   /* NW */
    CHECK(awareness_compass(0.0f, 0.0f) >= 0 && awareness_compass(0.0f, 0.0f) < 8);  /* degenerate input stays in range */

    /* intensity: a straight 0..100 clamp (see header doc comment -- no witness-count input here) */
    CHECK(awareness_intensity(-10) == 0);    /* clamped low */
    CHECK(awareness_intensity(200) == 100);  /* clamped high */
    CHECK(awareness_intensity(40) == 40);    /* mid-range: unchanged */
    CHECK(awareness_intensity(0) == 0);
    CHECK(awareness_intensity(100) == 100);

    if (fails == 0) printf("awareness_compass_test: all checks passed\n");
    else printf("awareness_compass_test: %d FAILURES\n", fails);
    return fails != 0;
}
