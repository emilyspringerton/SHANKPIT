/* env_matrix_test.c -- the unified environment matrix (packages/render/env_matrix.c, numbers from
 * PARENA/stdlib/shankpit/lighting_rules.prn). Headless by construction: no GL context, no SDL2, no
 * window, so it runs in this sandbox and in CI alike.
 *
 * The bug this exists to prevent, in the founder's words (2026-10-30): "it always looks too dark
 * everywhere". Weather, the day/night preset and the ambient slider each dimmed the scene
 * independently and nothing reconciled them, so a stormy night at slider 0 multiplied down to
 * near-black walls. These checks pin the PROPERTIES that fix that (a floor that always engages, a
 * scattered share that only ever adds, determinism, emitters trading reach for spread) rather than
 * the specific constants -- retune the .prn and this still has to pass.
 *
 * `env_matrix_apply_surface` is the exact function retro_lighting_eval_surface_rgb calls for every
 * wall in the game, which is why it lives in env_matrix.c and not inside the GL-heavy renderer.
 *
 * make test-env-matrix
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "env_matrix.h"

/* PARENA-generated (packages/simulation/lighting_rules.c) */
int env_occlusion(int cover, int rain, int storm, int fog_wash, int interior);
int env_light_bleed(int occlusion, int interior);
int env_exposure(int occlusion);
int env_ambient_floor(int occlusion, int ambient_level, int interior);
int env_emissive_scale(int ambient_permille);
int env_emitter_range_scale(int visibility);
int env_emitter_spread_scale(int range_scale);
int env_flash_boost(int flash);

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static EnvInputs base_inputs(void) {
    EnvInputs in;
    memset(&in, 0, sizeof in);
    in.fog_r = 0.42f; in.fog_g = 0.46f; in.fog_b = 0.52f;   /* a neutral daylight haze */
    return in;
}

static void stormify(EnvInputs *in) {
    in->cover = 1.0f; in->rain = 1.0f; in->storm = 1.0f; in->fog_wash = 1.0f;
    in->visibility = 540.0f;                                 /* the storm profile's own reach */
}

static float mean3(float r, float g, float b) { return (r + g + b) / 3.0f; }

