/* Unit tests for packages/common/obb.h (oriented boxes + ramps). */
#include <stdio.h>
#include <math.h>
#include "../../packages/common/obb.h"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static int near(float a, float b, float e) { return fabsf(a - b) < e; }
int main(void) {
    BoxOrient id; orient_identity(&id);
    float n[3], depth, t, y;
    /* axis-aligned 10x4x10 box at origin: sphere r=1 sitting 0.5 into the top is pushed up 0.5 */
    CHECK(obb_sphere_push(0,0,0, 10,4,10, &id, 0, 2.5f, 0, 1.0f, n, &depth), "AABB top overlap");
    CHECK(near(n[1], 1, 1e-4f) && near(depth, 0.5f, 1e-3f), "top push n.y=%f depth=%f", n[1], depth);
    CHECK(!obb_sphere_push(0,0,0, 10,4,10, &id, 0, 3.5f, 0, 1.0f, n, &depth), "AABB clear above");
    CHECK(obb_ground_y(0,0,0, 10,4,10, &id, 1, 1, &y) && near(y, 2, 1e-3f), "AABB ground y=%f", y);
    CHECK(!obb_ground_y(0,0,0, 10,4,10, &id, 9, 0, &y), "outside footprint");
    /* 45deg yaw: corner of a 10-wide box reaches 7.07 along +x; point at x=6 is inside, x=8 outside */
    BoxOrient yaw45; orient_from_euler_deg(&yaw45, 0, 45, 0, 0);
    CHECK(obb_ground_y(0,0,0, 10,4,10, &yaw45, 6, 0, &y), "rotated box covers the diagonal");
    CHECK(!obb_ground_y(0,0,0, 10,4,10, &yaw45, 5, 5, &y), "rotated box: (5,5) lies outside the diamond");
    /* 90deg yaw about Y swaps footprint of a 20x4x4 box */
    BoxOrient yaw90; orient_from_euler_deg(&yaw90, 0, 90, 0, 0);
    CHECK(obb_ground_y(0,0,0, 20,4,4, &yaw90, 0, 8, &y), "90deg: long axis now along z");
    CHECK(!obb_ground_y(0,0,0, 20,4,4, &yaw90, 8, 0, &y), "90deg: x=8 now empty");
    /* ramp 10 wide, 4 tall, 10 deep: rises toward +z; surface y = -2 + 4*(z+5)/10 */
    BoxOrient ramp; orient_from_euler_deg(&ramp, 0, 0, 0, 1);
    CHECK(obb_ground_y(0,0,0, 10,4,10, &ramp, 0, -5, &y) && near(y, -2, 1e-3f), "ramp low end y=%f", y);
    CHECK(obb_ground_y(0,0,0, 10,4,10, &ramp, 0, 0, &y) && near(y, 0, 1e-3f), "ramp mid y=%f", y);
    CHECK(obb_ground_y(0,0,0, 10,4,10, &ramp, 0, 4.99f, &y) && near(y, 1.996f, 1e-2f), "ramp high end y=%f", y);
    /* sphere resting on the slope is pushed along the slope normal (n.y>0, n.z<0) */
    CHECK(obb_sphere_push(0,0,0, 10,4,10, &ramp, 0, 0.5f, 0, 1.0f, n, &depth), "ramp slope overlap");
    CHECK(n[1] > 0.8f && n[2] < -0.2f, "slope normal %f %f %f", n[0], n[1], n[2]);
    /* a sphere above the high end but below the plane's line of sight... high-end cap blocks from +z */
    CHECK(obb_sphere_push(0,0,0, 10,4,10, &ramp, 0, 0.0f, 5.5f, 1.0f, n, &depth) && n[2] > 0.9f, "ramp back wall n.z=%f", n[2]);
    /* ray down onto the ramp hits the slope; ray from the side at low end passes over the empty wedge */
    CHECK(obb_segment_hit(0,0,0, 10,4,10, &ramp, 0, 10, 0, 0,-20,0, 1, &t, n) && near(10 - t*20, 0, 1e-3f), "ray hits slope at y=0");
    CHECK(!obb_segment_hit(0,0,0, 10,4,10, &ramp, -20, 1.5f, -4, 40,0,0, 1, &t, n), "ray above the low end of the slope misses");
    CHECK(obb_segment_hit(0,0,0, 10,4,10, &ramp, -20, -1.9f, -4, 40,0,0, 1, &t, n) && near(n[0], -1, 1e-4f), "ray below slope hits side face");
    /* tilted 30deg about X: top face normal tips toward -z/+y consistently with Rx */
    BoxOrient tilt; orient_from_euler_deg(&tilt, 30, 0, 0, 0);
    CHECK(obb_segment_hit(0,0,0, 10,4,10, &tilt, 0, 10, 0, 0,-20,0, 1, &t, n) && near(n[1], cosf(0.5235988f), 1e-3f), "tilt normal y=%f", n[1]);
    printf(fails ? "obb: %d FAILURES\n" : "obb: all passed\n", fails);
    return fails ? 1 : 0;
}
