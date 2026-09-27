// rigid_ragdoll/main.c -- the rigid-body successor to tools/ragdoll_spike (founder real-time,
// 2026-09-27: "upgrade shankpit and nock to formal rigid body physics" + "and the rl animations
// pipeline").
//
// Runs packages/simulation/rigid_ragdoll.c (17 rigid capsules with Winter body-segment masses,
// XPBD ball/hinge joints with anatomical limits, on the real multiplayer mannequin skeleton),
// shoves it, records every tick, and writes two real .gband clips with REAL ROTATION CHANNELS
// (`<joint>.qx/.qy/.qz/.qw` for all 65 joints + `pelvis.tx/.ty/.tz`) -- the orientation data the
// point-mass spike's position-only oracle never had:
//
//   tools/rigid_ragdoll/output/ragdoll_fall.gband(.json)   the fall, as simulated
//   tools/rigid_ragdoll/output/getup_oracle.gband(.json)   the same fall reversed: the S497
//       "play the death animation backwards" imitation TARGET for a future get-up policy
//       (RAGDOLL_ORIENTATION_NORTHSTAR.md) -- a reward-compiler reference, not a playable clip
//
// skeleton_hash is the real sha256 of mannequin_npc.gskel (the spike left it zeroed).
//
// Usage (from the repo root): make rigid-ragdoll && ./build/rigid_ragdoll [push_mps] [seconds]
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../../packages/simulation/rigid_ragdoll.h"
#include "../../packages/goldenband/sha256.h"

#define TICK_HZ 64

static int file_sha256(const char *path, unsigned char out[32]) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    static unsigned char buf[1 << 20];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    sha256(buf, n, out);
    return 1;
}

static void hex(const unsigned char *h, char *o) {
    for (int i = 0; i < 32; i++) sprintf(o + 2 * i, "%02x", h[i]);
}

static int write_clip(const char *base, const GSkel *sk, const float *data, int ticks, int nch,
                      const unsigned char skel_hash[32], const char *who, const char *tags) {
    char path[512];
    unsigned char ch[32];
    sha256((const uint8_t *)data, (size_t)ticks * nch * sizeof(float), ch);
    snprintf(path, sizeof path, "%s.gband", base);
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    unsigned char h[84] = {0};
    memcpy(h, "GBND", 4);
    uint32_t v[4] = {1, TICK_HZ, (uint32_t)ticks, (uint32_t)nch};
    for (int i = 0; i < 4; i++)
        for (int b = 0; b < 4; b++) h[4 + i * 4 + b] = (unsigned char)(v[i] >> (8 * b));
    memcpy(h + 20, skel_hash, 32);
    memcpy(h + 52, ch, 32);
    fwrite(h, 1, 84, f);
    fwrite(data, sizeof(float), (size_t)ticks * nch, f); // little-endian hosts
    fclose(f);

    char sh[65], chh[65];
    hex(skel_hash, sh);
    hex(ch, chh);
    snprintf(path, sizeof path, "%s.gband.json", base);
    f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "{\n  \"gband_version\": 1,\n  \"skeleton_hash\": \"%s\",\n  \"content_hash\": \"%s\",\n", sh, chh);
    fprintf(f, "  \"tick_rate\": %d,\n  \"duration_ticks\": %d,\n  \"channels\": [", TICK_HZ, ticks);
    for (uint32_t j = 0; j < sk->joint_count; j++) {
        const char *n = sk->joints[j].name;
        fprintf(f, "%s\"%s.qx\", \"%s.qy\", \"%s.qz\", \"%s.qw\"", j ? ", " : "", n, n, n, n);
    }
    fprintf(f, ", \"pelvis.tx\", \"pelvis.ty\", \"pelvis.tz\"],\n");
    fprintf(f, "  \"authorship\": {\n    \"kind\": \"generative\",\n    \"who\": \"%s\"\n  },\n", who);
    fprintf(f, "  \"intent_tags\": [%s],\n  \"loop_points\": {\n    \"start_tick\": 0,\n    \"end_tick\": %d\n  },\n", tags, ticks);
    fprintf(f, "  \"safety\": {\n    \"max_joint_velocity\": null,\n    \"max_joint_torque\": null\n  }\n}\n");
    fclose(f);
    printf("wrote %s.gband (+.json): %d ticks x %d channels, content %.16s\n", base, ticks, nch, chh);
    return 1;
}

