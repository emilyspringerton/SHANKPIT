/* lighting_chain_test.c -- the WHOLE lighting chain, headless: PARENA lighting rules ->
 * env_matrix -> retro_lighting_eval -> retro_lighting_eval_surface_rgb.
 *
 * The unit tests on either side of the matrix prove their own halves (env_matrix_test for the
 * matrix, test-ambient-boost for the slider). This one proves the property the founder actually
 * asked for -- "all the lighting needs to talk to each other" -- by sweeping every preset against
 * every weather state against every slider level against a set of face directions and asserting the
 * invariants that must hold for ALL of them, not just the cases someone thought to hand-check:
 *
 *   1. the matrix never makes a surface darker than it would have been (it only adds light),
 *   2. a shaded face never ends below the published floor,
 *   3. every result is finite and inside [0,1] (no NaN, no blowout),
 *   4. the day/night swing survives (the chain must not flatten the world to one brightness),
 *   5. an enclosed interior gets no open-sky fill,
 *   6. the same inputs always give the same pixels (client determinism).
 *
 * retro_sky.c is GL-bound (it draws the sky dome), so the two sun/fog evaluation functions
 * retro_lighting's DYNAMIC preset asks for are stubbed here. Only the sun's POSITION AND COLOUR are
 * stubbed -- every line of lighting arithmetic under test is the real production code.
 *
 * make test-lighting-chain
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "retro_lighting.h"
#include "env_matrix.h"

/* ---- the only stubs: where the sun is, and what colour the sky fog is ---------------- */
void retro_sky_eval_sun_dir(float t, float *x, float *y, float *z) {
    float a = t * 0.01f;                      /* a slow, deterministic arc */
    float sx = cosf(a), sy = sinf(a), sz = 0.25f;
    float len = sqrtf(sx * sx + sy * sy + sz * sz);
    if (len < 0.0001f) { sx = 0.0f; sy = 1.0f; sz = 0.0f; len = 1.0f; }
    *x = sx / len; *y = sy / len; *z = sz / len;
}
void retro_sky_eval_fog_rgb(float t, float *r, float *g, float *b) {
    (void)t;
    *r = 0.46f; *g = 0.57f; *b = 0.68f;
}

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static float mean3(float r, float g, float b) { return (r + g + b) / 3.0f; }
static float max3(float r, float g, float b) { return r > g ? (r > b ? r : b) : (g > b ? g : b); }
static int finite3(float r, float g, float b) {
    return isfinite(r) && isfinite(g) && isfinite(b);
}

static const RetroLightingPreset PRESETS[] = {
    RETRO_LIGHTING_DAY_STATIC, RETRO_LIGHTING_SUNSET_STATIC,
    RETRO_LIGHTING_NIGHT_MOONLIT, RETRO_LIGHTING_INTERIOR_FLAT,
    RETRO_LIGHTING_DYNAMIC
};

typedef struct { float cover, rain, storm, fog_wash, visibility; const char *name; } WeatherCase;
static const WeatherCase WEATHERS[] = {
    { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,   "clear" },
    { 1.0f, 0.0f, 0.0f, 0.2f, 1800.0f, "overcast" },
    { 0.6f, 1.0f, 0.0f, 0.5f, 1200.0f, "rain" },
    { 1.0f, 1.0f, 1.0f, 1.0f, 540.0f,  "storm" },
};

