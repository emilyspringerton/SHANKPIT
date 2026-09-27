/* rigid_ragdoll.c -- see rigid_ragdoll.h. */
#include "rigid_ragdoll.h"

#include <math.h>
#include <string.h>

#include "../goldenband/gpose.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef enum { RJ_BALL, RJ_HINGE } RagJointKind;

typedef struct {
    const char *joint;      /* skeleton joint where the segment (bone) starts */
    const char *end;        /* skeleton joint where it ends */
    double extend;          /* extra length past `end` along the bone (head top, fingertips) */
    double mass_fraction;   /* Winter Table 4.1 */
    double radius;          /* capsule radius, m */
    RagJointKind kind;      /* joint to the parent segment */
    double swing, twist;    /* BALL: cone half-angle, +/- twist (rad) */
    double hinge_axis[3];   /* HINGE: world axis at rest (positive angle = anatomical flexion) */
    double lower, upper;    /* HINGE limits (rad) */
} SegmentDef;

/* Winter (after Dempster): head+neck .081; trunk .497 = thorax .216 + abdomen .139 + pelvis
 * .142; upper arm .028; forearm .016; hand .006; thigh .100; leg .0465; foot .0145 -- sums to
 * exactly 1.000 over the whole body. The abdomen is split evenly across the two lumbar bones. */
static const SegmentDef SEGMENTS[] = {
    {"pelvis",     "spine_01",    0.00, 0.142,  0.11,  RJ_BALL,  0,    0,    {0, 0, 0},  0, 0},
    {"spine_01",   "spine_02",    0.00, 0.0695, 0.11,  RJ_BALL,  0.30, 0.25, {0, 0, 0},  0, 0},
    {"spine_02",   "spine_03",    0.00, 0.0695, 0.11,  RJ_BALL,  0.30, 0.25, {0, 0, 0},  0, 0},
    {"spine_03",   "neck_01",     0.00, 0.216,  0.12,  RJ_BALL,  0.25, 0.25, {0, 0, 0},  0, 0},
    {"neck_01",    "Head",        0.14, 0.081,  0.09,  RJ_BALL,  0.70, 0.80, {0, 0, 0},  0, 0},
    {"upperarm_l", "lowerarm_l",  0.00, 0.028,  0.045, RJ_BALL,  1.60, 1.20, {0, 0, 0},  0, 0},
    {"upperarm_r", "lowerarm_r",  0.00, 0.028,  0.045, RJ_BALL,  1.60, 1.20, {0, 0, 0},  0, 0},
    {"lowerarm_l", "hand_l",      0.00, 0.016,  0.035, RJ_HINGE, 0,    0,    {0, -1, 0}, -0.05, 2.50},
    {"lowerarm_r", "hand_r",      0.00, 0.016,  0.035, RJ_HINGE, 0,    0,    {0, 1, 0},  -0.05, 2.50},
    {"hand_l",     "middle_01_l", 0.08, 0.006,  0.03,  RJ_BALL,  0.90, 0.30, {0, 0, 0},  0, 0},
    {"hand_r",     "middle_01_r", 0.08, 0.006,  0.03,  RJ_BALL,  0.90, 0.30, {0, 0, 0},  0, 0},
    {"thigh_l",    "calf_l",      0.00, 0.100,  0.07,  RJ_BALL,  1.30, 0.50, {0, 0, 0},  0, 0},
    {"thigh_r",    "calf_r",      0.00, 0.100,  0.07,  RJ_BALL,  1.30, 0.50, {0, 0, 0},  0, 0},
    {"calf_l",     "foot_l",      0.00, 0.0465, 0.05,  RJ_HINGE, 0,    0,    {1, 0, 0},  -0.05, 2.40},
    {"calf_r",     "foot_r",      0.00, 0.0465, 0.05,  RJ_HINGE, 0,    0,    {1, 0, 0},  -0.05, 2.40},
    {"foot_l",     "ball_l",      0.00, 0.0145, 0.04,  RJ_BALL,  0.50, 0.30, {0, 0, 0},  0, 0},
    {"foot_r",     "ball_r",      0.00, 0.0145, 0.04,  RJ_BALL,  0.50, 0.30, {0, 0, 0},  0, 0},
};
#define N_SEGMENTS ((int)(sizeof SEGMENTS / sizeof SEGMENTS[0]))

/* Passive joint damping ("unconscious muscle tone"), N*m*s/rad per kg of child segment. */
#define DAMPING_PER_KG 0.08

