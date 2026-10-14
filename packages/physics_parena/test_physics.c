#include <math.h>
#include <stdio.h>
#include "physics_host.h"


static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

int main(void) {
    /* slot 0 = sentinel (skipped); box 1: 10x10x10 cube at (0,5,0); box 2: slab at x=20 */
    PhysHostBox boxes[3] = {{0, 0, 0, 0, 0, 0}, {0, 5, 0, 10, 10, 10}, {20, 5, 0, 2, 10, 10}};
    float hit[3], nrm[3], t; int bi;

    float seg[6] = {-20, 5, 0, 0, 5, 0};
    CHECK(phys_host_trace(boxes, 3, seg, hit, nrm, &t, &bi) == 1);
    CHECK(fabsf(hit[0] + 5.0f) < 1e-4f && nrm[0] == -1.0f && bi == 1);
    CHECK(fabsf(t - 0.75f) < 1e-5f);

    float seg_floor[6] = {50, 5, 50, 50, -3, 50};
    CHECK(phys_host_trace(boxes, 3, seg_floor, hit, nrm, &t, &bi) == 1);
    CHECK(nrm[1] == 1.0f && fabsf(hit[1] - 0.1f) < 1e-6f && bi == -1);

    float seg_miss[6] = {50, 5, 50, 60, 5, 50};
    CHECK(phys_host_trace(boxes, 3, seg_miss, hit, nrm, &t, &bi) == 0);

    /* sphere r=1 from x=-20 toward +x: inflated face at x=-6 -> t = 14/20 = 0.7 */
    float o[3] = {-20, 5, 0}, dl[3] = {20, 0, 0};
    CHECK(phys_host_sweep(boxes, 3, 1.0f, o, dl, hit, nrm, &t, &bi) == 1);
    CHECK(fabsf(t - 0.7f) < 1e-5f && fabsf(hit[0] + 6.0f) < 1e-4f);

    /* capsule base (0,0,0) h=4 r=1; ray from (-10,2,0) along +x hits cylinder at x=-1 */
    float ro[3] = {-10, 2, 0}, rd[3] = {1, 0, 0}, base[3] = {0, 0, 0};
    CHECK(fabsf(phys_host_ray_capsule(ro, rd, base, 4.0f, 1.0f) - 9.0f) < 1e-4f);
    /* above the top cap: ray at y=6 along x misses */
    float ro2[3] = {-10, 6, 0};
    CHECK(phys_host_ray_capsule(ro2, rd, base, 4.0f, 1.0f) < 0.0f);
    /* straight down the axis hits the top cap sphere (apex y=5) at t=5 from y=10 */
    float ro3[3] = {0, 10, 0}, rd3[3] = {0, -1, 0};
    CHECK(fabsf(phys_host_ray_capsule(ro3, rd3, base, 4.0f, 1.0f) - 5.0f) < 1e-4f);

    /* hit-location: shooter at (0,2,-20) firing +z at target feet (0,0,0): head sphere at y=2.42 */
    float so[3] = {0, 2.42f, -20}, sd[3] = {0, 0, 1}, tg[3] = {0, 0, 0};
    CHECK(phys_host_hit_location(so, sd, tg, 1, 0) == 2);
    float so2[3] = {0, -0.5f, -20};
    CHECK(phys_host_hit_location(so2, sd, tg, 1, 0) == 1);
    CHECK(phys_host_hit_location(so, sd, tg, 0, 0) == 0);

    /* resolve: land on top of box 1 (top = 10): prev_y = y - vy = 10.5 >= 10 -> LAND */
    PhysHostPlayer p = {0, 9.5f, 0, 0, -1.0f, 0, 0, 0.97f, 6.47f, 0, 0, 0, 0, 0.3f};
    PhysHostEvent ev[8];
    int n = phys_host_resolve(boxes, 3, &p, ev, 8);
    CHECK(n >= 1 && ev[0].kind == PHYS_EV_LAND && ev[0].a == 1);
    CHECK(p.on_ground == 1 && fabsf(p.y - 10.0f) < 1e-5f && p.vy == 0.0f);

    /* side push-out: walking into -x face at y=5 (inside slab vertically), prev_y == y */
    PhysHostPlayer q = {-4.5f, 5.0f, 0, 1.0f, 0, 0, 0, 0.97f, 6.47f, 0, 0, 0, 0, 0.3f};
    n = phys_host_resolve(boxes, 3, &q, ev, 8);
    CHECK(n >= 1 && ev[0].kind == PHYS_EV_WALL_X);
    CHECK(fabsf(q.x - (-5.0f - 0.97f)) < 1e-4f && q.vx == 0.0f);

    /* below ground */
    PhysHostPlayer r = {40, -2, 40, 0, -1, 0, 0, 0.97f, 6.47f, 0, 0, 0, 0, 0.3f};
    n = phys_host_resolve(boxes, 3, &r, ev, 8);
    CHECK(n == 1 && ev[0].kind == PHYS_EV_FLOOR && r.y == 0.0f && r.on_ground == 1);

    {
        PhysHostLagRecord ring[PHYS_HOST_LAG_SLOTS] = {{0, 0, 0, 0, 0}};
        float lp[3];
        phys_host_lag_store(ring, 1600.0, 0.0f, 0.0f, 0.0f);
        phys_host_lag_store(ring, 1616.0, 10.0f, 20.0f, 30.0f);
        CHECK(phys_host_lag_rewind(ring, 1616.0, lp) == 1 && lp[0] == 10.0f && lp[2] == 30.0f);
        CHECK(phys_host_lag_rewind(ring, 1608.0, lp) == 0);          /* exact match only */
        CHECK(phys_host_lag_rewind_lerp(ring, 1608.0, lp) == 1);      /* bracketed -> midpoint */
        CHECK(fabsf(lp[0] - 5.0f) < 1e-5f && fabsf(lp[1] - 10.0f) < 1e-5f);
        CHECK(phys_host_lag_rewind_lerp(ring, 1700.0, lp) == 0);      /* no newer sample */
    }
    printf("pool high-water: %lu bytes\n", phys_host_pool_high_water());
    printf(g_fail ? "FAILED (%d)\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
