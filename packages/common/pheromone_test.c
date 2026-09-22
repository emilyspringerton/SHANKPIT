/* pheromone_test.c -- real tests for pheromone.h (BIG_O engine merge, phase 5), a faithful port
 * of BIG_O's own real bigo_pheromone_test.c (8 tests). Plain assert() harness, same convention.
 *
 * Build and run:
 *   gcc -Wall -Wextra -O2 -o /tmp/pheromone_test packages/common/pheromone_test.c -lm \
 *       && /tmp/pheromone_test
 */
#include "pheromone.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    /* expire: only deactivates markers whose expiry has actually passed. */
    {
        PheromoneMarker m[PHEROMONE_MAX] = {0};
        m[0] = (PheromoneMarker){.active = 1, .x = 1, .z = 1, .expires_at_ms = 1000};
        m[1] = (PheromoneMarker){.active = 1, .x = 2, .z = 2, .expires_at_ms = 5000};
        pheromone_marker_expire(m, PHEROMONE_MAX, 2000);
        assert(m[0].active == 0);
        assert(m[1].active == 1);
        printf("PASS: pheromone_marker_expire deactivates only past markers\n");
    }

    /* claim_slot: prefers a free slot over evicting anything. */
    {
        PheromoneMarker m[PHEROMONE_MAX] = {0};
        m[0].active = 1; m[0].expires_at_ms = 1000;
        int slot = pheromone_claim_slot(m, PHEROMONE_MAX);
        assert(slot == 1);
        printf("PASS: pheromone_claim_slot prefers a free slot\n");
    }

    /* claim_slot: when every slot is full, evicts the soonest-expiring one. */
    {
        PheromoneMarker m[PHEROMONE_MAX];
        for (int i = 0; i < PHEROMONE_MAX; i++) m[i] = (PheromoneMarker){.active = 1, .expires_at_ms = (unsigned)(5000 + i * 1000)};
        m[2].expires_at_ms = 1500; /* soonest to expire */
        int slot = pheromone_claim_slot(m, PHEROMONE_MAX);
        assert(slot == 2);
        printf("PASS: pheromone_claim_slot evicts the soonest-expiring marker when full\n");
    }

    /* find_nearest: returns the nearest active marker within radius, fills the out params. */
    {
        PheromoneMarker m[PHEROMONE_MAX] = {0};
        m[0] = (PheromoneMarker){.active = 1, .x = 10, .z = 0, .expires_at_ms = 9999};
        m[1] = (PheromoneMarker){.active = 1, .x = 2, .z = 0, .expires_at_ms = 9999};
        float tx = 0, tz = 0;
        int found = pheromone_find_nearest(m, PHEROMONE_MAX, 0, 0, 20.0f, &tx, &tz);
        assert(found == 1);
        assert(tx == 2.0f && tz == 0.0f); /* nearer of the two */
        printf("PASS: pheromone_find_nearest picks the nearest active marker within radius\n");
    }

    /* find_nearest: ignores inactive markers and ones outside the detection radius. */
    {
        PheromoneMarker m[PHEROMONE_MAX] = {0};
        m[0] = (PheromoneMarker){.active = 0, .x = 1, .z = 0, .expires_at_ms = 9999}; /* inactive, closest */
        m[1] = (PheromoneMarker){.active = 1, .x = 100, .z = 0, .expires_at_ms = 9999}; /* out of range */
        float tx = 0, tz = 0;
        int found = pheromone_find_nearest(m, PHEROMONE_MAX, 0, 0, 20.0f, &tx, &tz);
        assert(found == 0);
        printf("PASS: pheromone_find_nearest ignores inactive/out-of-range markers\n");
    }

    /* step_toward: moves at exactly speed*dt without overshoot, reports arrival at the exact target. */
    {
        float x = 0, z = 0;
        int arrived = pheromone_step_toward(&x, &z, 10.0f, 0.0f, 2.0f, 1.0f); /* 2 units/sec for 1s */
        assert(arrived == 0);
        assert(x > 1.99f && x < 2.01f);
        printf("PASS: pheromone_step_toward moves at speed*dt without overshoot\n");
    }

    /* step_toward: a large step that would overshoot instead lands exactly on the target. */
    {
        float x = 0, z = 0;
        int arrived = pheromone_step_toward(&x, &z, 5.0f, 0.0f, 100.0f, 1.0f); /* way more than needed */
        assert(arrived == 1);
        assert(x == 5.0f && z == 0.0f);
        printf("PASS: pheromone_step_toward never overshoots -- lands exactly on target\n");
    }

    /* step_toward: diagonal direction is correct (normalized toward the real target vector). */
    {
        float x = 0, z = 0;
        pheromone_step_toward(&x, &z, 3.0f, 4.0f, 5.0f, 1.0f); /* 3-4-5 triangle, exactly one step to arrival */
        assert(x > 2.99f && x < 3.01f);
        assert(z > 3.99f && z < 4.01f);
        printf("PASS: pheromone_step_toward's diagonal direction is correct\n");
    }

    printf("ALL PASS\n");
    return 0;
}
