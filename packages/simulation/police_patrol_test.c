/* police_patrol_test.c -- the automated cruiser's steering, headless.
 *
 * What this proves is exactly what the host relies on: the ring target is deterministic and on the ring, the
 * car steers TOWARD it (right for a target on the right, left for one on the left), it goes forward rather
 * than reversing, it keeps rolling when the target is badly placed, the anchor is per-slot so two cruisers
 * cannot end up chasing one ring, a lane follows the level's own waypoints, and a lane has a DIRECTION with
 * the car held KEEP_RIGHT of the centreline -- which is what lets the same authored nodes carry both
 * directions of a two-way street instead of a convoy down the middle.
 *
 * NOT proven here: the physics of driving there, and what a siren or a jingle sounds like. The first is
 * simulate_rocket_car_state's job (already covered by its own tests); the second needs an audio device this
 * box does not have, which is why the siren and song voices are rendered offline by test-siren-dsp and
 * test-icecream-dsp instead.
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

static float dist_to(float x, float z, float px, float pz) {
    return sqrtf((x - px) * (x - px) + (z - pz) * (z - pz));
}

int main(void) {
    printf("== police patrol ==\n");

    /* ---- the target is on the ring, and it goes round ---- */
    {
        police_patrol_set_anchor(0, 100.0f, -50.0f, 40.0f);
        float x0, z0, x1, z1;
        police_patrol_target(0, 0, &x0, &z0);
        police_patrol_target(0, POLICE_PATROL_PERIOD_MS / 4, &x1, &z1);
        float r0 = dist_to(x0, z0, 100.0f, -50.0f);
        float r1 = dist_to(x1, z1, 100.0f, -50.0f);
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

    /* ---- lanes: the level's own beat, with the ring as the fallback ---- */
    {
        float xs[4] = { 100.0f, 180.0f, 160.0f, 60.0f };
        float zs[4] = { 0.0f, 40.0f, 140.0f, 120.0f };
        police_patrol_set_anchor(0, 0.0f, 0.0f, 100.0f);
        police_patrol_set_lane(0, xs, zs, 4);
        CHECK(police_patrol_has_lane(0) == 4, "the lane did not take (has %d)", police_patrol_has_lane(0));
        float tx, tz;
        police_patrol_target(0, 0, &tx, &tz);
        float off = dist_to(tx, tz, xs[0], zs[0]);
        printf("      first target is %.2f from the authored waypoint (keep-right %.1f)\n", off,
               POLICE_PATROL_KEEP_RIGHT);
        CHECK(fabsf(off - POLICE_PATROL_KEEP_RIGHT) < 0.05f,
              "a lane's first target sits %.2f from the waypoint, not keep-right %.1f", off,
              POLICE_PATROL_KEEP_RIGHT);
        /* arriving at the waypoint advances the beat, and the last one wraps */
        BuggyState b;
        memset(&b, 0, sizeof(b));
        b.active = 1; b.id = 0;
        for (int wp = 0; wp < 4; wp++) {
            police_patrol_target(0, 0, &tx, &tz);
            b.x = tx; b.z = tz; b.yaw = 90.0f;
            float th, st;
            police_patrol_intent(&b, 0, &th, &st);
            CHECK(police_patrol_waypoint(0) == (wp + 1) % 4,
                  "reaching waypoint %d left the cruiser on %d", wp, police_patrol_waypoint(0));
        }
        printf("PASS: a cruiser follows the level's lane and wraps at the last waypoint\n");
    }

    /* ---- two-way lanes: direction, and the car keeps right of the centreline ---- */
    {
        float xs[4] = { 100.0f, 180.0f, 160.0f, 60.0f };
        float zs[4] = { 0.0f, 40.0f, 140.0f, 120.0f };
        police_patrol_set_lane(0, xs, zs, 4);
        police_patrol_set_direction(0, 1);
        CHECK(police_patrol_direction(0) == 1, "a fresh lane did not run forward (dir %d)",
              police_patrol_direction(0));
        float fx, fz, rx, rz;
        police_patrol_target(0, 0, &fx, &fz);
        police_patrol_set_direction(0, -1);
        CHECK(police_patrol_direction(0) == -1, "the reverse direction did not take (dir %d)",
              police_patrol_direction(0));
        police_patrol_target(0, 0, &rx, &rz);

        /* the offset is on OPPOSITE sides of the centreline for the two directions */
        float fdx = fx - xs[0], fdz = fz - zs[0];
        float rdx = rx - xs[0], rdz = rz - zs[0];
        float cross = fdx * rdz - fdz * rdx;   /* sign flips when the two offsets face opposite ways */
        printf("      keep-right: forward offset (%.2f,%.2f), reverse offset (%.2f,%.2f)\n", fdx, fdz, rdx, rdz);
        CHECK(fabsf(dist_to(fx, fz, xs[0], zs[0]) - POLICE_PATROL_KEEP_RIGHT) < 0.05f,
              "the forward keep-right offset is not one car width off the lane");
        CHECK(fabsf(dist_to(rx, rz, xs[0], zs[0]) - POLICE_PATROL_KEEP_RIGHT) < 0.05f,
              "the reverse keep-right offset is not one car width off the lane");
        CHECK(cross < -1.0f || cross > 1.0f, "both directions offset to the SAME side of the road");

        /* and reversing the beat walks the author's order backwards */
        BuggyState b;
        memset(&b, 0, sizeof(b));
        b.active = 1; b.id = 0; b.x = xs[0]; b.z = zs[0]; b.yaw = 90.0f;
        police_patrol_set_direction(0, -1);
        int start_wp = police_patrol_waypoint(0);
        for (int k = 0; k < 2; k++) {
            police_patrol_target(0, 0, &fx, &fz);
            b.x = fx; b.z = fz;
            float th, st;
            police_patrol_intent(&b, 0, &th, &st);
        }
        int expect = ((start_wp - 2) % 4 + 4) % 4;
        printf("      reverse beat: from waypoint %d to %d (expected %d)\n",
               start_wp, police_patrol_waypoint(0), expect);
        CHECK(police_patrol_waypoint(0) == expect, "a reversed lane does not walk the beat backwards (%d, not %d)",
              police_patrol_waypoint(0), expect);
        printf("PASS: a lane has a direction, and both directions keep to their own side of the centreline\n");
    }

    /* ---- a graph gives each cruiser a direction and a different start ---- */
    {
        float xs[4] = { 100.0f, 180.0f, 160.0f, 60.0f };
        float zs[4] = { 0.0f, 40.0f, 140.0f, 120.0f };
        police_patrol_set_lane_from_graph(xs, zs, 4);
        CHECK(police_patrol_has_lane(3) == 4, "the graph did not reach every slot");
        CHECK(police_patrol_direction(0) == 1 && police_patrol_direction(1) == -1,
              "two slots on one beat were not given opposite directions (%d, %d)",
              police_patrol_direction(0), police_patrol_direction(1));
        CHECK(police_patrol_waypoint(0) != police_patrol_waypoint(2),
              "two cruisers going the SAME way still start at the same point (%d, %d)",
              police_patrol_waypoint(0), police_patrol_waypoint(2));
        CHECK(police_patrol_waypoint(0) == police_patrol_waypoint(1),
              "the pair that shares a road does not start at the same node from opposite ends (%d, %d)",
              police_patrol_waypoint(0), police_patrol_waypoint(1));

        /* a graph with no nodes clears the beat, and the ring comes back */
        police_patrol_set_lane_from_graph(NULL, NULL, 0);
        CHECK(police_patrol_has_lane(0) == 0, "clearing the lane left %d waypoints", police_patrol_has_lane(0));
        float tx, tz;
        police_patrol_set_anchor(0, 0.0f, 0.0f, 100.0f);
        police_patrol_target(0, POLICE_PATROL_PERIOD_MS / 4, &tx, &tz);
        CHECK(tx > 90.0f, "with no lane the ring did not return (target x %.1f)", tx);

        police_patrol_set_lane(0, xs, zs, 1);
        CHECK(police_patrol_has_lane(0) == 0, "a one-point 'lane' was accepted as a beat");
        printf("PASS: a graph spreads the cruisers and alternates their direction; a bad lane falls back to the ring\n");
    }

    /* ---- keep-right comes from the ROAD, not from a constant ---- */
    {
        /* A corridor: two walls, 1 unit thick, centred on x = +-half, running along Z. */
        PolicePatrolBox street_walls[2];
        float wx[2] = { 0.0f, 0.0f };
        float wz[2] = { 0.0f, 140.0f };

        street_walls[0].x = -10.0f; street_walls[0].z = 60.0f; street_walls[0].w = 1.0f; street_walls[0].d = 200.0f;
        street_walls[1].x =  10.0f; street_walls[1].z = 60.0f; street_walls[1].w = 1.0f; street_walls[1].d = 200.0f;
        float wide = police_patrol_measure_width(wx, wz, 2, street_walls, 2);
        printf("      a 20-unit street measures %.2f; keep-right %.2f\n",
               wide, police_patrol_keep_right_for_width(wide));
        CHECK(fabsf(wide - 19.0f) < 0.1f, "a 20-unit street measured %.2f (walls are 1 thick)", wide);
        CHECK(police_patrol_keep_right_for_width(wide) <= POLICE_PATROL_KEEP_RIGHT_MAX + 0.001f,
              "a wide street asked for an offset past the cap");

        /* Narrow it: the same walls 8 units apart. The offset has to shrink with the road, which is the whole
           point -- 4 units in an 8-unit lane leaves nothing for the other direction. */
        street_walls[0].x = -4.0f;
        street_walls[1].x = 4.0f;
        float narrow = police_patrol_measure_width(wx, wz, 2, street_walls, 2);
        float narrow_off = police_patrol_keep_right_for_width(narrow);
        printf("      the same beat in an 8-unit street measures %.2f; keep-right %.2f\n", narrow, narrow_off);
        CHECK(fabsf(narrow - 7.0f) < 0.1f, "the 8-unit street measured %.2f", narrow);
        CHECK(narrow_off < POLICE_PATROL_KEEP_RIGHT, "a narrower road kept the same %.2f offset", narrow_off);
        CHECK(fabsf(narrow_off - 1.5f) < 0.01f, "an 8-unit road (half 3.5, car half 2) gave %.2f", narrow_off);

        /* Narrower than two cars: no offset at all, rather than a car in the wall. */
        street_walls[0].x = -1.0f;
        street_walls[1].x = 1.0f;
        float tight = police_patrol_measure_width(wx, wz, 2, street_walls, 2);
        printf("      a 1-unit-wide gap measures %.2f; keep-right %.2f\n", tight,
               police_patrol_keep_right_for_width(tight));
        CHECK(police_patrol_keep_right_for_width(tight) == 0.0f,
              "a gap narrower than two cars still asked for %.2f of offset",
              police_patrol_keep_right_for_width(tight));

        /* Open ground / no walls: nothing measured, so the default stands. */
        CHECK(police_patrol_measure_width(wx, wz, 2, street_walls, 0) == 0.0f, "no boxes still measured a road");
        CHECK(police_patrol_keep_right_for_width(0.0f) == POLICE_PATROL_KEEP_RIGHT,
              "an unmeasured road did not fall back to the default");

        /* And the offset really reaches the target: both directions, exactly this far off the centreline, on
           OPPOSITE sides. Measured at the MIDDLE of a three-point beat, not at an end -- at the first waypoint
           both directions approach from the same point (the chain wraps either way), so an end-of-chain check
           would pass even if keep-right did nothing but shift the car one way. */
        police_patrol_set_keep_right(narrow_off);
        float xs[3] = { 0.0f, 0.0f, 0.0f }, zs[3] = { 0.0f, 70.0f, 140.0f };
        police_patrol_set_lane(0, xs, zs, 3);
        BuggyState rb;
        memset(&rb, 0, sizeof(rb));
        rb.active = 1; rb.id = 0; rb.x = xs[0]; rb.z = zs[0]; rb.yaw = 0.0f;
        float th, st;
        police_patrol_intent(&rb, 0, &th, &st);       /* arrive at waypoint 0: the beat is now mid-chain */
        CHECK(police_patrol_waypoint(0) == 1, "the cruiser did not advance into the middle of the beat (%d)",
              police_patrol_waypoint(0));
        float fx, fz, rx, rz;
        police_patrol_set_direction(0, 1);
        police_patrol_target(0, 0, &fx, &fz);
        police_patrol_set_direction(0, -1);
        police_patrol_target(0, 0, &rx, &rz);
        float fdx = fx - xs[1], fdz = fz - zs[1];
        float rdx = rx - xs[1], rdz = rz - zs[1];
        printf("      with keep-right %.2f mid-beat: forward (%.2f,%.2f), reverse (%.2f,%.2f)\n",
               narrow_off, fdx, fdz, rdx, rdz);
        CHECK(fabsf(sqrtf(fdx * fdx + fdz * fdz) - narrow_off) < 0.05f,
              "the measured offset did not reach the target (%.2f)", sqrtf(fdx * fdx + fdz * fdz));
        CHECK(fabsf(sqrtf(rdx * rdx + rdz * rdz) - narrow_off) < 0.05f,
              "the reverse direction ignored the measured offset (%.2f)", sqrtf(rdx * rdx + rdz * rdz));
        CHECK(fdx * rdx < 0.0f, "both directions took the measured offset to the SAME side of the road");
        /* and it is still inside the walls: |offset| + half a car must fit the half road */
        CHECK(narrow_off + POLICE_PATROL_CAR_HALF_WIDTH <= narrow * 0.5f + 0.001f,
              "a cruiser at %.2f with a %.1f half-width does not fit a %.2f road", narrow_off,
              POLICE_PATROL_CAR_HALF_WIDTH, narrow);

        police_patrol_set_keep_right(POLICE_PATROL_KEEP_RIGHT);   /* leave the default for anything after */
        CHECK(police_patrol_keep_right() == POLICE_PATROL_KEEP_RIGHT, "the default offset did not come back");
        printf("PASS: keep-right is measured from the road, shrinks with it, and never puts a car in a wall\n");
    }

    printf("\n%s%d check(s) failed\n", g_failed ? "FAIL: " : "PASS: ", g_failed);
    return g_failed ? 1 : 0;
}
