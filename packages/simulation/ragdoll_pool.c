/* ragdoll_pool.c -- see ragdoll_pool.h. Host plumbing only: every decision (bone from hit, impulse, shares, caps, calm/freeze/
 * despawn/fade timing, eviction score, contact response constants) is called from the PARENA-generated ragdoll_rules.c.
 *
 * Spawn pipeline (ragdoll_pool_spawn):
 *   1. sanitise every argument (non-finite -> neutral, clamp to the rules' bounds)
 *   2. pick a slot: lowest FREE index, else the lowest-priority resident (ties: oldest) unless the newcomer scores strictly below it
 *   3. restore the pristine template ragdoll into the slot (bit-identical to a fresh build)
 *   4. place the 17 bodies from the pose by forward kinematics (double precision), yawed by facing and moved to the world position
 *      -- a supplied pose that violates the joint limits too far is replaced by the default pose (rest, arms lowered)
 *   5. lift the whole body out of the floor if the pose starts below it, re-seed the hinge angles
 *   6. apply the death shove: per-segment share x impulse x direction (+lift, +lean spin), plus the inherited victim velocity
 * Step pipeline (one fixed 1/64 s tick, ragdoll_pool_step runs as many as the integer accumulator owes, capped):
 *   XPBD step (24 substeps, ground plane inside grb) -> static-box contact pass -> integrity guard + speed/spin caps -> measure
 *   fastest segment, fastest spin and the support gap -> PARENA calm / freeze / despawn / fade decisions. */
#include "ragdoll_pool.h"

#include <math.h>
#include <string.h>

#include "../goldenband/gpose.h"

#define TICK_DT (1.0 / (double)RAGDOLL_TICK_HZ)
#define ACC_UNITS_PER_TICK 1000000LL   /* (1/64 s = 15625 us) * 64 */
#define RUNAWAY_DISTANCE 5000.0        /* a body this far from where it spawned is broken, not simulated */
#define MAX_JOINT_TEAR 1.0             /* a joint attachment further apart than this (m) means the solve exploded */

/* bone id -> the skeleton joint rigid_ragdoll.c starts that segment at (verified against the built template at init) */
static const char *const BONE_JOINT[RAGDOLL_BONE_COUNT] = {
    "pelvis", "spine_01", "spine_02", "spine_03", "neck_01", "upperarm_l", "upperarm_r", "lowerarm_l", "lowerarm_r",
    "hand_l", "hand_r", "thigh_l", "thigh_r", "calf_l", "calf_r", "foot_l", "foot_r"
};

/* ---------------------------------------------------------------- small math */

static double fin(double v, double dflt) { return isfinite(v) ? v : dflt; }
static double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

static double v3len(const double v[3]) { return sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

static int finite3(const double v[3]) { return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]); }
static int finite4(const double v[4]) { return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]) && isfinite(v[3]); }

/* ---------------------------------------------------------------- pose placement */

/* Builds the world-space (model space, then yawed and translated) body transforms for the 17 segments from a local pose by
 * double-precision forward kinematics. `drop_arms` lowers each upper arm about its shoulder for the no-pose default. */