int main(void) {
    /* 1. the PARENA rules are pure and total: out-of-range in, sane out */
    CHECK(env_occlusion(5000, 5000, 5000, 5000, 0) <= 780);
    CHECK(env_occlusion(0, 0, 0, 0, 0) == 0);
    CHECK(env_occlusion(1000, 1000, 1000, 1000, 1) < env_occlusion(1000, 1000, 1000, 1000, 0));
    CHECK(env_ambient_floor(780, 0, 0) == 300);              /* the hard floor, even fully occluded */
    CHECK(env_ambient_floor(0, 10, 0) == 600);               /* clear + slider max */
    CHECK(env_ambient_floor(0, 99, 0) == env_ambient_floor(0, 10, 0));  /* slider clamped */
    CHECK(env_emitter_range_scale(0) == 1000);               /* no reading must not shorten a light */
    CHECK(env_emitter_range_scale(-5) == 1000);
    CHECK(env_emitter_range_scale(3000) == 1000);
    CHECK(env_emitter_range_scale(1) >= 450);                /* never fully extinguished */
    CHECK(env_emitter_spread_scale(1000) == 1000);
    CHECK(env_emitter_spread_scale(450) > 1000);
    CHECK(env_emissive_scale(1000) >= 750);                  /* bright scene tucks emissives down */
    CHECK(env_emissive_scale(300) > env_emissive_scale(700)); /* dark scene pushes them up */

    /* 2. clear weather is a genuine no-op on the emitters and on scattering */
    EnvInputs clear_in = base_inputs();
    EnvMatrix clear_m;
    env_matrix_eval(&clear_in, &clear_m);
    CHECK(clear_m.occlusion == 0.0f);
    CHECK(clear_m.light_bleed == 0.0f);
    CHECK(clear_m.exposure == 1.0f);
    CHECK(clear_m.emitter_range_scale == 1.0f && clear_m.emitter_spread_scale == 1.0f);
    CHECK(clear_m.ambient_floor >= 0.30f && clear_m.ambient_floor <= 0.90f);

    /* 3. a storm resolves into one coherent state: occluded, scattering, floored, emitters shortened */
    EnvInputs storm_in = base_inputs();
    stormify(&storm_in);
    EnvMatrix storm_m;
    env_matrix_eval(&storm_in, &storm_m);
    CHECK(storm_m.occlusion >= 0.70f);
    CHECK(storm_m.light_bleed > 0.10f);                      /* it scatters, it does not only subtract */
    CHECK(storm_m.ambient_floor >= 0.30f);
    CHECK(storm_m.exposure > 1.2f);
    CHECK(storm_m.emitter_range_scale <= 0.55f);
    CHECK(storm_m.emitter_spread_scale >= 1.40f);
    CHECK(storm_m.bleed_r > 0.0f && storm_m.bleed_b > 0.0f);

    /* 4. an interior is shielded from the weather but still lit */
    EnvInputs in_in = storm_in;
    in_in.interior = 1;
    EnvMatrix in_m;
    env_matrix_eval(&in_in, &in_m);
    CHECK(in_m.occlusion < storm_m.occlusion);
    CHECK(in_m.light_bleed < storm_m.light_bleed);
    CHECK(in_m.ambient_floor > storm_m.ambient_floor);

    /* 5. THE REGRESSION. A shaded wall at night in a storm, ambient slider at 0: this is the state the
          founder called "too dark everywhere". The before numbers are that scene's own term --
          night-moonlit ambient (0.30/0.36/0.47 x 0.70 intensity) x a 0.6 ambient factor, plus the
          moonlight sky fill -- which is what retro_lighting produced with nothing reconciling it. */
    float br = 0.166f, bg = 0.197f, bb = 0.264f;
    float before = mean3(br, bg, bb);
    float ar = br, ag = bg, ab = bb;
    env_matrix_apply_surface(storm_m.ambient_floor, (const float[]){ storm_m.bleed_r, storm_m.bleed_g, storm_m.bleed_b }, &ar, &ag, &ab);
    float after = mean3(ar, ag, ab);
    CHECK(after > before * 1.25f);                           /* measurably lifted, not a nudge */
    CHECK(ar >= 0.20f && ag >= 0.20f && ab >= 0.20f);        /* readable, not just "less black" */
    CHECK(ab > ag && ag > ar);                               /* the fill stays cool, hue preserved */

    /* 6. the floor alone really engages: with no scatter at all, a dark term is lifted to the floor */
    float fr = 0.10f, fg = 0.12f, fb = 0.16f;
    env_matrix_apply_surface(clear_m.ambient_floor, NULL, &fr, &fg, &fb);
    float mx = fr > fg ? (fr > fb ? fr : fb) : (fg > fb ? fg : fb);
    CHECK(fabsf(mx - clear_m.ambient_floor) < 1e-4f);        /* exactly the floor, no more */
    CHECK(fb > fg && fg > fr);                               /* hue order survives the lift */

    /* 7. the no-op contract: nothing published == exactly the original numbers, bit for bit. This is
          what every caller that never evaluates a matrix (and every existing test) relies on. */
    float nr = 0.123f, ng = 0.456f, nb = 0.789f;
    float nr2 = nr, ng2 = ng, nb2 = nb;
    env_matrix_apply_surface(0.0f, (const float[]){ 0.0f, 0.0f, 0.0f }, &nr2, &ng2, &nb2);
    CHECK(nr2 == nr && ng2 == ng && nb2 == nb);

    /* 8. the slider still means something under the worst weather (level 0 < level 10) */
    EnvInputs slider_hi = storm_in;
    slider_hi.ambient_level = 10;
    EnvMatrix hi_m;
    env_matrix_eval(&slider_hi, &hi_m);
    CHECK(hi_m.ambient_floor > storm_m.ambient_floor);
    float hr = br, hg = bg, hb = bb;
    env_matrix_apply_surface(hi_m.ambient_floor, (const float[]){ hi_m.bleed_r, hi_m.bleed_g, hi_m.bleed_b }, &hr, &hg, &hb);
    CHECK(mean3(hr, hg, hb) > after);

    /* 9. deterministic: the same broadcast flags give every client the same matrix, bit for bit
          (the whole MMO-concurrence argument -- it is a pure function, not sampled state) */
    EnvMatrix a, b;
    env_matrix_eval(&storm_in, &a);
    env_matrix_eval(&storm_in, &b);
    CHECK(memcmp(&a, &b, sizeof a) == 0);

    /* 10. bleeds take the sky's own colour: a warm haze scatters warm */
    EnvInputs warm = storm_in;
    warm.fog_r = 0.80f; warm.fog_g = 0.20f; warm.fog_b = 0.10f;
    EnvMatrix warm_m;
    env_matrix_eval(&warm, &warm_m);
    CHECK(warm_m.bleed_r > warm_m.bleed_b);

    /* 11. direct light is what the weather takes away; ambient is what the floor protects */
    RetroLightingState dim;
    memset(&dim, 0, sizeof dim);
    dim.sun_intensity = 0.75f; dim.ambient_intensity = 0.72f;
    dim.sun_r = 1.0f; dim.sun_g = 0.95f; dim.sun_b = 0.86f;
    dim.ambient_r = 0.56f; dim.ambient_g = 0.58f; dim.ambient_b = 0.58f;
    env_matrix_apply(&storm_m, &dim);
    CHECK(dim.sun_intensity / 0.75f < dim.ambient_intensity / 0.72f);

    /* 11b. the WIRING, without a GL context: publishing a matrix must make the surface transform
           pick it up. This is the exact pair of calls the render loop makes each frame
           (env_matrix_publish, then retro_lighting_eval_surface_rgb -> apply_published_surface). */
    env_matrix_publish(&storm_m);
    float wr = 0.05f, wg = 0.05f, wb = 0.05f;
    env_matrix_apply_published_surface(&wr, &wg, &wb);
    CHECK(mean3(wr, wg, wb) > 0.20f);
    float wmx = wr > wg ? (wr > wb ? wr : wb) : (wg > wb ? wg : wb);
    CHECK(wmx >= storm_m.ambient_floor - 1e-3f);
    /* and unpublishing returns the original look, bit for bit */
    retro_lighting_set_ambient_floor(0.0f);
    retro_lighting_set_sky_bleed(0.0f, 0.0f, 0.0f);
    float zr = 0.05f, zg = 0.05f, zb = 0.05f;
    env_matrix_apply_published_surface(&zr, &zg, &zb);
    CHECK(zr == 0.05f && zg == 0.05f && zb == 0.05f);
    env_matrix_publish(&storm_m);

    /* 11c. lightning is a light source, not a screen tint: a strike adds its own scattered light
           (whitened, because a bolt is broadband) and lifts exposure, then goes away again when
           the flash decays. flash = 0 must be exactly the no-strike result. */
    CHECK(env_flash_boost(0) == 0);
    CHECK(env_flash_boost(1000) > 0 && env_flash_boost(1000) <= 300);
    CHECK(env_flash_boost(5000) == env_flash_boost(1000));   /* clamped */
    EnvInputs quiet = storm_in;
    EnvMatrix quiet_m;
    env_matrix_eval(&quiet, &quiet_m);
    EnvInputs bolt = storm_in;
    bolt.flash = 1.0f;
    EnvMatrix bolt_m;
    env_matrix_eval(&bolt, &bolt_m);
    CHECK(bolt_m.flash_boost > 0.0f);
    CHECK(bolt_m.light_bleed > quiet_m.light_bleed);
    CHECK(bolt_m.exposure > quiet_m.exposure);
    CHECK(bolt_m.bleed_g > quiet_m.bleed_g);                   /* greener-but-dark haze goes whiter */
    {
        /* the strike really lands on a dark surface through the production transform */
        float sr = 0.06f, sg = 0.07f, sb = 0.08f;
        env_matrix_apply_surface(bolt_m.ambient_floor, (const float[]){ bolt_m.bleed_r, bolt_m.bleed_g, bolt_m.bleed_b }, &sr, &sg, &sb);
        CHECK(mean3(sr, sg, sb) >= mean3(bolt_m.bleed_r, bolt_m.bleed_g, bolt_m.bleed_b) * 0.99f);
    }
    CHECK(bolt_m.flash_boost <= 0.30f);
    CHECK(quiet_m.flash_boost == 0.0f);

    /* 12. the emitters trade reach for spread, and the two never disagree */
    for (int v = 0; v <= 3200; v += 100) {
        EnvInputs e = base_inputs();
        e.visibility = (float)v;
        EnvMatrix em;
        env_matrix_eval(&e, &em);
        CHECK(em.emitter_range_scale >= 0.05f && em.emitter_range_scale <= 1.0f);
        CHECK(em.emitter_spread_scale >= 1.0f && em.emitter_spread_scale <= 3.0f);
        if (em.emitter_range_scale < 0.999f) CHECK(em.emitter_spread_scale > 1.0f);
    }

    if (g_fail) { printf("env_matrix_test: %d FAILED\n", g_fail); return 1; }
    printf("env_matrix_test OK (stormy night at slider 0: %.3f -> %.3f, floor %.3f, occlusion %.2f, bleed %.2f)\n",
           before, after, storm_m.ambient_floor, storm_m.occlusion, storm_m.light_bleed);
    return 0;
}
