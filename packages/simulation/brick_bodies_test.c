/* brick_bodies_test.c -- headless checks for brick_bodies.{h,c}: broken brick as GOLDEN BAND rigid bodies.
 *
 *   gcc -std=gnu99 -Wall -Wextra -Werror -fsanitize=address,undefined -Ipackages/goldenband -Ipackages/simulation \
 *       packages/simulation/brick_bodies_test.c packages/simulation/brick_bodies.c packages/goldenband/grb.c -lm
 */
#include "brick_bodies.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static BrickBodies g_bb;     /* ~150 KB: static, never on the stack */

/* A 35-degree ramp: solid where  n . p < 0,  n = (-sin t, cos t, 0)  (surface y = x tan t, rising toward +x). */
static int ramp_push(void *u, const double c[3], double r, double nrm[3], double *pen) {
    (void)u;
    const double t = 35.0 * M_PI / 180.0, nx = -sin(t), ny = cos(t);
    double d = c[0] * nx + c[1] * ny;
    if (c[0] < -0.5 || d >= r) return 0;          /* the ramp starts at x = 0 (a little lip of tolerance) */
    nrm[0] = nx; nrm[1] = ny; nrm[2] = 0.0;
    *pen = r - d;
    return 1;
}

static void spawn_shard(BrickBodies *bb, double x, double y, double z, const double v[3]) {
    const double pos[3] = { x, y, z }, half[3] = { BB_SHARD_HALF, BB_SHARD_HALF, BB_SHARD_HALF }, om[3] = { 0.3, 0.2, 0.1 };
    CHECK(bb_spawn(bb, 0, 0, 1900.0 * 8.0 * BB_SHARD_HALF * BB_SHARD_HALF * BB_SHARD_HALF, pos, half, v, om) >= 0);
}

