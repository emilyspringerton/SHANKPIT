/* rigid_ragdoll_test.c -- real tests for rigid_ragdoll.c on the real multiplayer mannequin
 * skeleton. Plain assert() harness, same convention as humanness_test.c/day_night_clock_test.c.
 *
 * Build and run (from the repo root -- it loads assets/goldenband/mannequin_npc.gskel):
 *   make test-physics
 * or directly:
 *   gcc -Wall -Wextra -O2 -Ipackages/goldenband -Ipackages/simulation -o /tmp/rigid_ragdoll_test \
 *       packages/simulation/rigid_ragdoll_test.c packages/simulation/rigid_ragdoll.c \
 *       packages/goldenband/grb.c packages/goldenband/gskel.c packages/goldenband/gpose.c -lm
 */
#include "rigid_ragdoll.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void shove_and_settle(RigidRagdoll *r, int ticks, double *worst_limit, double *lowest, double *worst_drift) {
    double v[3] = {0, 0, -2.5}, w[3] = {1.5, 0, 0};
    rigid_ragdoll_push(r, "spine_03", v, w);
    *worst_limit = 0; *lowest = 1e9; *worst_drift = 0;
    for (int t = 0; t < ticks; t++) {
        rigid_ragdoll_step(r, 1.0 / 64);
        double l = rigid_ragdoll_limit_violation(r), lo = rigid_ragdoll_lowest_point(r), d = grb_max_joint_error(&r->world);
        if (l > *worst_limit) *worst_limit = l;
        if (lo < *lowest) *lowest = lo;
        if (d > *worst_drift) *worst_drift = d;
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    static GSkel skel;
    assert(gskel_init("assets/goldenband/mannequin_npc.gskel", &skel));
    static RigidRagdoll r;
    assert(rigid_ragdoll_build(&r, &skel, 70.0));
    /* 17 simulated segments; Winter's segment fractions sum to the whole body. */
    assert(r.seg_count == 17);
    assert(fabs(r.total_mass - 70.0) < 1e-9);
    printf("PASS: 17 rigid segments, %.3f kg (Winter segment fractions sum to 1)\n", r.total_mass);

    /* At rest the ragdoll reproduces the skeleton's own rest pose exactly -- every joint's local
     * rotation (q or -q) and the pelvis translation. */
    static float rot[GSKEL_MAX_JOINTS * 4];
    float pel[3];
    rigid_ragdoll_pose(&r, rot, pel);
    double worst = 0;
    for (uint32_t j = 0; j < skel.joint_count; j++) {
        double d = 0, dn = 0;
        for (int k = 0; k < 4; k++) {
            d += pow(rot[j * 4 + k] - skel.joints[j].rest_rotation[k], 2);
            dn += pow(rot[j * 4 + k] + skel.joints[j].rest_rotation[k], 2);
        }
        if (dn < d) d = dn;
        if (d > worst) worst = d;
    }
    int pelvis = gskel_find_joint(&skel, "pelvis");
    for (int k = 0; k < 3; k++) assert(fabs(pel[k] - skel.joints[pelvis].rest_translation[k]) < 1e-5);
    assert(sqrt(worst) < 1e-5);
    printf("PASS: rest pose round-trips through the rigid bodies (max quat err %.1e)\n", sqrt(worst));

    /* Shove the chest backward, 4 s. */
    double lim, low, drift;
    shove_and_settle(&r, 64 * 4, &lim, &low, &drift);
    double pelvis_y = r.world.bodies[r.seg_body[0]].pos[1], ke = grb_kinetic_energy(&r.world);
    printf("     after 4 s: pelvis y %.3f m, KE %.4f J, worst limit violation %.4f rad, lowest point %.4f m, drift %.1e m\n",
           pelvis_y, ke, lim, low, drift);
    assert(pelvis_y < 0.3);   /* it fell over */
    assert(ke < 0.05);        /* and came to rest */
    /* Transient peaks during the impact (XPBD with a finite substep count is not perfectly
     * rigid under a 70 kg body slamming down), then the settled state. */
    assert(lim < 0.06);       /* never more than ~3.4 deg past an anatomical limit, even mid-impact */
    assert(low > -0.005);     /* never sank more than 5 mm into the ground */
    assert(drift < 3e-3);     /* joints stayed attached within 3 mm */
    double settled_lim = rigid_ragdoll_limit_violation(&r);
    printf("     settled: limit violation %.5f rad, joint drift %.1e m\n", settled_lim, grb_max_joint_error(&r.world));
    assert(settled_lim < 0.005);
    assert(grb_max_joint_error(&r.world) < 5e-4);
    printf("PASS: falls, settles, stays inside every swing/twist/hinge limit, no ground penetration\n");

    /* Not poker-straight: the knees and elbows are one-way hinges, so the settled body is flexed
     * (this is exactly the failure tools/ragdoll_spike's point masses could never avoid). */
    int flexed = 0;
    for (int s = 0; s < r.seg_count; s++) {
        int gj = r.seg_grb_joint[s];
        if (gj >= 0 && r.world.joints[gj].type == GRB_JOINT_HINGE) {
            assert(r.world.joints[gj].angle > -0.05 - 0.01); /* never hyperextended */
            if (r.world.joints[gj].angle > 0.1) flexed++;
        }
    }
    assert(flexed >= 2);
    printf("PASS: %d of 4 knee/elbow hinges settle flexed, none hyperextended\n", flexed);

    /* Deterministic: the same shove reproduces bit-identical bodies. */
    static RigidRagdoll r2;
    rigid_ragdoll_build(&r2, &skel, 70.0);
    shove_and_settle(&r2, 64 * 4, &lim, &low, &drift);
    assert(memcmp(r.world.bodies, r2.world.bodies, sizeof(GrbBody) * r.world.body_count) == 0);
    printf("PASS: bit-identical on rerun\n");
    printf("OK\n");
    return 0;
}