static void place_bodies(RagdollPool *P, RigidRagdoll *rr, const float *pose_rot, const float *pose_trans, int drop_arms,
                         double tx, double ty, double tz, double facing)
{
    const GSkel *sk = P->skel;
    const int n = (int)sk->joint_count;
    double wrot[GSKEL_MAX_JOINTS][4], wpos[GSKEL_MAX_JOINTS][3];
    const double arm_drop = (double)ragdoll_default_arm_drop_mrad() / 1000.0;
    const double offs_lim = (double)ragdoll_max_pelvis_offset_mm() / 1000.0;

    for (int j = 0; j < n; j++) {
        double lq[4], lt[3], tmp[4];
        const GSkelJoint *jt = &sk->joints[j];
        for (int k = 0; k < 4; k++) lq[k] = jt->rest_rotation[k];
        for (int k = 0; k < 3; k++) lt[k] = jt->rest_translation[k];
        if (pose_rot) {
            for (int k = 0; k < 4; k++) tmp[k] = (double)pose_rot[j * 4 + k];
            double nrm = finite4(tmp) ? sqrt(tmp[0] * tmp[0] + tmp[1] * tmp[1] + tmp[2] * tmp[2] + tmp[3] * tmp[3]) : 0.0;
            if (nrm > 1e-6 && isfinite(nrm)) for (int k = 0; k < 4; k++) lq[k] = tmp[k] / nrm;
            /* else: keep the rest rotation for this joint */
        }
        if (j == P->pelvis_joint && pose_trans) {
            for (int k = 0; k < 3; k++) {
                double v = (double)pose_trans[j * 3 + k];
                if (isfinite(v)) lt[k] = clampd(v, lt[k] - offs_lim, lt[k] + offs_lim);
            }
        }
        int p = jt->parent_index;
        if (p < 0 || p >= j) {
            memcpy(wrot[j], lq, sizeof lq);
            memcpy(wpos[j], lt, sizeof lt);
        } else {
            double r[3];
            grb_quat_mul(wrot[p], lq, wrot[j]);
            grb_quat_rotate(wrot[p], lt, r);
            for (int k = 0; k < 3; k++) wpos[j][k] = wpos[p][k] + r[k];
        }
        grb_quat_normalize(wrot[j]);
        if (drop_arms && (j == P->upperarm_joint[0] || j == P->upperarm_joint[1])) {
            /* T-pose arms lie along +-X; swing each down about the world Z axis (left arm is +X: negative angle, right: positive) */
            const double Z[3] = {0, 0, 1};
            double q[4], res[4];
            grb_quat_from_axis_angle(Z, j == P->upperarm_joint[0] ? -arm_drop : arm_drop, q);
            grb_quat_mul(q, wrot[j], res);
            memcpy(wrot[j], res, sizeof res);
            grb_quat_normalize(wrot[j]);
        }
    }

    const double Y[3] = {0, 1, 0};
    double qface[4];
    grb_quat_from_axis_angle(Y, facing, qface);
    for (int s = 0; s < rr->seg_count; s++) {
        int j = rr->seg_joint[s];
        double offc[4], qb[4], rj[3], pm[3], pw[3], qw[4];
        grb_quat_conj(rr->seg_rot_offset[s], offc);
        grb_quat_mul(wrot[j], offc, qb);                        /* model-space body rotation: body_rot * offset = joint rot */
        grb_quat_rotate(qb, rr->seg_joint_local[s], rj);
        for (int k = 0; k < 3; k++) pm[k] = wpos[j][k] - rj[k]; /* model-space body centre */
        grb_quat_rotate(qface, pm, pw);
        grb_quat_mul(qface, qb, qw);
        grb_quat_normalize(qw);
        GrbBody *b = &rr->world.bodies[rr->seg_body[s]];
        b->pos[0] = pw[0] + tx; b->pos[1] = pw[1] + ty; b->pos[2] = pw[2] + tz;
        memcpy(b->rot, qw, sizeof qw);
        memset(b->vel, 0, sizeof b->vel);
        memset(b->omega, 0, sizeof b->omega);
        memset(b->force, 0, sizeof b->force);
        memset(b->torque, 0, sizeof b->torque);
        memcpy(b->prev_pos, b->pos, sizeof b->pos);
        memcpy(b->prev_rot, b->rot, sizeof b->rot);
        memset(b->pre_vel, 0, sizeof b->pre_vel);
        memset(b->pre_omega, 0, sizeof b->pre_omega);
    }
}

/* Re-seeds every hinge's unwrapped-angle bookkeeping from the placed pose, choosing the 2*pi branch nearest the middle of its
 * limit range (so a pose placed at e.g. -3.1 rad is read as 3.18, not as a valid in-range angle by accident). */
static void sync_hinges(RigidRagdoll *rr) {
    for (int s = 0; s < rr->seg_count; s++) {
        int gj = rr->seg_grb_joint[s];
        if (gj < 0) continue;
        GrbJoint *J = &rr->world.joints[gj];
        if (J->type != GRB_JOINT_HINGE) continue;
        J->angle_initialized = 0;
        grb_joint_sync_angle(&rr->world, gj, 0.5 * (J->lower + J->upper));
    }
}

static double lowest_surface_y(const RigidRagdoll *rr) { return rigid_ragdoll_lowest_point(rr); }  /* vs the y = 0 plane the template was built on */

static void shift_bodies_y(RigidRagdoll *rr, double dy) {
    for (int s = 0; s < rr->seg_count; s++) {
        GrbBody *b = &rr->world.bodies[rr->seg_body[s]];
        b->pos[1] += dy;
        b->prev_pos[1] += dy;
    }
}

/* ---------------------------------------------------------------- velocity helpers */

