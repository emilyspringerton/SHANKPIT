/* Differential test: physics_host (PARENA-generated) vs. the real SHANKPIT physics.h.
 * Original symbols are renamed via -D on the command line (see Makefile `diff` target). */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "physics.h"
#include "physics_host.h"

static unsigned g_seed = 12345u;
static float rnd(float lo, float hi) {
    g_seed = g_seed * 1664525u + 1013904223u;
    return lo + (hi - lo) * ((float)(g_seed >> 8) / 16777216.0f);
}
static int rndi(int n) {
    g_seed = g_seed * 1664525u + 1013904223u;
    return (int)((g_seed >> 8) % (unsigned)n);
}

static int g_fail, g_checks;
static void expect_near(const char *what, float a, float b, float tol, int iter) {
    g_checks++;
    if (!(fabsf(a - b) <= tol * (1.0f + fabsf(b)))) {
        if (g_fail < 12) printf("MISMATCH %s iter %d: port=%.7g orig=%.7g\n", what, iter, a, b);
        g_fail++;
    }
}

/* Stubs for symbols physics.h declares but defines elsewhere in the game. */
void evolve_bot(PlayerState *loser, PlayerState *winner) { (void)loser; (void)winner; }
PlayerState *get_best_bot(void) { return NULL; }

int main(void) {
    int it;
    /* --- friction / accelerate -------------------------------------------------------- */
    for (it = 0; it < 20000; it++) {
        PlayerState a;
        PhysHostPlayer h;
        memset(&a, 0, sizeof a);
        a.vx = rnd(-3, 3); a.vz = rnd(-3, 3);
        if (rndi(4) == 0) { a.vx *= 0.001f; a.vz *= 0.001f; }
        a.on_ground = rndi(2); a.crouching = rndi(2); a.in_vehicle = rndi(5) == 0;
        a.dash_timer = rndi(4) == 0 ? 3 : 0;
        a.ground_friction = rnd(0.05f, 0.9f);
        memset(&h, 0, sizeof h);
        h.vx = a.vx; h.vz = a.vz; h.on_ground = a.on_ground; h.crouching = a.crouching;
        h.in_vehicle = a.in_vehicle; h.dash_timer = a.dash_timer; h.ground_friction = a.ground_friction;
        orig_apply_friction(&a);
        phys_host_apply_friction(&h);
        expect_near("friction.vx", h.vx, a.vx, 2e-5f, it);
        expect_near("friction.vz", h.vz, a.vz, 2e-5f, it);

        {
            float wx = rnd(-1, 1), wz = rnd(-1, 1);
            float ws = rnd(0, 1.5f), ac = rnd(0, 2);
            orig_accelerate(&a, wx, wz, ws, ac);
            phys_host_accelerate(&h, wx, wz, ws, ac);
            expect_near("accel.vx", h.vx, a.vx, 2e-5f, it);
            expect_near("accel.vz", h.vz, a.vz, 2e-5f, it);
        }
    }
    /* --- angle helpers ------------------------------------------------------------------ */
    for (it = 0; it < 5000; it++) {
        float y = rnd(-1000, 1000), b = rnd(-720, 720);
        expect_near("norm_yaw", phys_host_norm_yaw(y), norm_yaw_deg(y), 1e-4f, it);
        expect_near("clamp_pitch", phys_host_clamp_pitch(y), clamp_pitch_deg(y), 1e-6f, it);
        expect_near("angle_diff", phys_host_angle_diff(y, b), angle_diff(y, b), 1e-4f, it);
        {
            float o[3], x, yy, z;
            phys_host_katana_forward(y, rnd(-90, 90), o);
            katana_forward_dir(y, 0.0f, &x, &yy, &z); /* shape check only below */
            (void)x; (void)yy; (void)z;
            expect_near("katana.len", sqrtf(o[0]*o[0] + o[1]*o[1] + o[2]*o[2]), 1.0f, 1e-5f, it);
        }
    }
    for (it = 0; it < 5000; it++) {
        float p = rnd(-90, 90), y = rnd(0, 360), o[3], x, yy, z;
        phys_host_katana_forward(y, p, o);
        katana_forward_dir(y, p, &x, &yy, &z);
        expect_near("katana.x", o[0], x, 1e-4f, it);
        expect_near("katana.y", o[1], yy, 1e-4f, it);
        expect_near("katana.z", o[2], z, 1e-4f, it);
    }
    /* --- collision / traces against a random box world ----------------------------------- */
    {
        static Box world[17];
        static PhysHostBox hb[17];
        int n = 17, i;
        memset(world, 0, sizeof world);
        for (it = 0; it < 300; it++) {
            for (i = 1; i < n; i++) {
                world[i].x = rnd(-60, 60); world[i].y = rnd(0, 20); world[i].z = rnd(-60, 60);
                world[i].w = rnd(2, 20);   world[i].h = rnd(2, 20); world[i].d = rnd(2, 20);
                hb[i].x = world[i].x; hb[i].y = world[i].y; hb[i].z = world[i].z;
                hb[i].w = world[i].w; hb[i].h = world[i].h; hb[i].d = world[i].d;
            }
            map_geo = world; map_count = n;
            g_scene_terrain.active = 0;
            {
                int k;
                for (k = 0; k < 40; k++) {
                    /* trace_map */
                    float seg[6] = {rnd(-80, 80), rnd(-5, 30), rnd(-80, 80),
                                    rnd(-80, 80), rnd(-5, 30), rnd(-80, 80)};
                    float ox, oy, oz, nx, ny, nz;
                    int ref = orig_trace_map(seg[0], seg[1], seg[2], seg[3], seg[4], seg[5],
                                             &ox, &oy, &oz, &nx, &ny, &nz);
                    float hit[3], nrm[3], t; int bi;
                    int got = phys_host_trace(hb, n, seg, hit, nrm, &t, &bi);
                    g_checks++;
                    if (got != ref) { if (g_fail < 12) printf("trace hit mismatch it=%d k=%d port=%d orig=%d\n", it, k, got, ref); g_fail++; }
                    else if (ref) {
                        expect_near("trace.x", hit[0], ox, 1e-3f, it);
                        expect_near("trace.y", hit[1], oy, 1e-3f, it);
                        expect_near("trace.z", hit[2], oz, 1e-3f, it);
                        expect_near("trace.nx", nrm[0], nx, 1e-6f, it);
                        expect_near("trace.ny", nrm[1], ny, 1e-6f, it);
                        expect_near("trace.nz", nrm[2], nz, 1e-6f, it);
                    }
                    /* resolve_collision */
                    {
                        PlayerState a;
                        PhysHostPlayer hp;
                        PhysHostEvent ev[16];
                        memset(&a, 0, sizeof a);
                        a.x = rnd(-60, 60); a.y = rnd(-1, 25); a.z = rnd(-60, 60);
                        a.vx = rnd(-1, 1); a.vy = rnd(-1.5f, 1.5f); a.vz = rnd(-1, 1);
                        a.crouching = 0; a.in_vehicle = 0;
                        memset(&hp, 0, sizeof hp);
                        hp.x = a.x; hp.y = a.y; hp.z = a.z; hp.vx = a.vx; hp.vy = a.vy; hp.vz = a.vz;
                        hp.pw = PLAYER_WIDTH; hp.ph = PLAYER_HEIGHT;
                        orig_resolve_collision(&a);
                        phys_host_resolve(hb, n, &hp, ev, 16);
                        expect_near("resolve.x", hp.x, a.x, 1e-4f, it);
                        expect_near("resolve.y", hp.y, a.y, 1e-4f, it);
                        expect_near("resolve.z", hp.z, a.z, 1e-4f, it);
                        expect_near("resolve.vx", hp.vx, a.vx, 1e-5f, it);
                        expect_near("resolve.vy", hp.vy, a.vy, 1e-5f, it);
                        expect_near("resolve.vz", hp.vz, a.vz, 1e-5f, it);
                        g_checks++;
                        if (hp.on_ground != a.on_ground) { if (g_fail < 12) printf("on_ground mismatch it=%d\n", it); g_fail++; }
                        expect_near("resolve.fric", hp.ground_friction, a.ground_friction, 1e-6f, it);
                    }
                    /* ground height */
                    {
                        float x = rnd(-60, 60), z = rnd(-60, 60), hh;
                        int src_ref = 0;
                        float ref_h = phys_sample_ground_height(x, z, &src_ref);
                        int src = phys_host_ground_height(hb, n, x, z, 0, 0.0f, &hh);
                        expect_near("ground.h", hh, ref_h, 1e-5f, it);
                        g_checks++;
                        if (src != src_ref) { g_fail++; }
                    }
                    /* check_hit_location */
                    {
                        PlayerState t2;
                        float o[3] = {rnd(-30, 30), rnd(0, 6), rnd(-30, 30)}, d[3], len, tg[3];
                        memset(&t2, 0, sizeof t2);
                        t2.active = 1; t2.in_vehicle = rndi(4) == 0;
                        t2.x = rnd(-20, 20); t2.y = rnd(0, 3); t2.z = rnd(-20, 20);
                        d[0] = rnd(-1, 1); d[1] = rnd(-0.3f, 0.3f); d[2] = rnd(-1, 1);
                        len = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
                        d[0] /= len; d[1] /= len; d[2] /= len;
                        tg[0] = t2.x; tg[1] = t2.y; tg[2] = t2.z;
                        g_checks++;
                        if (phys_host_hit_location(o, d, tg, 1, t2.in_vehicle) !=
                            orig_check_hit_location(o[0], o[1], o[2], d[0], d[1], d[2], &t2)) {
                            if (g_fail < 12) printf("hit_location mismatch it=%d\n", it);
                            g_fail++;
                        }
                    }
                }
            }
        }
    }
    printf("%d checks, %d mismatches\n", g_checks, g_fail);
    return g_fail != 0;
}
