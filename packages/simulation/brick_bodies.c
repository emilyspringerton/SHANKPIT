/* brick_bodies.c -- see brick_bodies.h. */
#include "brick_bodies.h"

#include <math.h>
#include <string.h>

#define BB_TICK (1.0 / BB_HZ)
#define BB_CORNER_R 0.03
#define BB_MU 0.45
#define BB_REST_SPEED 0.08
#define BB_REST_SPIN 0.25
#define BB_REST_TIME 0.6

static double dot3(const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void cross3(const double a[3], const double b[3], double o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0];
}
static int finite3(const double v[3]) { return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]); }

/* world-space I^-1 * v */
static void inv_i_apply(const GrbBody *b, const double v[3], double o[3]) {
    double c[4], l[3];
    grb_quat_conj(b->rot, c);
    grb_quat_rotate(c, v, l);
    l[0] *= b->inv_inertia[0]; l[1] *= b->inv_inertia[1]; l[2] *= b->inv_inertia[2];
    grb_quat_rotate(b->rot, l, o);
}

/* one impulse along dir at lever arm r; returns the impulse magnitude */
static double impulse(GrbBody *b, const double r[3], const double dir[3], double target_vn, double lo, double hi) {
    double wr[3], vp[3];
    cross3(b->omega, r, wr);
    for (int a = 0; a < 3; a++) vp[a] = b->vel[a] + wr[a];
    double vn = dot3(vp, dir);
    double rn[3], irn[3], t[3];
    cross3(r, dir, rn);
    inv_i_apply(b, rn, irn);
    cross3(irn, r, t);
    double denom = b->inv_mass + dot3(t, dir);
    if (denom < 1e-12) return 0.0;
    double j = (target_vn - vn) / denom;
    if (j < lo) j = lo;
    if (j > hi) j = hi;
    for (int a = 0; a < 3; a++) b->vel[a] += dir[a] * j * b->inv_mass;
    for (int a = 0; a < 3; a++) b->omega[a] += irn[a] * j;
    return j;
}

static void contact(GrbBody *b, const double pt[3], const double n[3]) {
    double r[3] = { pt[0] - b->pos[0], pt[1] - b->pos[1], pt[2] - b->pos[2] };
    double wr[3], vp[3];
    cross3(b->omega, r, wr);
    for (int a = 0; a < 3; a++) vp[a] = b->vel[a] + wr[a];
    double vn = dot3(vp, n);
    if (vn >= 0.0) return;
    double target = (vn < -2.0) ? -vn * b->restitution : 0.0;   /* no micro-bounce at rest */
    double jn = impulse(b, r, n, target, 0.0, 1e30);
    cross3(b->omega, r, wr);
    for (int a = 0; a < 3; a++) vp[a] = b->vel[a] + wr[a];
    double vn2 = dot3(vp, n);
    double tv[3] = { vp[0] - n[0] * vn2, vp[1] - n[1] * vn2, vp[2] - n[2] * vn2 };
    double tl = sqrt(dot3(tv, tv));
    if (tl > 1e-6) {
        double td[3] = { tv[0] / tl, tv[1] / tl, tv[2] / tl };
        double lim = b->friction * jn;
        impulse(b, r, td, 0.0, -lim, lim);
    }
}

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
    bb->world.substeps = 8;
    grb_world_add_plane(&bb->world, 0.0, 1.0, 0.0, bb->ground_y);
    const double p[3] = { 0, -1.0e4, 0 }, q[4] = { 0, 0, 0, 1 };
    for (int i = 0; i < BB_MAX; i++) grb_body_add(&bb->world, 0.0, NULL, p, q);   /* inert placeholders, activated in place */
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
    if (s < 0) { s = bb->cursor; bb->cursor = (bb->cursor + 1) % BB_MAX; }   /* recycle the oldest-ish */
    GrbBody *b = &bb->world.bodies[s];
    memset(b, 0, sizeof *b);
    for (int a = 0; a < 3; a++) { b->pos[a] = pos[a]; b->vel[a] = vel[a]; b->omega[a] = omega[a]; }
    b->rot[3] = 1.0;
    double I[3];
    grb_inertia_box(mass, half[0], half[1], half[2], I);
    b->inv_mass = 1.0 / mass;
    for (int a = 0; a < 3; a++) b->inv_inertia[a] = 1.0 / I[a];
    grb_body_set_box(b, half[0], half[1], half[2]);
    b->friction = BB_MU;
    b->restitution = (kind == 3) ? 0.10 : (kind == 2) ? 0.25 : 0.15;
    BbSlot *sl = &bb->slot[s];
    sl->active = 1; sl->kind = kind; sl->big = big;
    for (int a = 0; a < 3; a++) sl->half[a] = half[a];
    sl->calm = 0.0; sl->age = 0.0;
    return s;
}

static void collide_static(BrickBodies *bb, int si) {
    if (!bb->push) return;
    GrbBody *b = &bb->world.bodies[si];
    const BbSlot *sl = &bb->slot[si];
    for (int corner = 0; corner < 8; corner++) {
        double lp[3] = { (corner & 1) ? sl->half[0] : -sl->half[0], (corner & 2) ? sl->half[1] : -sl->half[1],
                         (corner & 4) ? sl->half[2] : -sl->half[2] };
        double off[3], wp[3], n[3], pen;
        grb_quat_rotate(b->rot, lp, off);
        for (int a = 0; a < 3; a++) wp[a] = b->pos[a] + off[a];
        if (!bb->push(bb->user, wp, BB_CORNER_R, n, &pen) || pen <= 0.0) continue;
        double push = pen > 0.25 ? 0.25 : pen;
        for (int a = 0; a < 3; a++) { b->pos[a] += n[a] * push; wp[a] += n[a] * push; }
        contact(b, wp, n);
    }
}

