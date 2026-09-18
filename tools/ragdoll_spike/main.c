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
//
// ITERATION 2 (S494, founder real-time: "just work on ragdoll physics" / "we are going to need
// to define masses for objects im sure for rigid body physics"). Added real per-bone mass
// (proportional to each joint's own parent-bone length -- a thigh genuinely outweighs a finger
// bone now, replacing the original uniform inv_mass=1) and a real bend constraint (a one-sided
// minimum-distance constraint between a joint and its own grandparent, the standard PBD technique
// for bend resistance -- see BendConstraint's own doc comment).
//
// RESULT, real and conclusive, and NOT the fix it looked like it would be: the bend constraint
// works exactly as designed -- verified directly, 0/63 constraints violated at the final settled
// state, every joint stays at least 85% extended from its own grandparent, real hyperextension/
// fold-back is genuinely prevented. But the whole body STILL collapses completely flat (every
// joint at y=0.000, spread out in the XZ plane) by t=0.75s, unchanged from iteration 1. Real,
// important, hard-won lesson: a fully-extended limb lying FLAT on the ground satisfies every
// distance AND bend constraint simultaneously -- neither constraint type has any concept of
// "orientation relative to gravity/up" at all, only relative distances BETWEEN points. This is a
// fundamental ceiling, not a tuning problem: NO number or combination of point-distance
// constraints (equality or one-sided) can ever stop a chain from lying flat, because that failure
// mode isn't a distance violation. A genuinely believable ragdoll needs actual per-BONE
// orientation state (not just per-joint position) and real angular/swing-twist joint limits
// constraining relative rotation between adjacent bones -- a categorically different, larger
// technique (true rigid-body dynamics with quaternion orientations) than point-mass PBD can reach
// by adding more constraints of the same kind. That's the real, now sharply-defined Phase 2 --
// not an incremental extension of what's built here.
//
// ITERATION 3 (S495, founder real-time: "we may as well see how far we can push the engine while
// we are building it"). Tested one more real, cheap hypothesis before committing to full
// orientation physics: nothing killed HORIZONTAL implied-velocity on ground contact, only
// vertical -- a grounded joint could slide sideways forever under a constraint correction's own
// pull. Added GROUND_FRICTION (kills tangential velocity at contact, the standard first-pass
// friction model). RESULT: real, measurable, but partial -- the Y-flatness is completely
// unchanged (still every joint at y=0.000 by t=0.75s), but the LATERAL spread tightened
// substantially (fingertip spread dropped from ~0.6-0.7 units to ~0.3-0.5) -- friction turns a
// splayed "starfish" into a more compact pile, real progress, but doesn't touch the underlying
// ceiling. Real, corrected framing reached in the same conversation: lying flat after falling
// isn't actually the bug (that's genuinely what happens to an unconscious body) -- the real,
// specific problem is the symmetric, POKER-STRAIGHT shape, since nothing resists a joint
// straightening toward full extension, only resists over-folding (the S494 bend constraint) or
// sliding after contact (this iteration). See docs2/RAGDOLL_ORIENTATION_NORTHSTAR.md for the
// real next design (swing-axis constraints derived from rest-pose geometry) -- this spike's own
// real job (validate hypotheses cheaply before committing to the bigger build) is done here.
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