static void cap_body_speeds(GrbBody *b) {
    const double vcap = (double)ragdoll_speed_cap_mmps() / 1000.0;
    const double wcap = (double)ragdoll_omega_cap_mradps() / 1000.0;
    double v = v3len(b->vel), w = v3len(b->omega);
    if (v > vcap) { double s = vcap / v; for (int k = 0; k < 3; k++) b->vel[k] *= s; }
    if (w > wcap) { double s = wcap / w; for (int k = 0; k < 3; k++) b->omega[k] *= s; }
}

/* direction -> permille per axis in the world frame; unusable input is "no direction" (an explosion is thrown straight up) */
static void dir_permille(const double d[3], int blast, int out[3]) {
    double x = fin(d[0], 0.0), y = fin(d[1], 0.0), z = fin(d[2], 0.0);
    double m = fmax(fabs(x), fmax(fabs(y), fabs(z)));
    if (!(m > 1e-12)) {
        out[0] = 0; out[1] = blast ? 1000 : 0; out[2] = 0;
        return;
    }
    x /= m; y /= m; z /= m;                       /* scale first so a 1e300 component cannot overflow the length */
    double len = sqrt(x * x + y * y + z * z);
    out[0] = (int)lround(clampd(x / len * 1000.0, -1000.0, 1000.0));
    out[1] = (int)lround(clampd(y / len * 1000.0, -1000.0, 1000.0));
    out[2] = (int)lround(clampd(z / len * 1000.0, -1000.0, 1000.0));
}

static void apply_shove(RigidRagdoll *rr, const RagdollSpawn *sp, int kind, int damage, const double vel_in[3]) {
    int blast = (kind == RAGDOLL_KIND_EXPLOSION);
    int dpm[3];
    dir_permille(sp->dir, blast, dpm);
    int hit_bone;
    if (sp->hit_bone >= 0) hit_bone = ragdoll_bone(sp->hit_bone);
    else hit_bone = ragdoll_bone_from_hit(kind, sp->hit_type, ragdoll_height_permille(sp->hit_height_mm),
                                          ragdoll_lateral_permille(sp->hit_lateral_mm));
    int mag = ragdoll_impulse_mmps(kind, damage, sp->blast_dist_pm);
    const double vmax = (double)ragdoll_max_carry_mmps() / 1000.0;
    const double carry = (double)ragdoll_carry_permille(kind) / 1000.0;
    double cv[3];
    for (int k = 0; k < 3; k++) cv[k] = clampd(fin(vel_in[k], 0.0), -vmax, vmax) * carry;

    for (int s = 0; s < rr->seg_count; s++) {
        GrbBody *b = &rr->world.bodies[rr->seg_body[s]];
        int share = ragdoll_segment_share_permille(kind, hit_bone, s);
        int dvx = ragdoll_dv_mmps(mag, share, dpm[0]);
        int dvy = ragdoll_dv_mmps(mag, share, dpm[1]) + ragdoll_lift_mmps(kind, mag, share);
        int dvz = ragdoll_dv_mmps(mag, share, dpm[2]);
        b->vel[0] += (double)dvx / 1000.0 + cv[0];
        b->vel[1] += (double)dvy / 1000.0 + cv[1];
        b->vel[2] += (double)dvz / 1000.0 + cv[2];
    }
    /* lean kick on the hit segment about the horizontal axis perpendicular to the force: up x d_h tips a point above the pivot
       toward d_h (the rules' sign flips it for segments below the pelvis) */
    double hx = (double)dpm[0], hz = (double)dpm[2], hl = sqrt(hx * hx + hz * hz);
    int spin = ragdoll_spin_mradps(kind, mag, hit_bone);
    if (hl > 0.0 && spin != 0) {
        double w = (double)spin / 1000.0;
        GrbBody *b = &rr->world.bodies[rr->seg_body[hit_bone]];
        b->omega[0] += (hz / hl) * w;
        b->omega[2] += (-hx / hl) * w;
    }
    for (int s = 0; s < rr->seg_count; s++) cap_body_speeds(&rr->world.bodies[rr->seg_body[s]]);
}

/* ---------------------------------------------------------------- static-box contacts */

