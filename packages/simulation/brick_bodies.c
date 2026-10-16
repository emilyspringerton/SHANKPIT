/* brick_bodies.c -- see brick_bodies.h. A deliberately FAKE fall: gravity, a spin that dies on landing, a
 * floor/wall probe and a flat settle. No solver, no pairs. One level scan per chunk per tick. */
#include "brick_bodies.h"

#include <math.h>
#include <string.h>

#define BB_TICK (1.0 / BB_HZ)
#define BB_TERMINAL 17.0
#define BB_SETTLE_T 0.18     /* seconds to ease from the landing tilt to lying flat */

static int finite3(const double v[3]) { return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]); }

static void deactivate(BrickBodies *bb, int i) {
    GrbBody *b = &bb->world.bodies[i];
    memset(b, 0, sizeof *b);
    b->rot[3] = 1.0;
    b->pos[1] = -1.0e4;
    bb->slot[i].active = 0;
}

void bb_init(BrickBodies *bb, BbPushFn push, void *user) {
    memset(bb, 0, sizeof *bb);
    bb->push = push;
    bb->user = user;
    grb_world_init(&bb->world, 0.0, -BB_GRAVITY, 0.0);
    const double p[3] = { 0, -1.0e4, 0 }, q[4] = { 0, 0, 0, 1 };
    for (int i = 0; i < BB_MAX; i++) grb_body_add(&bb->world, 0.0, NULL, p, q);   /* storage for pos/rot/mass only */
}

void bb_clear(BrickBodies *bb) {
    for (int i = 0; i < BB_MAX; i++) deactivate(bb, i);
    bb->cursor = 0;
    bb->acc = 0.0;
}

int bb_active_count(const BrickBodies *bb) {
    int n = 0;
    for (int i = 0; i < BB_MAX; i++) if (bb->slot[i].active) n++;
    return n;
}

double bb_slot_mass(const BrickBodies *bb, int slot) {
    if (slot < 0 || slot >= BB_MAX || !bb->slot[slot].active || bb->world.bodies[slot].inv_mass <= 0.0) return 0.0;
    return 1.0 / bb->world.bodies[slot].inv_mass;
}

int bb_spawn(BrickBodies *bb, int kind, int big, double mass, const double pos[3], const double half[3],
             const double vel[3], const double omega[3]) {
    if (!(mass > 1e-6) || !finite3(pos) || !finite3(half) || !finite3(vel) || !finite3(omega)) return -1;
    if (!(half[0] > 1e-4 && half[1] > 1e-4 && half[2] > 1e-4)) return -1;
    int s = -1;
    for (int i = 0; i < BB_MAX; i++) if (!bb->slot[i].active) { s = i; break; }
    if (s < 0) {   /* pool full: retire the one that has been down longest */
        double best = -1.0;
        for (int i = 0; i < BB_MAX; i++) {
            double score = bb->slot[i].calm * 100.0 + bb->slot[i].age;
            if (best < 0.0 || score > best) { best = score; s = i; }
        }
    }
    GrbBody *b = &bb->world.bodies[s];
    memset(b, 0, sizeof *b);
    for (int a = 0; a < 3; a++) { b->pos[a] = pos[a]; b->vel[a] = vel[a]; b->omega[a] = omega[a]; }
    b->rot[3] = 1.0;
    b->inv_mass = 1.0 / mass;
    BbSlot *sl = &bb->slot[s];
    sl->active = 1; sl->kind = kind; sl->big = big;
    for (int a = 0; a < 3; a++) sl->half[a] = half[a];
    sl->calm = 0.0; sl->age = 0.0;
    sl->bounced = 0;
    return s;
}

/* world-space half extent of the box along y, from its current orientation */
static double ext_y(const BbSlot *sl, const double q[4]) {
    double x = q[0], y = q[1], z = q[2], w = q[3];
    double r10 = 2 * (x * y + z * w), r11 = 1 - 2 * (x * x + z * z), r12 = 2 * (y * z - x * w);   /* row y of R */
    return fabs(r10) * sl->half[0] + fabs(r11) * sl->half[1] + fabs(r12) * sl->half[2];
}

/* the orientation that lays the box's thinnest axis along world y */
static void flat_target(const BbSlot *sl, const double q[4], double out[4]) {
    int k = 0;
    for (int a = 1; a < 3; a++) if (sl->half[a] < sl->half[k]) k = a;
    double yaw_axis[3] = { 0, 1, 0 }, qy[4], qf[4];
    double fwd[3] = { 1, 0, 0 }, f[3];
    grb_quat_rotate(q, fwd, f);
    grb_quat_from_axis_angle(yaw_axis, atan2(-f[2], f[0]), qy);   /* keep the heading it fell with */
    if (k == 1) { memcpy(qf, qy, sizeof qf); }
    else {
        double ax[3] = { 0, 0, 0 }, qt[4];
        if (k == 0) ax[2] = 1.0; else ax[0] = 1.0;                /* x-thin: roll about z; z-thin: pitch about x */
        grb_quat_from_axis_angle(ax, 1.5707963267948966, qt);
        grb_quat_mul(qy, qt, qf);
    }
    memcpy(out, qf, sizeof qf);
}