// BendConstraint -- S494 follow-up to S484's own named next step ("angular/joint-limit
// constraints... are the actual next architecture question," per this file's own header
// comment/EMILY BACKLOG.md S484). A real, standard, lightweight PBD technique (used for bend
// resistance in rope/cloth sims): a ONE-SIDED minimum-distance constraint between a joint and its
// own grandparent (skipping the parent in between). Folding a knee/elbow backward brings the
// child joint CLOSE to its grandparent even though neither adjacent bone-LENGTH constraint is
// violated -- that's exactly the failure mode the spike's own real result found ("converges to
// every joint flat at y=0... distance-only constraints don't stop a knee/elbow folding backward").
// Unlike DistanceConstraint (an EQUALITY constraint, always pulls toward exact rest_len), this
// only ever pushes APART, and only when already closer than min_dist -- doing nothing once the
// joint is extended past that, so it never fights the natural, correct bend of a relaxed limb.
typedef struct {
    int a, b;
    float min_dist;
} BendConstraint;

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
    static BendConstraint bend_constraints[MAX_JOINTS];
    int constraint_count = 0;
    int bend_constraint_count = 0;

    // BONE_MASS_PER_UNIT_LENGTH/MIN_JOINT_MASS -- S494, founder real-time: "we are going to need
    // to define masses for objects im sure for rigid body physics." Real, deliberate, simplest
    // physically-motivated model that doesn't need a hand-authored per-rig mass table: a joint's
    // own mass is proportional to the length of the bone connecting it to ITS OWN parent (a
    // longer limb segment -- a thigh vs. a finger bone -- masses more), matching this file's own
    // established "no engine-specific calibration, a real relative-scale approximation" spirit.
    // Units are arbitrary (this is a relative-mass spike, not calibrated to real kilograms) --
    // what matters is a THIGH bone genuinely outweighing a FINGER bone, not the absolute number.
    // MIN_JOINT_MASS floors near-zero-length bones (leaf/stub joints) so they never become
    // absurdly light (a body with near-zero mass gets thrown around by every constraint
    // correction applied to it, real PBD instability, not just "unrealistic").
    #define BONE_MASS_PER_UNIT_LENGTH 1.0f
    #define MIN_JOINT_MASS 0.15f

    float min_y = 1e9f;
    for (uint32_t j = 0; j < skel.joint_count; j++) {
        bodies[j].pos[0] = joint_world[j][12];
        bodies[j].pos[1] = joint_world[j][13];
        bodies[j].pos[2] = joint_world[j][14];
        memcpy(bodies[j].prev_pos, bodies[j].pos, sizeof(bodies[j].pos));
        strncpy(bodies[j].name, skel.joints[j].name, GSKEL_NAME_LEN - 1);
        if (bodies[j].pos[1] < min_y) min_y = bodies[j].pos[1];

        int32_t parent = skel.joints[j].parent_index;
        float own_bone_len = 0.0f;
        if (parent >= 0) {
            own_bone_len = vlen(bodies[j].pos, bodies[parent].pos);
            // A zero-length bone (two joints sharing one world position, e.g. an end-effector
            // stub) can't be a distance constraint -- skip it rather than divide-by-zero-ish
            // degenerate behavior later.
            if (own_bone_len > 0.0001f) {
                constraints[constraint_count].a = j;
                constraints[constraint_count].b = (int)parent;
                constraints[constraint_count].rest_len = own_bone_len;
                constraint_count++;
            }

            // Bend constraint against the grandparent, if one exists -- see BendConstraint's own
            // doc comment. GSKEL_FORMAT's own real "parent_index < child index" invariant means
            // `parent` (and its own parent) are already fully positioned by this point in the
            // single forward pass -- no second pass needed.
            int32_t grandparent = skel.joints[parent].parent_index;
            if (grandparent >= 0) {
                float parent_bone_len = vlen(bodies[parent].pos, bodies[grandparent].pos);
                float full_extension = own_bone_len + parent_bone_len;
                if (full_extension > 0.0001f) {
                    bend_constraints[bend_constraint_count].a = j;
                    bend_constraints[bend_constraint_count].b = grandparent;
                    bend_constraints[bend_constraint_count].min_dist = full_extension * 0.85f;
                    bend_constraint_count++;
                }
            }
        }

        float mass = own_bone_len * BONE_MASS_PER_UNIT_LENGTH;
        if (mass < MIN_JOINT_MASS) mass = MIN_JOINT_MASS;
        bodies[j].inv_mass = 1.0f / mass;
    }
    printf("built %d bodies, %d distance constraints, %d bend constraints\n",
           skel.joint_count, constraint_count, bend_constraint_count);
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

            // Bend constraints -- S494, same Gauss-Seidel relaxation pass so they cooperate with
            // the distance constraints instead of fighting them one full tick behind. ONE-SIDED:
            // only corrects when the joint has folded CLOSER to its grandparent than min_dist
            // allows, pushing them apart; a limb already extended past that does nothing here.
            for (int c = 0; c < bend_constraint_count; c++) {
                Body *ba = &bodies[bend_constraints[c].a];
                Body *bb = &bodies[bend_constraints[c].b];
                float diff[3] = {ba->pos[0] - bb->pos[0], ba->pos[1] - bb->pos[1], ba->pos[2] - bb->pos[2]};
                float len = sqrtf(diff[0] * diff[0] + diff[1] * diff[1] + diff[2] * diff[2]);
                if (len < 0.0001f || len >= bend_constraints[c].min_dist) continue; // already far enough apart -- real no-op
                float err = (len - bend_constraints[c].min_dist) / len; // negative -- len is below min_dist
                float w_sum = ba->inv_mass + bb->inv_mass;
                if (w_sum < 0.0001f) continue;
                // Same real, hand-derived sign convention the distance-constraint fix above
                // already established (delta_p_i = -invMass_i/w_sum * C * gradient_i) -- err is
                // negative here (below the minimum), so this pushes a and b APART, correctly.
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
            // GROUND_FRICTION -- S495 test (founder real-time: "we may as well see how far we
            // can push the engine while we are building it"). Real hypothesis before committing
            // to full per-bone orientation: nothing above kills HORIZONTAL implied-velocity on
            // ground contact, only vertical -- a grounded joint can still slide sideways forever
            // under a constraint correction's own pull, every tick, with nothing to stop it. That
            // alone could plausibly explain (or meaningfully contribute to) the flat-puddle
            // spread, cheaper to test directly than to assume. 1.0 = full stop (real kinetic-
            // friction simplification, same "kill tangential velocity at contact" a first-pass
            // friction model commonly uses), 0.0 = frictionless (the original, pre-S495 behavior).
            #define GROUND_FRICTION 1.0f
            for (uint32_t j = 0; j < skel.joint_count; j++) {
                if (bodies[j].pos[1] < 0.0f) {
                    bodies[j].pos[1] = 0.0f;
                    bodies[j].prev_pos[1] = 0.0f;
                    bodies[j].prev_pos[0] = bodies[j].pos[0] - (bodies[j].pos[0] - bodies[j].prev_pos[0]) * (1.0f - GROUND_FRICTION);
                    bodies[j].prev_pos[2] = bodies[j].pos[2] - (bodies[j].pos[2] - bodies[j].prev_pos[2]) * (1.0f - GROUND_FRICTION);
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

    // Real verification, not an assumption: confirm the bend constraints are actually holding
    // (no grandparent pair closer than its own min_dist) at the final settled state -- proves
    // they're doing real work even though (see below) the whole-body flat-collapse persists for
    // a different, separate reason.
    int bend_violations = 0;
    float worst_violation = 0.0f;
    for (int c = 0; c < bend_constraint_count; c++) {
        float d = vlen(bodies[bend_constraints[c].a].pos, bodies[bend_constraints[c].b].pos);
        if (d < bend_constraints[c].min_dist - 0.01f) { // small tolerance for solver residual
            bend_violations++;
            float violation = bend_constraints[c].min_dist - d;
            if (violation > worst_violation) worst_violation = violation;
        }
    }
    printf("\nbend constraint check: %d/%d violated at final state (worst=%.4f)\n",
           bend_violations, bend_constraint_count, worst_violation);

    printf("\nfinal per-joint positions:\n");
    for (uint32_t j = 0; j < skel.joint_count; j++) {
        printf("  %-24s (%7.3f, %7.3f, %7.3f)\n", bodies[j].name,
               bodies[j].pos[0], bodies[j].pos[1], bodies[j].pos[2]);
    }
    return 0;
}