static int gather_boxes(const RagdollPool *P, const double lo[3], const double hi[3], RagdollAabb *out, int max_out) {
    const RagdollWorld *w = &P->world;
    int n = 0;
    if (max_out > RAGDOLL_QUERY_CAP) max_out = RAGDOLL_QUERY_CAP;
    if (w->query) {
        int got = w->query(w->user, lo, hi, out, max_out);
        n = got < 0 ? 0 : (got > max_out ? max_out : got);
    } else if (w->boxes && w->box_count > 0) {
        for (int i = 0; i < w->box_count && n < max_out; i++) {
            const RagdollAabb *b = &w->boxes[i];
            if (b->max[0] < lo[0] || b->min[0] > hi[0] || b->max[1] < lo[1] || b->min[1] > hi[1] ||
                b->max[2] < lo[2] || b->min[2] > hi[2]) continue;
            out[n++] = *b;
        }
    }
    int m = 0;                                   /* drop boxes a callback (or a bad list) made unusable */
    for (int i = 0; i < n; i++) {
        const RagdollAabb *b = &out[i];
        if (finite3(b->min) && finite3(b->max) && b->min[0] <= b->max[0] && b->min[1] <= b->max[1] && b->min[2] <= b->max[2]) {
            if (m != i) out[m] = out[i];
            m++;
        }
    }
    return m;
}

/* Sphere (c, r) vs a box: returns the penetration depth (> 0) and the push-out normal, or the (non-positive) separation. */
static double sphere_box(const double c[3], double r, const RagdollAabb *bx, double nrm[3]) {
    double q[3], d[3], d2 = 0.0;
    for (int k = 0; k < 3; k++) {
        q[k] = clampd(c[k], bx->min[k], bx->max[k]);
        d[k] = c[k] - q[k];
        d2 += d[k] * d[k];
    }
    if (d2 > 1e-18) {
        double dist = sqrt(d2);
        for (int k = 0; k < 3; k++) nrm[k] = d[k] / dist;
        return r - dist;                         /* > 0 penetrating, <= 0 separated by -value */
    }
    /* centre inside the box: leave through the nearest face */
    double best = 1e30;
    int axis = 0, sgn = 1;
    for (int k = 0; k < 3; k++) {
        double dlo = c[k] - bx->min[k], dhi = bx->max[k] - c[k];
        if (dlo < best) { best = dlo; axis = k; sgn = -1; }
        if (dhi < best) { best = dhi; axis = k; sgn = 1; }
    }
    nrm[0] = nrm[1] = nrm[2] = 0.0;
    nrm[axis] = (double)sgn;
    return r + best;
}

/* Pushes one capsule body out of the static boxes (3 sample spheres along its axis) and returns the smallest clearance (m, may be
 * negative) between any of its end-sphere surfaces and the floor / any box -- the "support gap" the calm rule needs. */
static double collide_body(const RagdollPool *P, GrbBody *b) {
    const double Y[3] = {0, 1, 0};
    const double r = b->radius, hh = b->half_height;
    const double push_cap = (double)ragdoll_max_push_mm() / 1000.0;
    double ax[3];
    grb_quat_rotate(b->rot, Y, ax);

    double gap = 1e9;
    RagdollAabb cand[RAGDOLL_QUERY_CAP];
    int ncand = 0;
    if (P->world.query || (P->world.boxes && P->world.box_count > 0)) {
        double lo[3], hi[3];
        for (int k = 0; k < 3; k++) {
            double e0 = b->pos[k] - ax[k] * hh, e1 = b->pos[k] + ax[k] * hh;
            lo[k] = fmin(e0, e1) - r - 0.1;
            hi[k] = fmax(e0, e1) + r + 0.1;
        }
        int cap = ragdoll_query_max();
        ncand = gather_boxes(P, lo, hi, cand, cap);
    }

    int touched = 0;
    for (int pass = 0; pass < 2; pass++) {       /* pass 0 resolves penetrations, pass 1 measures what is left */
        for (int si = -1; si <= 1; si++) {
            double c[3];
            for (int k = 0; k < 3; k++) c[k] = b->pos[k] + ax[k] * hh * (double)si;
            if (pass == 1 && si != 0) {          /* end-sphere floor clearance (the middle sample is never lower) */
                double g = (c[1] - r) - P->world.ground_y;
                if (g < gap) gap = g;
            }
            for (int i = 0; i < ncand; i++) {
                double nrm[3];
                double pen = sphere_box(c, r, &cand[i], nrm);
                if (pass == 1) {                 /* separation to this box (negative = still penetrating) */
                    double g = -pen;
                    if (g < gap) gap = g;
                    continue;
                }
                if (pen <= 0.0) continue;
                double push = pen > push_cap ? push_cap : pen;
                for (int k = 0; k < 3; k++) { b->pos[k] += nrm[k] * push; c[k] += nrm[k] * push; }
                double vn = b->vel[0] * nrm[0] + b->vel[1] * nrm[1] + b->vel[2] * nrm[2];
                if (vn < 0.0) for (int k = 0; k < 3; k++) b->vel[k] -= vn * nrm[k];
                if (!touched) {                  /* friction and spin damping once per body per tick */
                    touched = 1;
                    const double fr = (double)ragdoll_contact_friction_pct() / 100.0;
                    const double om = (double)ragdoll_contact_omega_pct() / 100.0;
                    double vn2 = b->vel[0] * nrm[0] + b->vel[1] * nrm[1] + b->vel[2] * nrm[2];
                    for (int k = 0; k < 3; k++) b->vel[k] = nrm[k] * vn2 + (b->vel[k] - nrm[k] * vn2) * fr;
                    for (int k = 0; k < 3; k++) b->omega[k] *= om;
                }
            }
        }
    }
    return gap;
}

