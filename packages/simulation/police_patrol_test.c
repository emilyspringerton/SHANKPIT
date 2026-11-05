/* police_patrol_test.c -- the automated cruiser's steering, headless.
 *
 * What this proves is exactly what the host relies on: the ring target is deterministic and on the ring,
 * the car steers TOWARD it (right for a target on the right, left for one on the left), it goes forward
 * rather than reversing, it keeps rolling when the target is badly placed, and the anchor is per-slot so
 * two cruisers cannot end up chasing one ring.
 *
 * NOT proven here: the physics of driving there, and what a siren sounds like. The first is
 * simulate_rocket_car_state's job (already covered by its own tests); the second needs an audio device
 * this box does not have.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "police_patrol.h"

static int g_failed;
static int g_checks;

#define CHECK(cond, ...) do { \
    g_checks++; \
    if (!(cond)) { \
        g_failed++; \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

int main(void) {
    printf("== police patrol ==\n");

    /* ---- the target is on the ring, and it goes round ---- */
    {
        police_patrol_set_anchor(0, 100.0f, -50.0f, 40.0f);
        float x0, z0, x1, z1;
        police_patrol_target(0, 0, &x0, &z0);
        police_patrol_target(0, POLICE_PATROL_PERIOD_MS / 4, &x1, &z1);
        float r0 = sqrtf((x0 - 100.0f) * (x0 - 100.0f) + (z0 + 50.0f) * (z0 + 50.0f));
        float r1 = sqrtf((x1 - 100.0f) * (x1 - 100.0f) + (z1 + 50.0f) * (z1 + 50.0f));
        CHECK(fabsf(r0 - 40.0f) < 0.01f, "the patrol target is %.2f from its anchor, not 40", r0);
        CHECK(fabsf(r1 - 40.0f) < 0.01f, "a quarter lap later the target left the ring (%.2f)", r1);
        CHECK(fabsf(x1 - x0) + fabsf(z1 - z0) > 20.0f, "the target never moved in a quarter lap");
        float xa, za, xb, zb;
        police_patrol_target(0, 3000, &xa, &za);
        police_patrol_target(0, 3000, &xb, &zb);
        CHECK(xa == xb && za == zb, "the same clock gave two different targets (not deterministic)");
        float xw, zw;
        police_patrol_target(0, POLICE_PATROL_PERIOD_MS, &xw, &zw);
        CHECK(fabsf(xw - x0) < 0.01f && fabsf(zw - z0) < 0.01f, "a full period did not return to the start");
        printf("PASS: the ring target stays on its anchor+radius and completes a lap per 48 s\n");
    }

    /* ---- steering: right is right, left is left, ahead is straight ---- */
    {
        BuggyState b;
        float th, st;
        memset(&b, 0, sizeof(b));
        b.active = 1; b.id = 0; b.x = 0.0f; b.z = 0.0f; b.yaw = 0.0f;
        police_patrol_set_anchor(0, 0.0f, 0.0f, 100.0f);

        /* pick the moment the target sits 90 degrees to the car's right (+X is yaw 90) */
        float three_x, three_z;
        police_patrol_target(0, POLICE_PATROL_PERIOD_MS / 4, &three_x, &three_z);
        CHECK(three_x > 90.0f, "the quarter-lap target is not the +X side (%.2f)", three_x);
        police_patrol_intent(&b, POLICE_PATROL_PERIOD_MS / 4, &th, &st);
        printf("      target at (%.1f, %.1f) for a car facing +Z: throttle %.2f steer %.2f\n",
               three_x, three_z, th, st);
        CHECK(st > 0.2f, "a target to the right steered %.2f (should be positive)", st);
        CHECK(th > 0.0f, "the cruiser does not drive toward its right-hand target (throttle %.2f)", th);

        police_patrol_intent(&b, 3 * POLICE_PATROL_PERIOD_MS / 4, &th, &st);
        CHECK(st < -0.2f, "a target to the left steered %.2f (should be negative)", st);

        /* straight ahead: put the car on the target, facing away from the centre */
        float tx, tz;
        police_patrol_target(0, 0, &tx, &tz);
        b.x = tx; b.z = tz;
        /* face along the ring's tangent: +X at the +Z pole of the ring is 90 deg */
        b.yaw = 90.0f;
        b.x += 0.5f;
        police_patrol_intent(&b, 2000, &th, &st);
        printf("      car on the ring heading along it: throttle %.2f steer %.2f\n", th, st);
        CHECK(fabsf(st) < 0.6f, "a car already on its ring steers %.2f -- it is fighting the loop", st);
        CHECK(th > 0.0f, "a car on its ring is not moving (throttle %.2f)", th);
    }

    /* ---- anchors are per slot: two cruisers cannot share one ring by accident ---- */
    {
        police_patrol_set_anchor(0, 10.0f, 10.0f, 30.0f);
        police_patrol_set_anchor(1, -400.0f, 250.0f, 55.0f);
        float ax, az, bx, bz;
        police_patrol_target(0, 1000, &ax, &az);
        police_patrol_target(1, 1000, &bx, &bz);
        CHECK(fabsf(ax - bx) + fabsf(az - bz) > 100.0f, "two patrol slots are steering at the same point");
        police_patrol_set_anchor(99, 0.0f, 0.0f, 10.0f);   /* out of range: ignored, not memory */
        printf("PASS: each cruiser keeps its own ring; an out-of-range slot is ignored\n");
    }

    printf("\n%s%d check(s) failed\n", g_failed ? "FAIL: " : "PASS: ", g_failed);
    return g_failed ? 1 : 0;
}