/* Shard-vs-shard: each shard stands in as a world-aligned cube (a deliberate fake -- stable stacking is
 * what makes a heap, and a sphere cannot do it; the rendered rotation is free). Resolved along the axis
 * of least overlap with a position split by inverse mass, a normal impulse and Coulomb friction. */
static void collide_pairs(BrickBodies *bb) {
    for (int i = 0; i < BB_MAX; i++) {
        if (!bb->slot[i].active || bb->slot[i].big) continue;
        GrbBody *a = &bb->world.bodies[i];
        double ra = fmax(bb->slot[i].half[0], fmax(bb->slot[i].half[1], bb->slot[i].half[2]));
        for (int j = i + 1; j < BB_MAX; j++) {
            if (!bb->slot[j].active || bb->slot[j].big) continue;
            GrbBody *b = &bb->world.bodies[j];
            double rb = fmax(bb->slot[j].half[0], fmax(bb->slot[j].half[1], bb->slot[j].half[2]));
            double d[3] = { a->pos[0] - b->pos[0], a->pos[1] - b->pos[1], a->pos[2] - b->pos[2] };
            double rr = ra + rb, ov[3];
            int axis = 0, skip = 0;
            for (int k = 0; k < 3; k++) { ov[k] = rr - fabs(d[k]); if (ov[k] <= 0.0) skip = 1; }
            if (skip) continue;
            for (int k = 1; k < 3; k++) if (ov[k] < ov[axis]) axis = k;
            double n[3] = { 0, 0, 0 };
            n[axis] = d[axis] >= 0.0 ? 1.0 : -1.0;
            double pen = ov[axis];
            double wsum = a->inv_mass + b->inv_mass;
            if (wsum <= 0.0) continue;
            for (int k = 0; k < 3; k++) { a->pos[k] += n[k] * pen * a->inv_mass / wsum; b->pos[k] -= n[k] * pen * b->inv_mass / wsum; }
            double vr[3] = { a->vel[0] - b->vel[0], a->vel[1] - b->vel[1], a->vel[2] - b->vel[2] };
            double vn = dot3(vr, n);
            if (vn >= 0.0) continue;
            double j2 = -vn / wsum;
            for (int k = 0; k < 3; k++) { a->vel[k] += n[k] * j2 * a->inv_mass; b->vel[k] -= n[k] * j2 * b->inv_mass; }
            double tv[3] = { vr[0] - n[0] * vn, vr[1] - n[1] * vn, vr[2] - n[2] * vn };
            double tl = sqrt(dot3(tv, tv));
            if (tl > 1e-9) {
                double jt = tl / wsum, lim = BB_MU * j2;
                if (jt > lim) jt = lim;
                for (int k = 0; k < 3; k++) { a->vel[k] -= tv[k] / tl * jt * a->inv_mass; b->vel[k] += tv[k] / tl * jt * b->inv_mass; }
            }
            for (int k = 0; k < 3; k++) { a->omega[k] *= 0.97; b->omega[k] *= 0.97; }   /* slump, don't roll forever */
        }
    }
}

static void tick(BrickBodies *bb) {
    bb->world.planes[0].offset = bb->ground_y;
    grb_world_step(&bb->world, BB_TICK);
    for (int i = 0; i < BB_MAX; i++) {
        if (!bb->slot[i].active) continue;
        GrbBody *b = &bb->world.bodies[i];
        if (!finite3(b->pos) || !finite3(b->vel) || !finite3(b->omega) || b->pos[1] < -300.0) { deactivate(bb, i); continue; }
        /* sleeping (the cheap half of "only disturbed geometry costs anything"): a body that has been calm
           for a while skips the static-geometry scan, re-checking its support only every 16th tick */
        if (bb->slot[i].calm > BB_REST_TIME && ((bb->ticks + (unsigned int)i) & 15u) != 0u) continue;
        collide_static(bb, i);
    }
    bb->ticks++;
    collide_pairs(bb);
    for (int i = 0; i < BB_MAX; i++) {
        BbSlot *sl = &bb->slot[i];
        if (!sl->active) continue;
        GrbBody *b = &bb->world.bodies[i];
        grb_quat_normalize(b->rot);
        sl->age += BB_TICK;
        if (sl->age > BB_MAX_AGE) { deactivate(bb, i); continue; }
        double sp = dot3(b->vel, b->vel), sw = dot3(b->omega, b->omega);
        if (sp < BB_REST_SPEED * BB_REST_SPEED && sw < BB_REST_SPIN * BB_REST_SPIN) {
            sl->calm += BB_TICK;
            if (sl->calm > BB_REST_TIME) { b->vel[0] = b->vel[1] = b->vel[2] = 0.0; b->omega[0] = b->omega[1] = b->omega[2] = 0.0; }
        } else {
            sl->calm = 0.0;
        }
    }
}

void bb_step(BrickBodies *bb, double dt) {
    if (!(dt > 0.0)) return;
    bb->acc += dt;
    if (bb->acc > 4.0 * BB_TICK) bb->acc = 4.0 * BB_TICK;
    while (bb->acc >= BB_TICK) { tick(bb); bb->acc -= BB_TICK; }
}
