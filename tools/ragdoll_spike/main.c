// ragdoll_spike/main.c — throwaway PBD ragdoll validation spike (S484, founder real-time:
// "can we add ragdoll and rigid body physics including stuff with mass? can we make the models
// based on the manequin we have for multiplayer?").
//
// NOT wired into any gameplay loop, NOT a shippable physics module -- a standalone CLI that
// loads the real mannequin_npc.gskel (the exact same skeleton already used to render real
// multiplayer players via SKIN_MANNEQUIN, see apps/lobby/src/main.c's gband_skel_npc_draw call),
// FKs its rest pose once to get real joint world positions + real bone lengths, then runs a
// minimal Verlet/PBD point-mass-plus-distance-constraint simulation (gravity + a ground plane)
// to see whether the approach settles into a plausible resting pose instead of exploding.
// Validates ONE architectural question before any real design doc gets written: can a
// determinism-friendly (SIM-100 SS2's own requirement -- "same clip + same seed + same tick =
// bit-identical pose") position-based solver produce a stable ragdoll on this engine's real
// skeleton data, cheaply, without pulling in a third-party physics engine. Throwaway if the
// answer is no.
//
// Deliberately minimal: distance constraints only (parent-child bone length, satisfied via
// Gauss-Seidel iteration), no angular/joint-limit constraints yet -- a real ragdoll needs those
// too (an elbow that can hyperextend backwards isn't acceptable for a shippable version) but
// they're a second, separable axis of the same architecture question, not needed to answer
// "does PBD on this skeleton settle or diverge."
//
// RESULT (real, run against the live mannequin_npc.gskel, 65 joints/64 constraints, 3s @ 64Hz):
// found and fixed two real bugs in the first cut (a Verlet ground-clamp velocity spike, and a
// sign error in the distance-constraint correction for body B -- see the two comments below,
// each marked with what broke and why) before the solver was stable at all. Once fixed: no NaN,
// no divergence, converges to a fixed configuration by t=0.5s -- the core Verlet/PBD approach IS
// numerically stable on this engine's real skeleton data at its real 64-tick rate, answering
// this spike's actual question yes. BUT it converges to every joint flat at y=0 -- distance-only
// constraints don't stop a knee/elbow from folding backward or a whole limb from going coplanar
// with the ground, since none of that violates a bone-LENGTH constraint. Conclusion: PBD is the
// right foundation, but angular/joint-limit constraints (not just distance) are a hard
// requirement for a believable ragdoll, not a nice-to-have -- that's the real next spike/design
// question, not per-bone collision shapes or mass tuning (those are comparatively minor next to
// getting joints to stop folding flat).
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "../../packages/goldenband/gskel.h"
#include "../../packages/goldenband/gpose.h"

#define MAX_JOINTS GSKEL_MAX_JOINTS

// Real SHANKPIT fixed tick rate (apps/server/src/main.c runs its own sim loop at 64Hz -- see
// packages/common/physics.h's own dt_scale normalization against 0.016f elsewhere in this repo).
#define TICK_HZ 64.0f
#define DT (1.0f / TICK_HZ)

// Gravity: no single named "GRAVITY" constant exists for the humanoid player path in
// physics.h today (checked directly -- only GRAVITY_FLOAT/GRAVITY_DROP for the floating-state
// and BUGGY_GRAVITY for the vehicle both exist, no generic one), so this is a deliberate,
// labeled spike approximation matching GRAVITY_FLOAT's own order of magnitude (units/tick^2 at
// 64 ticks/sec), not a value pulled from the real player fall code.
#define GRAVITY_PER_TICK2 0.03f

#define SOLVER_ITERATIONS 8
#define SIM_SECONDS 3.0f
#define PRINT_EVERY_N_TICKS 16 // 4x/sec

typedef struct {
    float pos[3];
    float prev_pos[3];
    float inv_mass; // 0 = pinned (infinite mass)
    char name[GSKEL_NAME_LEN];
} Body;

typedef struct {
    int a, b;       // body indices
    float rest_len;
} DistanceConstraint;