static void mat_to_quat(const float m[16], double q[4]) {
    /* column-major rotation part: element (row r, col c) = m[c*4 + r] */
    double r00 = m[0], r11 = m[5], r22 = m[10], r01 = m[4], r10 = m[1], r02 = m[8], r20 = m[2], r12 = m[9], r21 = m[6];
    double tr = r00 + r11 + r22;
    if (tr > 0) {
        double s = sqrt(tr + 1.0) * 2;
        q[3] = 0.25 * s; q[0] = (r21 - r12) / s; q[1] = (r02 - r20) / s; q[2] = (r10 - r01) / s;
    } else if (r00 > r11 && r00 > r22) {
        double s = sqrt(1.0 + r00 - r11 - r22) * 2;
        q[3] = (r21 - r12) / s; q[0] = 0.25 * s; q[1] = (r01 + r10) / s; q[2] = (r02 + r20) / s;
    } else if (r11 > r22) {
        double s = sqrt(1.0 + r11 - r00 - r22) * 2;
        q[3] = (r02 - r20) / s; q[0] = (r01 + r10) / s; q[1] = 0.25 * s; q[2] = (r12 + r21) / s;
    } else {
        double s = sqrt(1.0 + r22 - r00 - r11) * 2;
        q[3] = (r10 - r01) / s; q[0] = (r02 + r20) / s; q[1] = (r12 + r21) / s; q[2] = 0.25 * s;
    }
    grb_quat_normalize(q);
}

