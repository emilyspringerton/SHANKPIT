/* brick_bodies_test.c -- cut-off geometry falls and lies flat (the cheap fake, brick_bodies.h): it falls under
 * gravity, never hangs in the air, lands on the floor plane or on a platform box, settles flat and goes away. */
#include "brick_bodies.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

/* one platform: top at y = 10, spanning x,z in [-5,5] */
static int platform_push(void *u, const double c[3], double r, double n[3], double *pen) {
    (void)u;
    double hx = 5, hy = 1, hz = 5, bx = 0, by = 9, bz = 0;
    double q[3] = { c[0] - bx, c[1] - by, c[2] - bz };
    double cl[3] = { fmax(-hx, fmin(hx, q[0])), fmax(-hy, fmin(hy, q[1])), fmax(-hz, fmin(hz, q[2])) };
    double d[3] = { q[0] - cl[0], q[1] - cl[1], q[2] - cl[2] };
    double d2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    if (d2 < 1e-12) { n[0] = 0; n[1] = 1; n[2] = 0; *pen = (by + hy - c[1]) + r; return 1; }   /* centre inside: out through the top */
    if (d2 >= r * r) return 0;
    double dl = sqrt(d2);
    n[0] = d[0] / dl; n[1] = d[1] / dl; n[2] = d[2] / dl; *pen = r - dl;
    return 1;
}

static void run(BrickBodies *bb, double seconds) { for (double t = 0; t < seconds; t += 1.0 / 60) bb_step(bb, 1.0 / 60); }

int main(void) {
    static BrickBodies bb;
    bb_init(&bb, NULL, NULL);
    const double half[3] = { 2.5, 2.5, 0.3 }, vel[3] = { 0.5, 0, 0 }, om[3] = { 2.0, 0.5, 1.5 };

    /* falls, then lies flat on the floor plane (thin axis vertical), never below it, never hovering */
    double pos[3] = { 0, 20, 0 };
    int s = bb_spawn(&bb, 3, 1, 2500.0 * 5 * 5 * 0.6, pos, half, vel, om);
    CHECK(s >= 0);
    CHECK(fabs(bb_slot_mass(&bb, s) - 37500.0) < 1.0);
    const GrbBody *b = &bb.world.bodies[s];
    run(&bb, 0.5);
    CHECK(b->pos[1] < 20.0 - 2.0);                                   /* falling */
    double min_y = 1e9;
    for (int i = 0; i < 200; i++) { bb_step(&bb, 1.0 / 60); if (b->pos[1] < min_y) min_y = b->pos[1]; }
    CHECK(min_y > 0.25);                                             /* never through the floor */
    CHECK(fabs(b->pos[1] - 0.3) < 0.05);                             /* resting on it, 0.3 = half the thin axis */
    double up[3] = { 0, 0, 1 }, w[3];
    grb_quat_rotate(b->rot, up, w);
    CHECK(fabs(fabs(w[1]) - 1.0) < 0.02);                            /* thin axis is vertical: lying flat */
    printf("chunk landed flat at y=%.2f\n", b->pos[1]);
    run(&bb, 6.0);
    CHECK(bb_active_count(&bb) == 0);                                /* and goes away after lingering */

    /* lands on a platform, not through it */
    bb_clear(&bb); bb.push = platform_push;
    double p2[3] = { 1, 25, 1 };
    s = bb_spawn(&bb, 0, 1, 1000.0, p2, half, vel, om);
    b = &bb.world.bodies[s];
    run(&bb, 2.5);
    CHECK(fabs(b->pos[1] - (10.0 + 0.3)) < 0.1);
    printf("chunk landed on platform at y=%.2f\n", b->pos[1]);

    /* a chunk beside the platform falls past it to the floor */
    bb_clear(&bb);
    double p3[3] = { 30, 25, 0 };
    s = bb_spawn(&bb, 0, 1, 1000.0, p3, half, vel, om);
    b = &bb.world.bodies[s];
    run(&bb, 2.5);
    CHECK(fabs(b->pos[1] - 0.3) < 0.1);

    /* pool full: a new chunk still spawns (oldest landed one retires), nothing explodes */
    bb_clear(&bb);
    for (int i = 0; i < BB_MAX + 10; i++) { double pp[3] = { i, 5 + i, 0 }; CHECK(bb_spawn(&bb, 0, 1, 10.0, pp, half, vel, om) >= 0); }
    CHECK(bb_active_count(&bb) == BB_MAX);

    /* bad input rejected */
    double nan3[3] = { NAN, 0, 0 };
    CHECK(bb_spawn(&bb, 0, 1, 10.0, nan3, half, vel, om) == -1);
    CHECK(bb_spawn(&bb, 0, 1, 0.0, pos, half, vel, om) == -1);

    if (g_fail) { printf("%d FAILED\n", g_fail); return 1; }
    printf("brick_bodies_test: all checks passed\n");
    return 0;
}