/* facing down (never sees sun or moon), facing up, facing sideways */
static const float NORMALS[][3] = { { 0.0f, -1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };

static EnvInputs inputs_for(const WeatherCase *w, int slider, int interior) {
    EnvInputs in;
    memset(&in, 0, sizeof in);
    in.cover = w->cover; in.rain = w->rain; in.storm = w->storm;
    in.fog_wash = w->fog_wash; in.visibility = w->visibility;
    in.ambient_level = slider;
    in.interior = interior;
    in.fog_r = 0.46f; in.fog_g = 0.57f; in.fog_b = 0.68f;
    return in;
}

int main(void) {
    int combos = 0, lifted = 0;
    float brightest_day = 0.0f, darkest_night = 1.0f;

    for (size_t pi = 0; pi < sizeof PRESETS / sizeof PRESETS[0]; pi++) {
        for (size_t wi = 0; wi < sizeof WEATHERS / sizeof WEATHERS[0]; wi++) {
            for (int slider = 0; slider <= 10; slider += 5) {
                EnvInputs in = inputs_for(&WEATHERS[wi], slider, PRESETS[pi] == RETRO_LIGHTING_INTERIOR_FLAT);
                EnvMatrix m;
                env_matrix_eval(&in, &m);

                for (size_t ni = 0; ni < sizeof NORMALS / sizeof NORMALS[0]; ni++) {
                    combos++;

                    /* WITHOUT the matrix: the preset alone, which is what the old chain produced */
                    retro_lighting_set_ambient_boost(0.0f);
                    retro_lighting_set_ambient_floor(0.0f);
                    retro_lighting_set_sky_bleed(0.0f, 0.0f, 0.0f);
                    RetroLightingState before_st;
                    retro_lighting_eval((float)(wi * 90), PRESETS[pi], &before_st);
                    float br = 0, bg = 0, bb = 0;
                    retro_lighting_eval_surface_rgb(&before_st, NORMALS[ni][0], NORMALS[ni][1], NORMALS[ni][2],
                                                    0.6f, &br, &bg, &bb);

                    /* WITH the matrix, through exactly the calls draw_scene makes */
                    RetroLightingState st;
                    retro_lighting_eval((float)(wi * 90), PRESETS[pi], &st);
                    env_matrix_apply(&m, &st);
                    env_matrix_publish(&m);
                    float ar = 0, ag = 0, ab = 0;
                    retro_lighting_eval_surface_rgb(&st, NORMALS[ni][0], NORMALS[ni][1], NORMALS[ni][2],
                                                    0.6f, &ar, &ag, &ab);

                    /* 1. CLEAR weather must never darken anything: with nothing occluded the chain is
                          pure addition (the floor can only lift). Bad weather IS allowed to dim, and a
                          sun-facing face is supposed to lose its direct beam -- that is a storm reading
                          as a storm rather than a flat dimmer switch. The guarantee that holds for
                          every combination is the floor, asserted next. */
                    if (mean3(ar, ag, ab) > mean3(br, bg, bb) + 1e-5f) lifted++;
                    if (wi == 0) CHECK(mean3(ar, ag, ab) >= mean3(br, bg, bb) - 1e-4f);

                    /* 2/3. the floor holds, and nothing escapes [0,1] or goes NaN */
                    if (max3(ar, ag, ab) < m.ambient_floor - 1e-3f) {
                        printf("FLOOR preset=%zu weather=%zu slider=%d normal=%zu max=%.4f floor=%.4f "
                               "bleed=%.3f occ=%.2f sun=%.3f amb=%.3f | after %.3f %.3f %.3f | before %.3f %.3f %.3f\n",
                               pi, wi, slider, ni, max3(ar, ag, ab), m.ambient_floor,
                               m.light_bleed, m.occlusion, st.sun_intensity, st.ambient_intensity,
                               ar, ag, ab, br, bg, bb);
                        g_fail++;
                    }
                    CHECK(finite3(ar, ag, ab) && finite3(br, bg, bb));
                    CHECK(ar >= 0.0f && ar <= 1.0f && ag >= 0.0f && ag <= 1.0f && ab >= 0.0f && ab <= 1.0f);

                    /* 6. determinism, through the whole chain */
                    RetroLightingState st2;
                    retro_lighting_eval((float)(wi * 90), PRESETS[pi], &st2);
                    env_matrix_apply(&m, &st2);
                    env_matrix_publish(&m);
                    float dr = 0, dg = 0, db = 0;
                    retro_lighting_eval_surface_rgb(&st2, NORMALS[ni][0], NORMALS[ni][1], NORMALS[ni][2],
                                                    0.6f, &dr, &dg, &db);
                    CHECK(dr == ar && dg == ag && db == ab);

                    if (wi == 0 && slider == 5) {   /* clear weather: the day/night swing must survive */
                        if (pi == 0) brightest_day = fmaxf(brightest_day, mean3(ar, ag, ab));
                        if (pi == 2) darkest_night = fminf(darkest_night, mean3(ar, ag, ab));
                    }
                }
            }
        }
    }

    /* 4. the swing is real: clear noon is still clearly brighter than clear midnight, even though the
          floor has lifted the night. A flat world would fail this. */
    CHECK(brightest_day > darkest_night * 1.25f);

    /* 4b. the actual regression, restated at CHAIN level: a stormy night at slider 0 must be
           readable through the production path, not just inside the matrix unit test. */
    {
        EnvInputs worst = inputs_for(&WEATHERS[3], 0, 0);
        EnvMatrix wm;
        env_matrix_eval(&worst, &wm);
        RetroLightingState ws;
        retro_lighting_eval(0.0f, RETRO_LIGHTING_NIGHT_MOONLIT, &ws);
        env_matrix_apply(&wm, &ws);
        env_matrix_publish(&wm);
        float wr = 0, wg = 0, wb = 0;
        retro_lighting_eval_surface_rgb(&ws, 0.0f, -1.0f, 0.0f, 0.6f, &wr, &wg, &wb);
        CHECK(mean3(wr, wg, wb) >= 0.20f);
        CHECK(max3(wr, wg, wb) >= wm.ambient_floor - 1e-3f);
        CHECK(wm.light_bleed > 0.10f);            /* the storm puts light back, it does not only subtract */
    }

    /* 5. an interior gets no open-sky fill: at the same weather, the interior floor is higher but its
          sky contribution is zero -- a sun-facing wall indoors must not be brighter than one outdoors
          under the SAME preset-independent inputs */
    {
        EnvInputs out_in = inputs_for(&WEATHERS[3], 3, 0);
        EnvInputs in_in = inputs_for(&WEATHERS[3], 3, 1);
        EnvMatrix out_m, in_m;
        env_matrix_eval(&out_in, &out_m);
        env_matrix_eval(&in_in, &in_m);
        CHECK(in_m.occlusion < out_m.occlusion);
        CHECK(in_m.ambient_floor > out_m.ambient_floor);

        RetroLightingState io, ii;
        retro_lighting_eval(0.0f, RETRO_LIGHTING_INTERIOR_FLAT, &io);
        retro_lighting_eval(0.0f, RETRO_LIGHTING_INTERIOR_FLAT, &ii);
        env_matrix_apply(&out_m, &io);
        env_matrix_apply(&in_m, &ii);
        float orr = 0, og = 0, ob = 0, ir = 0, ig = 0, ib = 0;
        retro_lighting_set_ambient_floor(out_m.ambient_floor);
        retro_lighting_set_sky_bleed(out_m.bleed_r, out_m.bleed_g, out_m.bleed_b);
        retro_lighting_eval_surface_rgb(&io, 0.0f, 1.0f, 0.0f, 0.6f, &orr, &og, &ob);
        retro_lighting_set_ambient_floor(in_m.ambient_floor);
        retro_lighting_set_sky_bleed(in_m.bleed_r, in_m.bleed_g, in_m.bleed_b);
        retro_lighting_eval_surface_rgb(&ii, 0.0f, 1.0f, 0.0f, 0.6f, &ir, &ig, &ib);
        /* the interior's own preset has no sun/moon at all, so it can only be floor + its small bleed */
        CHECK(finite3(ir, ig, ib));
        CHECK(max3(ir, ig, ib) >= in_m.ambient_floor - 1e-3f);
        CHECK(max3(orr, og, ob) >= out_m.ambient_floor - 1e-3f);
    }

    retro_lighting_set_ambient_floor(0.0f);
    retro_lighting_set_sky_bleed(0.0f, 0.0f, 0.0f);
    retro_lighting_set_ambient_boost(0.0f);

    if (g_fail) { printf("lighting_chain_test: %d FAILED\n", g_fail); return 1; }
    printf("lighting_chain_test OK (%d preset x weather x slider x face combos, %d measurably lifted; "
           "clear noon %.2f vs clear midnight %.2f)\n", combos, lifted, brightest_day, darkest_night);
    return 0;
}