static void land(BrickBodies *bb, int i, double support_y) {
    GrbBody *b = &bb->world.bodies[i];
    BbSlot *sl = &bb->slot[i];
    if (-b->vel[1] > 5.0 && !sl->bounced) {   /* one small bounce, then it is done */
        sl->bounced = 1;
        b->vel[1] = -b->vel[1] * 0.18;
        b->vel[0] *= 0.55; b->vel[2] *= 0.55;
        for (int a = 0; a < 3; a++) b->omega[a] *= 0.4;
        b->pos[1] = support_y + ext_y(sl, b->rot);
        return;
    }
    flat_target(sl, b->rot, sl->settle_q);
    b->vel[0] = b->vel[1] = b->vel[2] = 0.0;
    b->omega[0] = b->omega[1] = b->omega[2] = 0.0;
    int k = 0;
    for (int a = 1; a < 3; a++) if (sl->half[a] < sl->half[k]) k = a;
    sl->rest_y = support_y + sl->half[k];
    sl->calm = 1e-6;                           /* landed */
}

static void tick(BrickBodies *bb) {
    for (int i = 0; i < BB_MAX; i++) {
        BbSlot *sl = &bb->slot[i];
        if (!sl->active) continue;
        GrbBody *b = &bb->world.bodies[i];
        sl->age += BB_TICK;
        if (!finite3(b->pos) || b->pos[1] < -300.0 || sl->age > BB_MAX_AGE || sl->calm > BB_LINGER) { deactivate(bb, i); continue; }
        if (sl->calm > 0.0) {                  /* landed: ease flat, then sit */
            sl->calm += BB_TICK;
            if (sl->calm < BB_SETTLE_T) {
                double t = sl->calm / BB_SETTLE_T, q[4], n = 0.0;
                for (int a = 0; a < 4; a++) { q[a] = b->rot[a] * (1.0 - t) + sl->settle_q[a] * t; n += q[a] * q[a]; }
                double d = b->rot[0] * sl->settle_q[0] + b->rot[1] * sl->settle_q[1] + b->rot[2] * sl->settle_q[2] + b->rot[3] * sl->settle_q[3];
                if (d < 0.0) for (int a = 0; a < 4; a++) q[a] = b->rot[a] * (1.0 - t) - sl->settle_q[a] * t;
                (void)n;
                memcpy(b->rot, q, sizeof q); grb_quat_normalize(b->rot);
                b->pos[1] += (sl->rest_y - b->pos[1]) * t;
            } else {
                memcpy(b->rot, sl->settle_q, sizeof b->rot);
                b->pos[1] = sl->rest_y;
            }
            continue;
        }
        b->vel[1] -= BB_GRAVITY * BB_TICK;
        if (b->vel[1] < -BB_TERMINAL) b->vel[1] = -BB_TERMINAL;   /* a step stays under the probe radius: no tunnelling */
        for (int a = 0; a < 3; a++) b->pos[a] += b->vel[a] * BB_TICK;
        {   /* q += 0.5 dt (omega, 0) * q */
            const double *o = b->omega, *q = b->rot;
            double dq[4] = { 0.5 * BB_TICK * (o[0] * q[3] + o[1] * q[2] - o[2] * q[1]),
                             0.5 * BB_TICK * (o[1] * q[3] + o[2] * q[0] - o[0] * q[2]),
                             0.5 * BB_TICK * (o[2] * q[3] + o[0] * q[1] - o[1] * q[0]),
                             0.5 * BB_TICK * (-o[0] * q[0] - o[1] * q[1] - o[2] * q[2]) };
            for (int a = 0; a < 4; a++) b->rot[a] += dq[a];
            grb_quat_normalize(b->rot);
        }
        double ey = ext_y(sl, b->rot);
        if (b->pos[1] - ey <= bb->ground_y) { b->pos[1] = bb->ground_y + ey; land(bb, i, bb->ground_y); continue; }
        if (bb->push && b->vel[1] <= 0.0) {    /* one probe under the chunk's footprint: a platform / box top */
            double rp = sl->half[0] < sl->half[1] ? sl->half[0] : sl->half[1];
            if (sl->half[2] < rp) rp = sl->half[2];
            if (rp < 0.15) rp = 0.15;
            double c[3] = { b->pos[0], b->pos[1] - ey + rp, b->pos[2] }, n[3], pen;
            if (bb->push(bb->user, c, rp, n, &pen) && pen > 0.0 && n[1] > 0.6) {
                double support = c[1] + n[1] * pen - rp;
                b->pos[1] = support + ey;
                land(bb, i, support);
            }
        }
    }
    bb->ticks++;
}

void bb_step(BrickBodies *bb, double dt) {
    if (!(dt > 0.0)) return;
    bb->acc += dt;
    if (bb->acc > 2.0 * BB_TICK) bb->acc = 2.0 * BB_TICK;
    while (bb->acc >= BB_TICK) { tick(bb); bb->acc -= BB_TICK; }
}