/* ---------------------------------------------------------------- one fixed tick */

static int clamp_to_int(double v, double lo, double hi) { return (int)clampd(v, lo, hi); }

/* Returns 0 if the body set is broken (non-finite state, runaway, torn joints) and must be killed. */
static int guard_and_cap(const RagdollSlot *slot, RigidRagdoll *rr) {
    for (int s = 0; s < rr->seg_count; s++) {
        GrbBody *b = &rr->world.bodies[rr->seg_body[s]];
        if (!finite3(b->pos) || !finite4(b->rot) || !finite3(b->vel) || !finite3(b->omega)) return 0;
        double dx = b->pos[0] - slot->spawn_pos[0], dy = b->pos[1] - slot->spawn_pos[1], dz = b->pos[2] - slot->spawn_pos[2];
        if (sqrt(dx * dx + dy * dy + dz * dz) > RUNAWAY_DISTANCE) return 0;
        grb_quat_normalize(b->rot);
        cap_body_speeds(b);
    }
    double err = grb_max_joint_error(&rr->world);
    if (!isfinite(err) || err > MAX_JOINT_TEAR) return 0;
    return 1;
}

static void tick_active(RagdollPool *P, RagdollSlot *slot) {
    RigidRagdoll *rr = &slot->rr;
    rr->world.planes[0].offset = P->world.ground_y;
    rigid_ragdoll_step(rr, TICK_DT);

    double gap = 1e9;
    for (int s = 0; s < rr->seg_count; s++) {
        double g = collide_body(P, &rr->world.bodies[rr->seg_body[s]]);
        if (g < gap) gap = g;
    }
    /* gap now holds the smallest clearance to the floor (always measured) and to any box under the body */

    if (!guard_and_cap(slot, rr)) {
        slot->state = RAGDOLL_STATE_FREE;
        P->stats.bad_kills++;
        return;
    }

    /* "Calm" is judged on NET motion over the tick (pose now vs the pose one tick ago), not on the XPBD solver's per-substep
       velocities: those carry ~1 rad/s / ~2 mm/s of harmless constraint jitter on a body lying perfectly still (measured), while the
       net per-tick motion of the same body is ~0.01 rad/s -- two orders of magnitude cleaner. */
    double dmax = 0.0, amax = 0.0;
    for (int s = 0; s < rr->seg_count; s++) {
        const GrbBody *b = &rr->world.bodies[rr->seg_body[s]];
        double d[3] = {b->pos[0] - slot->tick_pos[s][0], b->pos[1] - slot->tick_pos[s][1], b->pos[2] - slot->tick_pos[s][2]};
        double dl = v3len(d);
        double c[4], dq[4];
        grb_quat_conj(slot->tick_rot[s], c);
        grb_quat_mul(b->rot, c, dq);
        double al = 2.0 * atan2(sqrt(dq[0] * dq[0] + dq[1] * dq[1] + dq[2] * dq[2]), fabs(dq[3]));
        if (dl > dmax) dmax = dl;
        if (al > amax) amax = al;
        memcpy(slot->tick_pos[s], b->pos, sizeof slot->tick_pos[s]);
        memcpy(slot->tick_rot[s], b->rot, sizeof slot->tick_rot[s]);
    }
    int lin_mmps = clamp_to_int(dmax * (double)RAGDOLL_TICK_HZ * 1000.0, 0.0, 1.0e9);
    int ang_mrad = clamp_to_int(amax * (double)RAGDOLL_TICK_HZ * 1000.0, 0.0, 1.0e9);
    int gap_mm = clamp_to_int(gap * 1000.0, -1.0e9, 1.0e9);

    slot->age++;
    if (ragdoll_calm_tick(lin_mmps, ang_mrad, gap_mm)) slot->calm_ticks++;
    else slot->calm_ticks = 0;

    if (ragdoll_should_freeze(slot->kind, slot->age, slot->calm_ticks)) {
        for (int s = 0; s < rr->seg_count; s++) {
            GrbBody *b = &rr->world.bodies[rr->seg_body[s]];
            memset(b->vel, 0, sizeof b->vel);
            memset(b->omega, 0, sizeof b->omega);
        }
        slot->state = RAGDOLL_STATE_FROZEN;
        slot->frozen_ticks = 0;
        slot->fade_pm = ragdoll_fade_permille(slot->kind, 0);
        P->stats.froze++;
    } else if (ragdoll_should_despawn(slot->kind, slot->age, 0)) {
        slot->state = RAGDOLL_STATE_FREE;
        P->stats.despawned++;
    }
}

