/* rigid_ragdoll.h -- a real rigid-body ragdoll for the multiplayer mannequin skeleton
 * (assets/goldenband/mannequin_npc.gskel), built on GOLDEN BAND's grb XPBD physics
 * (packages/goldenband/grb.{h,c}, vendored from the GOLDENBAND repo).
 *
 * Founder real-time, 2026-09-27: "upgrade shankpit and nock to formal rigid body physics". This
 * is RAGDOLL_ORIENTATION_NORTHSTAR.md's own "v1: real per-bone orientation state + twist limits"
 * -- the thing tools/ragdoll_spike proved point-mass PBD can never reach ("no concept of
 * orientation"). Every major bone is now a 6-DOF rigid capsule (quaternion orientation, real
 * inertia), connected by XPBD ball joints with swing-cone + twist limits (hips, shoulders, spine,
 * neck, wrists, ankles) and one-way hinges for knees and elbows -- so a knee cannot hyperextend or
 * swing sideways, which is precisely the "poker-straight starfish" failure the spike documented.
 *
 * Masses are real human body-segment parameters, not bone-length guesses: segment mass fractions
 * from D. A. Winter, "Biomechanics and Motor Control of Human Movement" (4th ed., Table 4.1,
 * after Dempster 1955), applied to a chosen total body mass. The skeleton is authored in meters,
 * so kilograms and meters are physically consistent. Inertias use a uniform-density capsule per
 * segment (a named approximation -- Winter's radii of gyration are the next refinement).
 *
 * Named v0 limits: no limb-vs-limb or limb-vs-world collision other than the ground plane (grb
 * v0 has ground planes only); fingers, toes, clavicles and leaf bones are not simulated -- they
 * ride rigidly on their parent segment at rest orientation; hip/shoulder cones are symmetric
 * (real hips flex ~120 deg but extend ~20 deg).
 */
#ifndef SHANKPIT_RIGID_RAGDOLL_H
#define SHANKPIT_RIGID_RAGDOLL_H

#include "../goldenband/gskel.h"
#include "../goldenband/grb.h"

#define RAGDOLL_MAX_SEGMENTS 20

typedef struct {
    const GSkel *skel;
    GrbWorld world;
    int seg_count;
    int seg_joint[RAGDOLL_MAX_SEGMENTS];       /* skeleton joint whose bone this segment is */
    int seg_body[RAGDOLL_MAX_SEGMENTS];
    int seg_parent[RAGDOLL_MAX_SEGMENTS];      /* parent segment, -1 for the pelvis */
    int seg_grb_joint[RAGDOLL_MAX_SEGMENTS];   /* grb joint to the parent segment, -1 for the pelvis */
    double seg_mass[RAGDOLL_MAX_SEGMENTS];
    double seg_rot_offset[RAGDOLL_MAX_SEGMENTS][4]; /* joint_world_rot = body_rot * offset */
    double seg_joint_local[RAGDOLL_MAX_SEGMENTS][3]; /* the joint's origin, in the body frame */
    int joint_seg[GSKEL_MAX_JOINTS];           /* segment driving each skeleton joint, -1 = rides its parent */
    double rest_world_rot[GSKEL_MAX_JOINTS][4];
    double rest_world_pos[GSKEL_MAX_JOINTS][3];
    double total_mass;
} RigidRagdoll;

/* Builds the ragdoll standing in the skeleton's rest pose (y-up, ground plane y = 0). Returns 1
 * on success, 0 if a required mannequin joint (pelvis, spine_01..03, neck_01, Head, upperarm/
 * lowerarm/hand, thigh/calf/foot/ball _l/_r) is missing. */
int rigid_ragdoll_build(RigidRagdoll *r, const GSkel *skel, double total_mass_kg);

/* Adds a world-space linear/angular velocity to one segment (a hit, a shove). */
void rigid_ragdoll_push(RigidRagdoll *r, const char *joint_name, const double vel[3], const double omega[3]);

void rigid_ragdoll_step(RigidRagdoll *r, double dt);

/* Current pose as skeleton-local quaternions (x,y,z,w per joint, skel->joint_count entries) --
 * the exact shape gpose_compute_joint_world / gseq consume -- plus the pelvis's local
 * translation (relative to its parent, the root). */
void rigid_ragdoll_pose(const RigidRagdoll *r, float *out_local_rot, float out_pelvis_local_pos[3]);

/* Diagnostics for tests: worst joint-limit violation (rad) across every ragdoll joint right now,
 * lowest point of any capsule relative to the ground (m, negative = penetration). */
double rigid_ragdoll_limit_violation(const RigidRagdoll *r);
double rigid_ragdoll_lowest_point(const RigidRagdoll *r);

#endif
