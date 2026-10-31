/* slide_rules_test.c -- a crouch-slide on a hillside, headless.
 *
 * The property that matters: a slide on flat ground is EXACTLY what it was before this change (the
 * same 1% scrub, no acceleration at all), and a slide pointed downhill ends up measurably faster than
 * the same slide on the flat. Everything else here guards the edges -- uphill earns nothing, the cap
 * holds, and standing/airborne/vehicle states are untouched.
 *
 * The terrain is a real ramp: height = 4 per grid cell at cell_size 4, i.e. a 1:1 grade (45 degrees,
 * normal.y = 0.707), sampled through the engine's own terrain_sample_normal rather than a hand-passed
 * normal, so this tests the integration and not just the arithmetic.
 */
#include <stdio.h>
#include <string.h>

#include "physics.h"
#include "slide_rules_host.h"

PlayerState *get_best_bot(void) { return NULL; }
void evolve_bot(PlayerState *a, PlayerState *b) { (void)a; (void)b; }

static int g_fail = 0, g_checks = 0;
static void ok(int cond, const char *what) {
    g_checks++;
    if (!cond) { g_fail++; printf("FAIL  %s\n", what); }
    else printf("ok    %s\n", what);
}

static PlayerState crouched(float vx, float vz) {
    PlayerState p;
    memset(&p, 0, sizeof p);
    p.active = 1;
    p.state = STATE_ALIVE;
    p.on_ground = 1;
    p.crouching = 1;
    p.ground_friction = FRICTION;
    p.vx = vx; p.vz = vz;
    p.x = 0.0f; p.y = 40.0f; p.z = 0.0f;
    return p;
}

/* one tick of the real move order for a sliding player */
static void tick(PlayerState *p) {
    apply_friction(p);
    phys_slide_slope_tick(p);
}

static float speed_of(const PlayerState *p) { return sqrtf(p->vx * p->vx + p->vz * p->vz); }

/* a 1:1 ramp rising along +x, so +x is UPHILL and -x is downhill */
static void build_ramp(void) {
    terrain_init(&g_scene_terrain, 64, 64, 4.0f, -128.0f, -128.0f);
    for (int gz = 0; gz < 64; gz++)
        for (int gx = 0; gx < 64; gx++)
            terrain_set_height(&g_scene_terrain, gx, gz, (float)gx * 4.0f);
}

static void build_flat(void) {
    terrain_clear(&g_scene_terrain, 0.0f);
}