static void tick_frozen(RagdollPool *P, RagdollSlot *slot) {
    slot->age++;
    slot->frozen_ticks++;
    if (ragdoll_should_despawn(slot->kind, slot->age, slot->frozen_ticks)) {
        slot->state = RAGDOLL_STATE_FREE;
        P->stats.despawned++;
    } else {
        slot->fade_pm = ragdoll_fade_permille(slot->kind, slot->frozen_ticks);
    }
}

static void pool_tick(RagdollPool *P) {
    P->stats.ticks_run++;
    for (int i = 0; i < P->capacity; i++) {
        RagdollSlot *s = &P->slots[i];
        if (s->state == RAGDOLL_STATE_ACTIVE) tick_active(P, s);
        else if (s->state == RAGDOLL_STATE_FROZEN) tick_frozen(P, s);
    }
}

void ragdoll_pool_step(RagdollPool *P, double dt_ms) {
    if (!P || !P->ready) return;
    double maxdt = (double)ragdoll_max_dt_ms();
    double dt = (dt_ms > 0.0) ? dt_ms : 0.0;           /* NaN and negatives both fail the comparison */
    if (dt > maxdt) dt = maxdt;
    int64_t dt_us = (int64_t)(dt * 1000.0 + 0.5);
    P->acc += dt_us * 64;
    int ticks = 0, cap = ragdoll_max_catchup_ticks();
    while (P->acc >= ACC_UNITS_PER_TICK && ticks < cap) {
        P->acc -= ACC_UNITS_PER_TICK;
        pool_tick(P);
        ticks++;
    }
    if (P->acc >= ACC_UNITS_PER_TICK) {                /* backlog beyond the cap: drop whole ticks, keep the sub-tick remainder */
        P->acc %= ACC_UNITS_PER_TICK;
        P->stats.frames_clipped++;
    }
}

/* ---------------------------------------------------------------- init / world / spawn */

int ragdoll_pool_init(RagdollPool *P, const GSkel *skel, int capacity) {
    if (!P) return 0;
    memset(P, 0, sizeof *P);
    if (!skel) return 0;
    P->skel = skel;
    P->capacity = (capacity <= 0 || capacity > RAGDOLL_POOL_SLOTS) ? RAGDOLL_POOL_SLOTS : capacity;
    if (!rigid_ragdoll_build(&P->tmpl, skel, (double)ragdoll_body_mass_kg())) return 0;
    if (P->tmpl.seg_count != RAGDOLL_BONE_COUNT) return 0;
    for (int b = 0; b < RAGDOLL_BONE_COUNT; b++) {
        int j = P->tmpl.seg_joint[b];
        if (j < 0 || j >= (int)skel->joint_count || strncmp(skel->joints[j].name, BONE_JOINT[b], GSKEL_NAME_LEN) != 0) return 0;
    }
    P->pelvis_joint = P->tmpl.seg_joint[RAGDOLL_BONE_PELVIS];
    P->upperarm_joint[0] = P->tmpl.seg_joint[RAGDOLL_BONE_UPPERARM_L];
    P->upperarm_joint[1] = P->tmpl.seg_joint[RAGDOLL_BONE_UPPERARM_R];
    P->world.ground_y = 0.0;
    P->ready = 1;
    return 1;
}

