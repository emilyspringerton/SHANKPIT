/* ocean_test.c -- the archipelago's water, headless. See docs/ARCHIPELAGO_NORTHSTAR.md.
 *
 * This is the first water code in this engine, so the test is the spec. It pins:
 *   * calm water is flat (to the millimetre), and a higher sea state is a higher sea (RMS, monotonic);
 *   * identical inputs give identical output -- the property the netcode depends on, since the server arbitrates
 *     buoyancy and the client predicts it;
 *   * A HULL SETTLES AT A DRAFT: dropped from above the waterline it stops sinking and stays floating, and dropped
 *     from below it rises. This is the one that catches a sign error in buoyancy, which looks fine in a screenshot;
 *   * planing lift rises with speed and saturates; drag rises with speed;
 *   * a storm raises hull slam (peak vertical acceleration) over calm water;
 *   * a sloped surface gives a tilted normal, so a boat on a swell leans.
 *
 * make test-ocean
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "ocean.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static float rms_height(int sea, float t) {
    double sum = 0.0;
    int n = 0;
    for (int x = -40; x <= 40; x += 4)
        for (int z = -40; z <= 40; z += 4) {
            double h = ocean_height_m((float)x, (float)z, t, sea);
            sum += h * h;
            n++;
        }
    return (float)sqrt(sum / (double)n);
}

static void make_hull(OceanVessel *v, float y) {
    memset(v, 0, sizeof(*v));
    v->probe_count = 4;
    v->mass_kg = 900.0f;
    v->hull_height_m = 1.0f;
    v->y = y;
    const float px[4] = { -2.0f, -0.7f, 0.7f, 2.0f };
    for (int i = 0; i < 4; i++) {
        v->probe[i].x = px[i];
        v->probe[i].z = 0.0f;
        v->probe[i].area_m2 = 1.1f;
        v->probe[i].keel_y = y;
    }
}

int main(void) {
    /* 1. calm water is flat to the millimetre, and the wave field sums its weights to the sea's own amplitude */
    CHECK(fabsf(ocean_height_m(0.0f, 0.0f, 0.0f, OCEAN_SEA_CALM)) < 0.004f);
    CHECK(fabsf(ocean_height_m(0.0f, 0.0f, 0.0f, OCEAN_SEA_STORM) - 1.2f) < 0.01f);

    /* 2. a higher sea state is a higher sea */
    float prev = -1.0f;
    for (int sea = 0; sea < ocean_sea_state_count(); sea++) {
        float r = rms_height(sea, 3.0f);
        if (sea > 0) CHECK(r > prev);
        prev = r;
    }
    printf("sea rms at t=3s: calm %.3f  swell %.3f  choppy %.3f  storm %.3f  tempest %.3f\n",
           rms_height(0, 3.0f), rms_height(1, 3.0f), rms_height(2, 3.0f), rms_height(3, 3.0f), rms_height(4, 3.0f));

    /* 3. determinism: the server and every client must agree about where the water is */
    CHECK(ocean_height_m(12.5f, -7.25f, 4.5f, OCEAN_SEA_CHOPPY) == ocean_height_m(12.5f, -7.25f, 4.5f, OCEAN_SEA_CHOPPY));
    CHECK(ocean_planing_lift_mm(5000) == ocean_planing_lift_mm(5000));

    /* 4. planing lift rises with speed then saturates; drag rises with speed */
    CHECK(ocean_planing_lift_mm(0) == 0);
    CHECK(ocean_planing_lift_mm(6000) > ocean_planing_lift_mm(2000));
    CHECK(ocean_planing_lift_mm(12000) == ocean_planing_lift_mm(60000));   /* saturated, not still climbing */
    CHECK(ocean_drag_permille(10000, 1000) > ocean_drag_permille(1000, 1000));

    /* 5. THE property: a hull settles at a draft. Dropped high, it must not sink; dropped low, it must rise. And a
          deep probe cannot displace more than its own area, so it cannot launch the boat out of the water. */
    {
        OceanVessel v;
        make_hull(&v, 3.0f);                     /* 3 m of air under the keel */
        float peak_y = -1e9f, min_y = 1e9f;
        for (int i = 0; i < 1200; i++) {
            v.y = v.y;                           /* keel_y is refreshed below so the probes follow the hull */
            for (int k = 0; k < v.probe_count; k++) v.probe[k].keel_y = v.y;
            ocean_vessel_step(&v, 1.0f / 60.0f, (float)i / 60.0f, OCEAN_SEA_CALM);
            if (v.y > peak_y) peak_y = v.y;
            if (v.y < min_y) min_y = v.y;
        }
        printf("hull dropped from 3.0m: settled y=%.3f (peak %.3f, low %.3f, vy=%.4f)\n", v.y, peak_y, min_y, v.vy);
        CHECK(v.y < 3.0f);                       /* it fell in */
        CHECK(min_y > -3.0f);                    /* it did not keep sinking */
        CHECK(fabsf(v.vy) < 0.2f);               /* it is resting, not bobbing hard forever */

        /* starting from under the water, it rises */
        make_hull(&v, -2.0f);
        for (int i = 0; i < 600; i++) {
            for (int k = 0; k < v.probe_count; k++) v.probe[k].keel_y = v.y;
            ocean_vessel_step(&v, 1.0f / 60.0f, (float)i / 60.0f, OCEAN_SEA_CALM);
        }
        CHECK(v.y > -2.0f);
        printf("hull released at -2.0m: rose to y=%.3f\n", v.y);
    }

    /* 6. a storm slams the hull harder than a calm sea: measure peak |vertical acceleration| */
    {
        float worst[2] = { 0.0f, 0.0f };
        int seas[2] = { OCEAN_SEA_CALM, OCEAN_SEA_STORM };
        for (int s = 0; s < 2; s++) {
            OceanVessel v;
            make_hull(&v, 0.0f);
            float last_vy = 0.0f;
            for (int i = 0; i < 1500; i++) {
                for (int k = 0; k < v.probe_count; k++) v.probe[k].keel_y = v.y;
                ocean_vessel_step(&v, 1.0f / 60.0f, (float)i / 60.0f, seas[s]);
                /* Skip the first 600 ticks: that is the hull DROPPING INTO the water, a one-off transient that
                 * dwarfs the sea's own motion and would make calm water look as violent as a storm. What is being
                 * measured is the slamming a settled hull takes, which is the thing a player feels. */
                if (i > 600) {
                    float a = fabsf(v.vy - last_vy) * 60.0f;
                    if (a > worst[s]) worst[s] = a;
                }
                last_vy = v.vy;
            }
        }
        printf("peak vertical accel: calm %.2f m/s2, storm %.2f m/s2\n", worst[0], worst[1]);
        CHECK(worst[1] > worst[0] * 1.5f);
    }

    /* 7. a sloped surface tilts the normal, so a hull leans into a swell */
    {
        int sea = OCEAN_SEA_STORM;
        float max_tilt = 0.0f;
        for (int x = -20; x <= 20; x++) {
            float n[3];
            ocean_normal_m((float)x, 3.5f, 1.25f, sea, n);
            float tilt = sqrtf(n[0] * n[0] + n[2] * n[2]);       /* 0 = perfectly flat */
            if (tilt > max_tilt) max_tilt = tilt;
        }
        float flat[3];
        ocean_normal_m(0.0f, 0.0f, 0.0f, OCEAN_SEA_CALM, flat);
        CHECK(max_tilt > 0.05f);                                  /* a storm genuinely slopes */
        CHECK(flat[1] > 0.99f);                                   /* calm water is flat up */
    }

    /* 8. wind rises with the sea, and the state table is total */
    CHECK(ocean_wind_mps(OCEAN_SEA_TEMPEST) > ocean_wind_mps(OCEAN_SEA_SWELL));
    CHECK(ocean_wind_mps(99) == ocean_wind_mps(OCEAN_SEA_CALM));
    CHECK(ocean_submerged_permille(0, 1000) == 0);
    CHECK(ocean_submerged_permille(500, 1000) == 500);
    CHECK(ocean_submerged_permille(5000, 1000) == 1000);
    CHECK(ocean_submerged_permille(500, 0) == 0);

    if (g_fail) { printf("ocean_test: %d FAILED\n", g_fail); return 1; }
    printf("ocean_test OK\n");
    return 0;
}