int main(void) {
    phys_set_scene(SCENE_VOXWORLD);
    static Box geo[2];
    geo[1] = (Box){0, -1, 0, 4, 2, 4};
    map_geo = geo; map_count = 1;      /* no boxes: this is purely the heightfield case */

    /* ---- the ramp is really a ramp ------------------------------------------------------------ */
    build_ramp();
    {
        float nx, ny, nz, dx, dz;
        PlayerState p = crouched(0.0f, 0.0f);
        int ny_pm;
        terrain_sample_normal(&g_scene_terrain, p.x, p.z, &nx, &ny, &nz);
        ny_pm = phys_slide_surface_permille(&p, &dx, &dz);
        printf("      ramp normal.y %.3f (%d permille), downhill (%.2f, %.2f)\n", ny, ny_pm, dx, dz);
        ok(ny_pm > 600 && ny_pm < 800, "the sampled slope is ~45 degrees, not flat");
        ok(dx < -0.9f, "downhill points -x (the ramp rises toward +x)");
    }

    /* ---- flat: exactly the old slide ---------------------------------------------------------- */
    build_flat();
    {
        PlayerState p = crouched(-0.9f, 0.0f);
        float start = speed_of(&p);
        int moved = 0;
        /* one tick at 0.9: the engine's own slide branch, untouched -- drop = speed * SLIDE_FRICTION */
        tick(&p);
        printf("      flat, one tick at %.2f: %.5f (old slide branch gives %.5f)\n",
               start, speed_of(&p), start * (1.0f - SLIDE_FRICTION));
        ok(fabsf(speed_of(&p) - start * (1.0f - SLIDE_FRICTION)) < 1e-5f,
           "one tick of a flat slide is exactly the old formula");
        for (int t = 1; t < 60; t++) { tick(&p); moved += phys_slide_slope_tick(&p); }
        printf("      flat after 60 ticks: %.4f\n", speed_of(&p));
        ok(moved == 0, "and the slope step never fires on flat ground");
        ok(speed_of(&p) < 0.05f, "a flat slide comes to rest (this is not a free speed hack)");
    }

    /* ---- downhill: faster than flat ---------------------------------------------------------- */
    build_ramp();
    {
        PlayerState down = crouched(-0.9f, 0.0f);
        PlayerState flat = crouched(-0.9f, 0.0f);
        build_ramp();
        for (int t = 0; t < 60; t++) tick(&down);
        build_flat();
        for (int t = 0; t < 60; t++) tick(&flat);
        printf("      after 60 ticks: downhill %.4f vs flat %.4f\n", speed_of(&down), speed_of(&flat));
        ok(speed_of(&down) > speed_of(&flat) + 0.15f, "a downhill slide is much faster than a flat one");
        ok(speed_of(&down) > 1.0f, "it actually outruns a sprint (MAX_SPEED is 0.95)");
        ok(speed_of(&down) <= 1.2f + 0.001f, "and the speed cap holds");
        ok(down.vx < 0.0f, "it accelerated along the downhill direction (-x), not a random axis");
    }

    /* ---- uphill: no free speed, and gravity eventually turns you around ---------------------- */
    /* An uphill slide must never GAIN speed going up. It is allowed -- and expected -- to stall and
     * then roll back down: that is what a hill does to a board, and the first version of this test
     * asserted "uphill equals flat", which the run disproved at tick ~20 when the velocity crossed
     * zero and the slope started pulling it back down. Asserting the physics rather than my guess. */
    {
        PlayerState up = crouched(0.9f, 0.0f);
        float start = speed_of(&up), max_uphill = 0.0f;
        int stalled_at = -1;
        build_ramp();
        for (int t = 0; t < 60; t++) {
            tick(&up);
            if (up.vx > 0.0f) {
                if (speed_of(&up) > max_uphill) max_uphill = speed_of(&up);
            } else if (stalled_at < 0) {
                stalled_at = t;
            }
        }
        printf("      uphill: peak %.4f (start %.4f), stalled at tick %d, now %.4f at vx %.4f\n",
               max_uphill, start, stalled_at, speed_of(&up), up.vx);
        ok(max_uphill <= start + 1e-4f, "an uphill slide never gains speed on the way up");
        ok(stalled_at > 0, "it stalls, as a slide up a 45 degree hill should");
        ok(up.vx < 0.0f, "and the hill sends it back down (in the downhill direction, not randomly)");
        ok(speed_of(&up) <= 1.2f + 0.001f, "the rollback respects the same speed cap");
    }

    /* ---- from a standstill, and controls ----------------------------------------------------- */
    build_ramp();
    {
        PlayerState p = crouched(0.0f, 0.0f);
        for (int t = 0; t < 30; t++) tick(&p);
        printf("      standing crouched on a slope for 30 ticks: %.4f\n", speed_of(&p));
        ok(speed_of(&p) > 0.05f, "crouching on a hill slides you downhill from rest");

        p = crouched(-0.9f, 0.0f);
        p.on_ground = 0;
        ok(phys_slide_slope_tick(&p) == 0, "in the air the slope does nothing");
        p = crouched(-0.9f, 0.0f);
        p.crouching = 0;
        ok(phys_slide_slope_tick(&p) == 0, "standing up cancels it");
        p = crouched(-0.9f, 0.0f);
        p.in_vehicle = 1;
        ok(phys_slide_slope_tick(&p) == 0, "a vehicle is not a slide");

        /* flat ground with crouch held: the step must never fire */
        build_flat();
        p = crouched(-0.9f, 0.0f);
        ok(phys_slide_slope_tick(&p) == 0, "flat ground never fires the slope step");
    }

    /* ---- cap, both directions --------------------------------------------------------------- */
    build_ramp();
    {
        PlayerState p = crouched(-1.19f, 0.0f);
        for (int t = 0; t < 120; t++) tick(&p);
        printf("      cap test: %.4f (cap %.2f)\n", speed_of(&p), (double)(slide_slope_speed_cap_milli() / 1000.0));
        ok(speed_of(&p) <= 1.2f + 0.001f, "long downhill never exceeds the cap");
        ok(speed_of(&p) > 1.19f, "...but it does stay pinned at it");
    }

    /* ---- the compiled defaults are PARENA's numbers ------------------------------------------ */
    {
        SlideRules defaults = g_slide_rules;
        int mismatches = 0, samples = 0;
        slide_rules_install();
        for (int ny = 0; ny <= 1000; ny += 25) {
            if (defaults.steep_permille(ny) != slide_slope_steep_permille(ny)) mismatches++;
            if (defaults.scrub_permille(ny) != slide_slope_scrub_permille(ny)) mismatches++;
            for (int dot = 0; dot <= 1000; dot += 100) {
                for (int sp = 0; sp <= 1400; sp += 200) {
                    samples++;
                    if (defaults.accel_permille(ny, dot, sp) !=
                        slide_slope_accel_permille(ny, dot, sp)) mismatches++;
                }
            }
        }
        printf("      PARENA vs compiled-in defaults: %d samples, %d mismatches\n", samples, mismatches);
        ok(mismatches == 0, "the module and the header defaults are the same curve");
        ok(defaults.speed_cap_milli() == slide_slope_speed_cap_milli(), "and the same cap");
        ok(slide_slope_flat_threshold_permille() == 990, "the flat threshold is the one physics.h uses");
    }

    /* ---- and the installed curve still behaves ----------------------------------------------- */
    build_ramp();
    {
        PlayerState p = crouched(-0.9f, 0.0f);
        for (int t = 0; t < 60; t++) tick(&p);
        printf("      PARENA-installed downhill: %.4f\n", speed_of(&p));
        ok(speed_of(&p) > 1.0f, "the generated curve accelerates a downhill slide just the same");
    }

    printf("\n%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