void ragdoll_pool_set_world(RagdollPool *P, const RagdollWorld *w) {
    if (!P || !w) return;
    P->world = *w;
    P->world.ground_y = clampd(fin(w->ground_y, 0.0), -RAGDOLL_WORLD_LIMIT, RAGDOLL_WORLD_LIMIT);
    if (P->world.box_count < 0) P->world.box_count = 0;
}

void ragdoll_pool_set_viewer(RagdollPool *P, double x, double y, double z) {
    if (!P) return;
    P->viewer[0] = clampd(fin(x, 0.0), -RAGDOLL_WORLD_LIMIT, RAGDOLL_WORLD_LIMIT);
    P->viewer[1] = clampd(fin(y, 0.0), -RAGDOLL_WORLD_LIMIT, RAGDOLL_WORLD_LIMIT);
    P->viewer[2] = clampd(fin(z, 0.0), -RAGDOLL_WORLD_LIMIT, RAGDOLL_WORLD_LIMIT);
    P->viewer_set = 1;
}

static int dist_mm_from_viewer(const RagdollPool *P, double x, double y, double z) {
    if (!P->viewer_set) return 0;
    double dx = x - P->viewer[0], dy = y - P->viewer[1], dz = z - P->viewer[2];
    double d = sqrt(dx * dx + dy * dy + dz * dz) * 1000.0;
    if (!(d >= 0.0)) return 0;
    return d > 200000.0 ? 200000 : (int)d;
}

static int slot_priority(const RagdollPool *P, const RagdollSlot *s) {
    if (s->state == RAGDOLL_STATE_FREE) return 0;
    const double *p = s->rr.world.bodies[s->rr.seg_body[RAGDOLL_BONE_PELVIS]].pos;
    return ragdoll_priority(s->state, s->age, dist_mm_from_viewer(P, p[0], p[1], p[2]), s->kind, s->local, s->fade_pm);
}

/* lowest FREE slot, else the lowest-priority resident (ties: the oldest) unless the newcomer scores strictly below it. */
static int pick_slot(RagdollPool *P, int newcomer_score, int *evicting) {
    *evicting = 0;
    for (int i = 0; i < P->capacity; i++)
        if (P->slots[i].state == RAGDOLL_STATE_FREE) return i;
    int victim = -1, vscore = 0;
    for (int i = 0; i < P->capacity; i++) {
        int sc = slot_priority(P, &P->slots[i]);
        if (victim < 0 || sc < vscore ||
            (sc == vscore && (uint32_t)(P->next_seq - P->slots[i].seq) > (uint32_t)(P->next_seq - P->slots[victim].seq))) {
            victim = i;
            vscore = sc;
        }
    }
    if (victim < 0 || !ragdoll_evict_ok(newcomer_score, vscore)) return -1;
    *evicting = 1;
    return victim;
}