static float vlen(const float a[3], const float b[3]) {
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

int main(void) {
    GSkel skel;
    // Path relative to SHANKPIT repo root -- run this from the repo root the same way every
    // other GOLDENBAND consumer here already expects (apps/lobby's own "assets/goldenband"
    // relative load path, matched here rather than inventing a new asset-root convention).
    if (!gskel_init("assets/goldenband/mannequin_npc.gskel", &skel)) {
        fprintf(stderr, "ragdoll_spike: failed to load mannequin_npc.gskel\n");
        return 1;
    }
    printf("loaded mannequin_npc.gskel: %u joints\n", skel.joint_count);

    // FK the rest pose once (rest_rotation/rest_translation per joint, exactly the values
    // gpose_compute_joint_world already expects -- same shape gseq_player_sample_pose produces
    // for an animated pose, we just pass the skeleton's own bind/rest values straight through).
    float pose_rot[MAX_JOINTS * 4];
    float pose_trans[MAX_JOINTS * 3];
    for (uint32_t j = 0; j < skel.joint_count; j++) {
        memcpy(&pose_rot[j * 4], skel.joints[j].rest_rotation, 4 * sizeof(float));
        memcpy(&pose_trans[j * 3], skel.joints[j].rest_translation, 3 * sizeof(float));
    }
    static float joint_world[MAX_JOINTS][16];
    gpose_compute_joint_world(&skel, pose_rot, pose_trans, joint_world);

    // One Body per joint -- position = that joint's real rest-pose world translation (column-
    // major Mat4, translation is m[12..14]).
    static Body bodies[MAX_JOINTS];
    static DistanceConstraint constraints[MAX_JOINTS];
    int constraint_count = 0;

    float min_y = 1e9f;
    for (uint32_t j = 0; j < skel.joint_count; j++) {
        bodies[j].pos[0] = joint_world[j][12];
        bodies[j].pos[1] = joint_world[j][13];
        bodies[j].pos[2] = joint_world[j][14];
        memcpy(bodies[j].prev_pos, bodies[j].pos, sizeof(bodies[j].pos));
        bodies[j].inv_mass = 1.0f; // uniform 1kg-equivalent point mass -- real per-bone mass
                                    // estimation (volume-from-capsule-radius * density) is real
                                    // future work, not needed to answer the settle-or-explode
                                    // question this spike exists for.
        strncpy(bodies[j].name, skel.joints[j].name, GSKEL_NAME_LEN - 1);
        if (bodies[j].pos[1] < min_y) min_y = bodies[j].pos[1];

        int32_t parent = skel.joints[j].parent_index;
        if (parent >= 0) {
            float rest_len = vlen(bodies[j].pos, bodies[parent].pos);
            // A zero-length bone (two joints sharing one world position, e.g. an end-effector
            // stub) can't be a distance constraint -- skip it rather than divide-by-zero-ish
            // degenerate behavior later.
            if (rest_len > 0.0001f) {
                constraints[constraint_count].a = j;
                constraints[constraint_count].b = (int)parent;
                constraints[constraint_count].rest_len = rest_len;
                constraint_count++;
            }
        }
    }
    printf("built %d bodies, %d distance constraints\n", skel.joint_count, constraint_count);
    printf("rest-pose lowest joint y = %.3f (ground plane assumed at y=0)\n", min_y);

    // Lift the whole ragdoll above the ground plane before dropping it -- the rest pose's own
    // min_y might already be at/near 0 (a standing character), and we want a real drop-and-settle
    // test, not a zero-height no-op.
    float lift = 8.0f - min_y;
    for (uint32_t j = 0; j < skel.joint_count; j++) {
        bodies[j].pos[1] += lift;
        bodies[j].prev_pos[1] += lift;
    }

    int total_ticks = (int)(SIM_SECONDS * TICK_HZ);
    for (int tick = 0; tick < total_ticks; tick++) {
        // Verlet integration: implicit velocity via (pos - prev_pos), no separate velocity
        // array needed -- standard PBD/Verlet ragdoll technique, real reason it's the usual
        // choice for this exact problem (cheap, unconditionally stable for stiff constraints
        // compared to explicit-velocity Euler + separate constraint impulses).
        for (uint32_t j = 0; j < skel.joint_count; j++) {
            Body *b = &bodies[j];
            float vx = b->pos[0] - b->prev_pos[0];
            float vy = b->pos[1] - b->prev_pos[1];
            float vz = b->pos[2] - b->prev_pos[2];
            b->prev_pos[0] = b->pos[0];
            b->prev_pos[1] = b->pos[1];
            b->prev_pos[2] = b->pos[2];
            b->pos[0] += vx;
            b->pos[1] += vy - GRAVITY_PER_TICK2;
            b->pos[2] += vz;
        }

        // Gauss-Seidel distance-constraint relaxation, several iterations per tick for
        // stability (single-pass PBD on a chain this deep -- ~20 joints, several long bone
        // chains -- would visibly "stretch" under gravity; iterating converges it).
        for (int iter = 0; iter < SOLVER_ITERATIONS; iter++) {
            for (int c = 0; c < constraint_count; c++) {
                Body *ba = &bodies[constraints[c].a];
                Body *bb = &bodies[constraints[c].b];
                float diff[3] = {ba->pos[0] - bb->pos[0], ba->pos[1] - bb->pos[1], ba->pos[2] - bb->pos[2]};
                float len = sqrtf(diff[0] * diff[0] + diff[1] * diff[1] + diff[2] * diff[2]);
                if (len < 0.0001f) continue;
                float err = (len - constraints[c].rest_len) / len;
                float w_sum = ba->inv_mass + bb->inv_mass;
                if (w_sum < 0.0001f) continue;
                // Real, found-live sign bug in the first cut: with diff = pa - pb (so diff
                // points from b toward a), the correct PBD update pushes BOTH bodies along
                // +diff*corr when stretched (a moves toward b along -diff, b moves toward a
                // along +diff) -- verified by deriving delta_p_i = -invMass_i/sum(invMass) * C *
                // gradient_i against this constraint's own C(pa,pb)=|pa-pb|-restLen by hand. The
                // original `bb->pos -= diff*corr_b` pushed b AWAY from a on a stretched bone --
                // i.e. every relaxation iteration made the violation worse, not better, which is
                // exactly what produced the tick-16 NaN blowup (confirmed: this fix alone took
                // the sim from exploding within 0.25s to a real settle, see this file's own
                // header comment / EMILY/BACKLOG.md S484 for the before/after).
                float corr_a = -ba->inv_mass / w_sum * err;
                float corr_b = bb->inv_mass / w_sum * err;
                for (int k = 0; k < 3; k++) {
                    ba->pos[k] += diff[k] * corr_a;
                    bb->pos[k] += diff[k] * corr_b;
                }
            }
            // Ground plane constraint (y >= 0), same relaxation pass so it cooperates with the
            // distance constraints instead of fighting them one full tick behind. Real, found-
            // live bug in the first cut of this spike: clamping pos.y without also clamping
            // prev_pos.y left Verlet's implicit velocity (pos - prev_pos) reading whatever
            // overshoot gravity+constraints produced BELOW the floor that tick -- next tick
            // that "velocity" fires as a teleport, which then blows up through the whole
            // constraint chain (confirmed: original version went to NaN within 16 ticks). Also
            // clamping prev_pos.y removes the implied vertical velocity, so a body actually
            // rests instead of springing back through the floor.
            for (uint32_t j = 0; j < skel.joint_count; j++) {
                if (bodies[j].pos[1] < 0.0f) {
                    bodies[j].pos[1] = 0.0f;
                    bodies[j].prev_pos[1] = 0.0f;
                }
            }
        }

        if (tick % PRINT_EVERY_N_TICKS == 0) {
            // Track min/max/avg Y and the single largest per-tick displacement as a cheap
            // divergence signal -- a healthy settle should show these numbers shrinking toward
            // a steady resting spread, not growing unbounded or hitting NaN.
            float min_yt = 1e9f, max_yt = -1e9f, sum_y = 0.0f;
            for (uint32_t j = 0; j < skel.joint_count; j++) {
                float y = bodies[j].pos[1];
                if (y < min_yt) min_yt = y;
                if (y > max_yt) max_yt = y;
                sum_y += y;
            }
            printf("tick %4d (t=%.2fs): y min=%.3f max=%.3f avg=%.3f\n",
                   tick, tick * DT, min_yt, max_yt, sum_y / skel.joint_count);
        }
    }

    printf("\nfinal per-joint positions:\n");
    for (uint32_t j = 0; j < skel.joint_count; j++) {
        printf("  %-24s (%7.3f, %7.3f, %7.3f)\n", bodies[j].name,
               bodies[j].pos[0], bodies[j].pos[1], bodies[j].pos[2]);
    }
    return 0;
}