int main(void) {
    const double zero[3] = { 0, 0, 0 };

    /* --- mass: a brick shard is ~15 kg ------------------------------------------------------- */
    bb_init(&g_bb, NULL, NULL);
    spawn_shard(&g_bb, 0, 5, 0, zero);
    double m = bb_slot_mass(&g_bb, 0);
    CHECK(fabs(m - 15.2) < 0.05);
    CHECK(bb_active_count(&g_bb) == 1);

    /* --- falls and comes to rest on the flat floor -------------------------------------------- */
    for (int i = 0; i < 64 * 5; i++) bb_step(&g_bb, 1.0 / 64.0);
    const GrbBody *b = &g_bb.world.bodies[0];
    CHECK(b->pos[1] > 0.05 && b->pos[1] < 0.2);                 /* resting on a face/edge, never through the floor */
    CHECK(fabs(b->vel[1]) < 0.1);
    CHECK(fabs(b->pos[1]) < 1.0);

    /* --- inherits a fraction of the impact velocity ------------------------------------------- */
    bb_clear(&g_bb);
    const double shot[3] = { 9.0, 0, 0 };
    spawn_shard(&g_bb, 0, 50, 0, shot);                         /* high up: pure ballistic for 0.25 s */
    for (int i = 0; i < 16; i++) bb_step(&g_bb, 1.0 / 64.0);
    CHECK(fabs(g_bb.world.bodies[0].pos[0] - 9.0 * 0.25) < 0.05);
    CHECK(fabs(g_bb.world.bodies[0].vel[0] - 9.0) < 0.05);

    /* --- shards pile up instead of interpenetrating ------------------------------------------- */
    bb_clear(&g_bb);
    for (int i = 0; i < 24; i++) {
        double jx = (double)(i % 3) * 0.04, jz = (double)((i / 3) % 3) * 0.04;
        spawn_shard(&g_bb, jx, 0.6 + (double)i * 0.35, jz, zero);
    }
    for (int i = 0; i < 64 * 10; i++) bb_step(&g_bb, 1.0 / 64.0);
    double top = 0.0, min_sep = 1e9;
    for (int i = 0; i < 24; i++) {
        const GrbBody *a = &g_bb.world.bodies[i];
        CHECK(a->pos[1] > 0.03);
        if (a->pos[1] > top) top = a->pos[1];
        for (int j = i + 1; j < 24; j++) {
            const GrbBody *c = &g_bb.world.bodies[j];
            double d = sqrt((a->pos[0] - c->pos[0]) * (a->pos[0] - c->pos[0]) + (a->pos[1] - c->pos[1]) * (a->pos[1] - c->pos[1]) +
                            (a->pos[2] - c->pos[2]) * (a->pos[2] - c->pos[2]));
            if (d < min_sep) min_sep = d;
        }
    }
    CHECK(min_sep > 0.12);                                      /* spheres of radius 0.1: no deep overlap */
    CHECK(top > 0.25);                                          /* a heap, not a single flat layer */

    /* --- slides / tumbles down a ramp ---------------------------------------------------------- */
    bb_init(&g_bb, ramp_push, NULL);
    spawn_shard(&g_bb, 6.0, 6.0 * tan(35.0 * M_PI / 180.0) + 0.5, 0.0, zero);
    double x0 = g_bb.world.bodies[0].pos[0];
    for (int i = 0; i < 64 * 6; i++) bb_step(&g_bb, 1.0 / 64.0);
    const GrbBody *rb = &g_bb.world.bodies[0];
    CHECK(rb->pos[0] < x0 - 3.0);                               /* travelled well down the slope */
    CHECK(rb->pos[1] < 0.5 && rb->pos[1] > 0.0);                /* ... and reached the floor at its foot */
    const double ramp_x = rb->pos[0];

    /* --- a big chunk: mass is the sum over its cells, and it keeps spinning a bit as it falls --- */
    bb_init(&g_bb, NULL, NULL);
    const double cp[3] = { 0, 8, 0 }, ch[3] = { 1.5, 0.15, 1.5 }, co[3] = { 1.2, 0.4, 0.8 };
    int s = bb_spawn(&g_bb, 3, 1, 2500.0 * 3.0 * 0.3 * 3.0, cp, ch, zero, co);
    CHECK(s >= 0);
    CHECK(fabs(bb_slot_mass(&g_bb, s) - 2500.0 * 2.7) < 1.0);
    for (int i = 0; i < 32; i++) bb_step(&g_bb, 1.0 / 64.0);
    CHECK(g_bb.world.bodies[s].pos[1] < 8.0 && g_bb.world.bodies[s].pos[1] > 4.0);   /* falling, not teleporting */
    double w = fabs(g_bb.world.bodies[s].omega[0]) + fabs(g_bb.world.bodies[s].omega[2]);
    CHECK(w > 0.3);                                             /* the tumble survived (no air drag on spin) */

    /* --- determinism ------------------------------------------------------------------------- */
    static BrickBodies other;
    bb_init(&g_bb, ramp_push, NULL); bb_init(&other, ramp_push, NULL);
    for (int pass = 0; pass < 2; pass++) {
        BrickBodies *bb = pass ? &other : &g_bb;
        for (int i = 0; i < 6; i++) spawn_shard(bb, 3.0 + i * 0.2, 5.0 + i * 0.5, 0.1 * i, shot);
        for (int i = 0; i < 200; i++) bb_step(bb, 1.0 / 64.0);
    }
    CHECK(memcmp(g_bb.world.bodies, other.world.bodies, sizeof(g_bb.world.bodies[0]) * 6) == 0);

    /* --- bounded: spawning past capacity recycles, never overflows ------------------------------ */
    bb_init(&g_bb, NULL, NULL);
    for (int i = 0; i < BB_MAX + 40; i++) spawn_shard(&g_bb, i * 0.3, 2.0, 0, zero);
    CHECK(bb_active_count(&g_bb) == BB_MAX);

    if (g_fail) { printf("brick_bodies_test: %d FAILED\n", g_fail); return 1; }
    printf("brick_bodies_test: all checks passed (brick shard mass %.1f kg, pile top %.2f m, ramp slide to x=%.2f)\n", m, top, ramp_x);
    return 0;
}