int ragdoll_pool_spawn(RagdollPool *P, const RagdollSpawn *sp) {
    if (!P || !P->ready || !sp) return -1;
    const int kind = ragdoll_kind(sp->kind);
    const int damage = ragdoll_damage(sp->damage);
    const int local = (sp->local == 1);
    const double px = clampd(fin(sp->x, 0.0), -RAGDOLL_WORLD_LIMIT, RAGDOLL_WORLD_LIMIT);
    const double py = clampd(fin(sp->y, 0.0), -RAGDOLL_WORLD_LIMIT, RAGDOLL_WORLD_LIMIT);
    const double pz = clampd(fin(sp->z, 0.0), -RAGDOLL_WORLD_LIMIT, RAGDOLL_WORLD_LIMIT);
    const double facing = clampd(fin(sp->facing_rad, 0.0), -1.0e6, 1.0e6);

    int newcomer = ragdoll_priority(RAGDOLL_STATE_ACTIVE, 0, dist_mm_from_viewer(P, px, py, pz), kind, local, 1000);
    int evicting = 0;
    int idx = pick_slot(P, newcomer, &evicting);
    if (idx < 0) { P->stats.refused++; return -1; }
    if (evicting) P->stats.evicted++;

    RagdollSlot *slot = &P->slots[idx];
    memcpy(&slot->rr, &P->tmpl, sizeof slot->rr);
    RigidRagdoll *rr = &slot->rr;
    rr->world.planes[0].offset = P->world.ground_y;

    int use_default = (sp->pose_rot == NULL);
    place_bodies(P, rr, sp->pose_rot, sp->pose_trans, use_default, px, py, pz, facing);
    sync_hinges(rr);
    if (!use_default && rigid_ragdoll_limit_violation(rr) > RAGDOLL_POSE_MAX_VIOLATION_RAD) {
        P->stats.poses_rejected++;
        place_bodies(P, rr, NULL, NULL, 1, px, py, pz, facing);
        sync_hinges(rr);
    }
    /* start above the floor: a pose that begins below it is lifted out (bounded), never depenetrated violently by the solver */
    {
        double gap = lowest_surface_y(rr) - P->world.ground_y;
        if (gap < 0.0) shift_bodies_y(rr, fmin(-gap, (double)ragdoll_max_spawn_lift_mm() / 1000.0));
    }
    apply_shove(rr, sp, kind, damage, sp->vel);
    for (int b = 0; b < rr->seg_count; b++) {
        const GrbBody *g = &rr->world.bodies[rr->seg_body[b]];
        memcpy(slot->tick_pos[b], g->pos, sizeof slot->tick_pos[b]);
        memcpy(slot->tick_rot[b], g->rot, sizeof slot->tick_rot[b]);
    }

    slot->state = RAGDOLL_STATE_ACTIVE;
    slot->kind = kind;
    slot->local = local;
    slot->seq = P->next_seq++;
    slot->spawn_tick = sp->now_tick;
    slot->age = 0;
    slot->calm_ticks = 0;
    slot->frozen_ticks = 0;
    slot->fade_pm = 1000;
    slot->spawn_pos[0] = px; slot->spawn_pos[1] = py; slot->spawn_pos[2] = pz;
    P->stats.spawned++;
    return idx;
}

/* ---------------------------------------------------------------- queries */

int ragdoll_pool_get_pose(const RagdollPool *P, int slot, float *out_rot, float *out_trans) {
    if (!P || !P->ready || !out_rot || !out_trans || slot < 0 || slot >= P->capacity) return 0;
    const RagdollSlot *s = &P->slots[slot];
    if (s->state == RAGDOLL_STATE_FREE) return 0;
    float pel[3];
    rigid_ragdoll_pose(&s->rr, out_rot, pel);
    const GSkel *sk = P->skel;
    for (uint32_t j = 0; j < sk->joint_count; j++)
        for (int k = 0; k < 3; k++) out_trans[j * 3 + k] = sk->joints[j].rest_translation[k];
    for (int k = 0; k < 3; k++) out_trans[P->pelvis_joint * 3 + k] = pel[k];
    return 1;
}

int ragdoll_pool_get_skin_matrices(const RagdollPool *P, int slot, float out[][16]) {
    if (!P || !P->ready || !out) return 0;
    float rot[GSKEL_MAX_JOINTS * 4], trans[GSKEL_MAX_JOINTS * 3];
    if (!ragdoll_pool_get_pose(P, slot, rot, trans)) return 0;
    gpose_compute_skin_matrices(P->skel, rot, trans, out);
    return 1;
}

int ragdoll_pool_info(const RagdollPool *P, int slot, RagdollInfo *out) {
    if (!P || !P->ready || !out || slot < 0 || slot >= P->capacity) return 0;
    const RagdollSlot *s = &P->slots[slot];
    memset(out, 0, sizeof *out);
    out->state = s->state;
    out->kind = s->kind;
    out->local = s->local;
    out->age = s->age;
    out->frozen_ticks = s->frozen_ticks;
    out->fade_permille = s->state == RAGDOLL_STATE_FREE ? 0 : s->fade_pm;
    out->priority = slot_priority(P, s);
    out->seq = s->seq;
    out->spawn_tick = s->spawn_tick;
    if (s->state != RAGDOLL_STATE_FREE) {
        const double *p = s->rr.world.bodies[s->rr.seg_body[RAGDOLL_BONE_PELVIS]].pos;
        for (int k = 0; k < 3; k++) out->pelvis[k] = p[k];
    }
    return 1;
}

int ragdoll_pool_count(const RagdollPool *P, int state) {
    if (!P || !P->ready) return 0;
    int n = 0;
    for (int i = 0; i < P->capacity; i++) if (P->slots[i].state == state) n++;
    return n;
}

void ragdoll_pool_clear(RagdollPool *P) {
    if (!P || !P->ready) return;
    for (int i = 0; i < P->capacity; i++) P->slots[i].state = RAGDOLL_STATE_FREE;
    P->acc = 0;
}