/* Shortest-arc rotation taking unit vector a onto unit vector b. */
static void quat_between(const double a[3], const double b[3], double q[4]) {
    double c = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    double ax[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    if (c < -1.0 + 1e-9) {
        double p[3] = {1, 0, 0};
        if (fabs(a[0]) > 0.9) { p[0] = 0; p[1] = 1; }
        double perp[3] = {a[1] * p[2] - a[2] * p[1], a[2] * p[0] - a[0] * p[2], a[0] * p[1] - a[1] * p[0]};
        grb_quat_from_axis_angle(perp, M_PI, q);
        return;
    }
    q[0] = ax[0]; q[1] = ax[1]; q[2] = ax[2]; q[3] = 1.0 + c;
    grb_quat_normalize(q);
}

static int find_seg(const RigidRagdoll *r, int joint) {
    for (int s = 0; s < r->seg_count; s++)
        if (r->seg_joint[s] == joint) return s;
    return -1;
}

int rigid_ragdoll_build(RigidRagdoll *r, const GSkel *skel, double total_mass_kg) {
    memset(r, 0, sizeof *r);
    r->skel = skel;
    grb_world_init(&r->world, 0, -9.81, 0);
    grb_world_add_plane(&r->world, 0, 1, 0, 0);
    /* 24 substeps (1536 Hz): measured on a 2.5 m/s chest shove, 16 lets a thigh landing on a
     * forearm push a joint ~4.7 deg past its limit for a few ticks; 24 halves that (~2.3 deg,
     * 1.2 mm drift) for ~0.5 ms per ragdoll per 64 Hz tick. */
    r->world.substeps = 24;

    /* Rest pose FK. */
    static float rot[GSKEL_MAX_JOINTS * 4], tr[GSKEL_MAX_JOINTS * 3], W[GSKEL_MAX_JOINTS][16];
    for (uint32_t j = 0; j < skel->joint_count; j++) {
        memcpy(&rot[j * 4], skel->joints[j].rest_rotation, 4 * sizeof(float));
        memcpy(&tr[j * 3], skel->joints[j].rest_translation, 3 * sizeof(float));
    }
    gpose_compute_joint_world(skel, rot, tr, W);
    for (uint32_t j = 0; j < skel->joint_count; j++) {
        mat_to_quat(W[j], r->rest_world_rot[j]);
        for (int k = 0; k < 3; k++) r->rest_world_pos[j][k] = W[j][12 + k];
        r->joint_seg[j] = -1;
    }

    const double Y[3] = {0, 1, 0}, Z[3] = {0, 0, 1};
    for (int s = 0; s < N_SEGMENTS; s++) {
        const SegmentDef *d = &SEGMENTS[s];
        int j = gskel_find_joint(skel, d->joint), e = gskel_find_joint(skel, d->end);
        if (j < 0 || e < 0) return 0;
        const double *p0 = r->rest_world_pos[j], *p1 = r->rest_world_pos[e];
        double dir[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
        double len = sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        if (len < 1e-6) return 0;
        for (int k = 0; k < 3; k++) dir[k] /= len;
        len += d->extend;
        double center[3] = {p0[0] + dir[0] * len / 2, p0[1] + dir[1] * len / 2, p0[2] + dir[2] * len / 2};
        double q[4];
        quat_between(Y, dir, q);
        double mass = total_mass_kg * d->mass_fraction;
        double hh = len / 2 - d->radius;
        if (hh < 0.01) hh = 0.01;
        double I[3];
        grb_inertia_capsule(mass, d->radius, hh, I);
        int b = grb_body_add(&r->world, mass, I, center, q);
        GrbBody *body = &r->world.bodies[b];
        grb_body_set_capsule(body, d->radius, hh);
        body->friction = 0.8;
        r->seg_joint[r->seg_count] = j;
        r->seg_body[r->seg_count] = b;
        r->seg_mass[r->seg_count] = mass;
        r->total_mass += mass;
        double qc[4];
        grb_quat_conj(q, qc);
        grb_quat_mul(qc, r->rest_world_rot[j], r->seg_rot_offset[r->seg_count]);
        double rel[3] = {p0[0] - center[0], p0[1] - center[1], p0[2] - center[2]};
        grb_quat_rotate(qc, rel, r->seg_joint_local[r->seg_count]);
        r->joint_seg[j] = r->seg_count;
        r->seg_count++;
    }

    /* Connect each segment to the nearest ancestor segment, at the segment's own joint. */
    for (int s = 0; s < r->seg_count; s++) {
        const SegmentDef *d = &SEGMENTS[s];
        int j = r->seg_joint[s], a = skel->joints[j].parent_index, ps = -1;
        while (a >= 0 && (ps = find_seg(r, a)) < 0) a = skel->joints[a].parent_index;
        r->seg_parent[s] = ps;
        r->seg_grb_joint[s] = -1;
        if (ps < 0) continue;
        const double *p0 = r->rest_world_pos[j];
        int e = gskel_find_joint(skel, d->end);
        double dir[3] = {r->rest_world_pos[e][0] - p0[0], r->rest_world_pos[e][1] - p0[1], r->rest_world_pos[e][2] - p0[2]};
        double l = sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        for (int k = 0; k < 3; k++) dir[k] /= l;
        double frame[4];
        quat_between(Z, d->kind == RJ_HINGE ? d->hinge_axis : dir, frame);
        int gj = grb_joint_add(&r->world, d->kind == RJ_HINGE ? GRB_JOINT_HINGE : GRB_JOINT_BALL,
                               r->seg_body[ps], r->seg_body[s], p0, frame);
        GrbJoint *J = &r->world.joints[gj];
        if (d->kind == RJ_HINGE) {
            J->has_limits = 1;
            J->lower = d->lower;
            J->upper = d->upper;
        } else {
            J->swing_max = d->swing;
            J->twist_lower = -d->twist;
            J->twist_upper = d->twist;
        }
        J->damping = DAMPING_PER_KG * r->seg_mass[s];
        r->seg_grb_joint[s] = gj;
    }
    return 1;
}

void rigid_ragdoll_push(RigidRagdoll *r, const char *name, const double vel[3], const double omega[3]) {
    int j = gskel_find_joint(r->skel, name);
    if (j < 0 || r->joint_seg[j] < 0) return;
    GrbBody *b = &r->world.bodies[r->seg_body[r->joint_seg[j]]];
    for (int k = 0; k < 3; k++) {
        if (vel) b->vel[k] += vel[k];
        if (omega) b->omega[k] += omega[k];
    }
}

void rigid_ragdoll_step(RigidRagdoll *r, double dt) { grb_world_step(&r->world, dt); }

void rigid_ragdoll_pose(const RigidRagdoll *r, float *out_rot, float out_pelvis[3]) {
    const GSkel *sk = r->skel;
    static double wrot[GSKEL_MAX_JOINTS][4], wpos[GSKEL_MAX_JOINTS][3];
    for (uint32_t j = 0; j < sk->joint_count; j++) {
        int s = r->joint_seg[j];
        int p = sk->joints[j].parent_index;
        if (s >= 0) {
            const GrbBody *b = &r->world.bodies[r->seg_body[s]];
            grb_quat_mul(b->rot, r->seg_rot_offset[s], wrot[j]);
            grb_body_world_point(b, r->seg_joint_local[s], wpos[j]);
        } else if (p < 0) {
            memcpy(wrot[j], r->rest_world_rot[j], sizeof wrot[j]);
            memcpy(wpos[j], r->rest_world_pos[j], sizeof wpos[j]);
        } else {
            /* rides its parent rigidly at the rest local transform */
            double lr[4], lt[3], t[3];
            for (int k = 0; k < 4; k++) lr[k] = sk->joints[j].rest_rotation[k];
            for (int k = 0; k < 3; k++) lt[k] = sk->joints[j].rest_translation[k];
            grb_quat_mul(wrot[p], lr, wrot[j]);
            grb_quat_rotate(wrot[p], lt, t);
            for (int k = 0; k < 3; k++) wpos[j][k] = wpos[p][k] + t[k];
        }
        double local[4];
        if (p < 0) {
            memcpy(local, wrot[j], sizeof local);
        } else {
            double c[4];
            grb_quat_conj(wrot[p], c);
            grb_quat_mul(c, wrot[j], local);
        }
        if (local[3] < 0) for (int k = 0; k < 4; k++) local[k] = -local[k];
        for (int k = 0; k < 4; k++) out_rot[j * 4 + k] = (float)local[k];
    }
    int pel = gskel_find_joint(sk, "pelvis");
    if (pel >= 0 && out_pelvis) {
        int p = sk->joints[pel].parent_index;
        double d[3], c[4], l[3];
        for (int k = 0; k < 3; k++) d[k] = wpos[pel][k] - (p >= 0 ? wpos[p][k] : 0);
        if (p >= 0) grb_quat_conj(wrot[p], c); else { c[0] = c[1] = c[2] = 0; c[3] = 1; }
        grb_quat_rotate(c, d, l);
        for (int k = 0; k < 3; k++) out_pelvis[k] = (float)l[k];
    }
}

double rigid_ragdoll_limit_violation(const RigidRagdoll *r) {
    double worst = 0.0;
    const double X[3] = {1, 0, 0}, Z[3] = {0, 0, 1};
    for (int s = 0; s < r->seg_count; s++) {
        int gj = r->seg_grb_joint[s];
        if (gj < 0) continue;
        const GrbJoint *J = &r->world.joints[gj];
        if (J->type == GRB_JOINT_HINGE) {
            double v = J->angle < J->lower ? J->lower - J->angle : (J->angle > J->upper ? J->angle - J->upper : 0);
            if (v > worst) worst = v;
            continue;
        }
        const GrbBody *A = &r->world.bodies[J->body_a], *B = &r->world.bodies[J->body_b];
        double qa[4], qb[4], za[3], zb[3], xa[3], xb[3];
        grb_quat_mul(A->rot, J->local_rot_a, qa);
        grb_quat_mul(B->rot, J->local_rot_b, qb);
        grb_quat_rotate(qa, Z, za);
        grb_quat_rotate(qb, Z, zb);
        double c = za[0] * zb[0] + za[1] * zb[1] + za[2] * zb[2];
        double swing = acos(c > 1 ? 1 : (c < -1 ? -1 : c));
        if (swing - J->swing_max > worst) worst = swing - J->swing_max;
        double n[3] = {za[0] + zb[0], za[1] + zb[1], za[2] + zb[2]};
        double nl = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (nl < 1e-9) continue;
        for (int k = 0; k < 3; k++) n[k] /= nl;
        grb_quat_rotate(qa, X, xa);
        grb_quat_rotate(qb, X, xb);
        double da = xa[0] * n[0] + xa[1] * n[1] + xa[2] * n[2], db = xb[0] * n[0] + xb[1] * n[1] + xb[2] * n[2];
        for (int k = 0; k < 3; k++) { xa[k] -= da * n[k]; xb[k] -= db * n[k]; }
        double cr[3] = {xa[1] * xb[2] - xa[2] * xb[1], xa[2] * xb[0] - xa[0] * xb[2], xa[0] * xb[1] - xa[1] * xb[0]};
        double twist = atan2(cr[0] * n[0] + cr[1] * n[1] + cr[2] * n[2], xa[0] * xb[0] + xa[1] * xb[1] + xa[2] * xb[2]);
        double v = twist < J->twist_lower ? J->twist_lower - twist : (twist > J->twist_upper ? twist - J->twist_upper : 0);
        if (v > worst) worst = v;
    }
    return worst;
}

double rigid_ragdoll_lowest_point(const RigidRagdoll *r) {
    double low = 1e9;
    for (int s = 0; s < r->seg_count; s++) {
        const GrbBody *b = &r->world.bodies[r->seg_body[s]];
        for (int e = -1; e <= 1; e += 2) {
            double lp[3] = {0, e * b->half_height, 0}, p[3];
            grb_body_world_point(b, lp, p);
            if (p[1] - b->radius < low) low = p[1] - b->radius;
        }
    }
    return low;
}