int main(int argc, char **argv) {
    double push = argc > 1 ? atof(argv[1]) : 2.5;
    double seconds = argc > 2 ? atof(argv[2]) : 3.0;
    const char *skel_path = "assets/goldenband/mannequin_npc.gskel";
    static GSkel skel;
    if (!gskel_init(skel_path, &skel)) { fprintf(stderr, "rigid_ragdoll: cannot load %s (run from the repo root)\n", skel_path); return 1; }
    unsigned char skel_hash[32];
    file_sha256(skel_path, skel_hash);
    static RigidRagdoll r;
    if (!rigid_ragdoll_build(&r, &skel, 70.0)) { fprintf(stderr, "rigid_ragdoll: skeleton is missing mannequin joints\n"); return 1; }
    printf("rigid ragdoll: %d segments, %.1f kg, %u grb joints, %d substeps\n", r.seg_count, r.total_mass, r.world.joint_count, r.world.substeps);

    int ticks = (int)(seconds * TICK_HZ) + 1;
    int nch = (int)skel.joint_count * 4 + 3;
    float *fall = malloc(sizeof(float) * (size_t)ticks * nch);
    float *rev = malloc(sizeof(float) * (size_t)ticks * nch);
    double v[3] = {0, 0, -push}, w[3] = {push * 0.6, 0, 0};
    double worst_lim = 0, low = 1e9;
    for (int t = 0; t < ticks; t++) {
        if (t == 1) rigid_ragdoll_push(&r, "spine_03", v, w);
        if (t > 0) rigid_ragdoll_step(&r, 1.0 / TICK_HZ);
        float *row = &fall[(size_t)t * nch];
        rigid_ragdoll_pose(&r, row, row + skel.joint_count * 4);
        double l = rigid_ragdoll_limit_violation(&r), lo = rigid_ragdoll_lowest_point(&r);
        if (l > worst_lim) worst_lim = l;
        if (lo < low) low = lo;
        if (t % 32 == 0 || t == ticks - 1)
            printf("  t=%.2fs pelvis y=%.3f m  KE=%8.3f J\n", t / (double)TICK_HZ, r.world.bodies[r.seg_body[0]].pos[1], grb_kinetic_energy(&r.world));
    }
    printf("  worst joint-limit overshoot %.4f rad, lowest capsule point %.4f m\n", worst_lim, low);
    for (int t = 0; t < ticks; t++) memcpy(&rev[(size_t)t * nch], &fall[(size_t)(ticks - 1 - t) * nch], sizeof(float) * nch);

    mkdir("tools/rigid_ragdoll/output", 0755);
    char who[256];
    snprintf(who, sizeof who, "tools/rigid_ragdoll: grb rigid-body mannequin (70 kg, Winter segments), %.2f m/s chest shove, simulated", push);
    int ok = write_clip("tools/rigid_ragdoll/output/ragdoll_fall", &skel, fall, ticks, nch, skel_hash, who, "\"fall\", \"ragdoll\", \"synthetic\"");
    snprintf(who, sizeof who, "tools/rigid_ragdoll: the simulated %.2f m/s fall, reversed -- a get-up imitation target (reward-compiler reference), not a playable clip", push);
    ok &= write_clip("tools/rigid_ragdoll/output/getup_oracle", &skel, rev, ticks, nch, skel_hash, who, "\"get-up\", \"recovery-oracle\", \"synthetic\"");
    free(fall);
    free(rev);
    return ok ? 0 : 1;
}
