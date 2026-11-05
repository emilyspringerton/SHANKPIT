/* ground_fog_test.c -- the ground fog model, headless (no GL, no display).
 *
 * What it has to prove is what the founder asked for: fog when the weather is foggy/stormy, lying
 * CLOSE TO THE GROUND rather than evenly through the sky, and lit by the same global environment
 * matrix as everything else -- so a strike lights it, occlusion darkens it, and clear weather has
 * none of it.
 *
 * Build/run: make test-ground-fog
 */
#include <math.h>
#include <stdio.h>

#include "ground_fog.h"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static float lum(float r, float g, float b) { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }

/* A clear noon, on the ground, with an environment matrix evaluated for that weather. */
static GroundFogInputs clear_noon(void) {
    GroundFogInputs in;
    memset(&in, 0, sizeof in);
    in.fog_wash = 0.0f;
    in.visibility = 3000.0f;
    in.cover = 0.24f;
    in.storm = 0.0f;
    in.fog_r = 0.60f; in.fog_g = 0.79f; in.fog_b = 0.95f;
    in.occlusion = 0.0f;
    in.light_bleed = 0.0f;
    in.bleed_r = in.bleed_g = in.bleed_b = 0.0f;
    in.exposure = 1.0f;
    in.flash_boost = 0.0f;
    in.ground_y = 0.0f;
    return in;
}

static GroundFogInputs stormy(void) {
    GroundFogInputs in = clear_noon();
    in.fog_wash = 1.0f;
    in.visibility = 540.0f;
    in.cover = 1.0f;
    in.storm = 1.0f;
    in.fog_r = 0.55f; in.fog_g = 0.58f; in.fog_b = 0.64f;
    in.occlusion = 0.78f;
    in.light_bleed = 0.32f;
    in.bleed_r = 0.62f; in.bleed_g = 0.65f; in.bleed_b = 0.72f;
    in.exposure = 1.12f;
    return in;
}

int main(void) {
    GroundFog clear_gf, storm_gf;

    /* ---- clear weather has no ground fog at all (a bit-for-bit no-op draw) ---- */
    {
        GroundFogInputs in = clear_noon();
        ground_fog_eval(&in, &clear_gf);
        CHECK(clear_gf.layers == 0, "clear noon grew %d ground-fog layers", clear_gf.layers);
        CHECK(clear_gf.density == 0.0f, "clear noon had density %.3f", clear_gf.density);
        printf("PASS: clear noon draws no ground fog (layers %d)\n", clear_gf.layers);
    }

    /* ---- a storm puts fog on the ground, and it is dense ---- */
    {
        GroundFogInputs in = stormy();
        ground_fog_eval(&in, &storm_gf);
        CHECK(storm_gf.layers > 0, "a storm grew no ground fog");
        CHECK(storm_gf.density > 0.5f, "storm ground fog is thin: %.3f", storm_gf.density);
        CHECK(storm_gf.density <= 1.0f, "storm ground fog density out of range: %.3f", storm_gf.density);
        printf("PASS: a storm pools %.2f density of fog in %d layers\n",
               storm_gf.density, storm_gf.layers);
    }

    /* ---- CLOSE TO THE GROUND: dense at the boots, gone by the tower tops ---- */
    {
        GroundFogInputs in = stormy();
        ground_fog_eval(&in, &storm_gf);
        float at_ground = ground_fog_density_at(&storm_gf, 0.0f);
        float at_3 = ground_fog_density_at(&storm_gf, 3.0f);
        float at_top = ground_fog_density_at(&storm_gf, storm_gf.thickness);
        float at_60 = ground_fog_density_at(&storm_gf, 60.0f);
        CHECK(at_ground > 0.5f, "no fog at ground level (%.3f)", at_ground);
        CHECK(at_3 < at_ground, "fog does not thin with height: %.3f at 0 vs %.3f at 3", at_ground, at_3);
        CHECK(at_top < at_ground * 0.15f, "fog is still %.3f thick at the top of its own layer", at_top);
        CHECK(at_60 < 0.02f, "fog at 60 units up is %.3f -- it is not hugging the ground", at_60);
        /* The property that makes it READ as ground fog: from up high the air is clear. */
        CHECK(at_60 < at_ground * 0.05f, "the pool at the boots (%.3f) and at 60 up (%.3f) barely differ",
              at_ground, at_60);
        printf("PASS: fog hugs the ground (%.2f at 0, %.2f at 3, %.3f at %.0f up, %.3f at 60 up)\n",
               at_ground, at_3, at_top, storm_gf.thickness, at_60);
    }

    /* ---- storm scud sits LOWER than a calm fog bank -- that is what makes a storm read ---- */
    {
        GroundFogInputs calm = clear_noon();
        GroundFog calm_gf, scud_gf;
        calm.fog_wash = 0.85f;
        calm.visibility = 700.0f;
        calm.cover = 0.95f;
        calm.storm = 0.10f;
        ground_fog_eval(&calm, &calm_gf);
        GroundFogInputs scud = stormy();
        ground_fog_eval(&scud, &scud_gf);
        CHECK(calm_gf.layers > 0, "a calm fog bank grew nothing");
        CHECK(scud_gf.scale_height < calm_gf.scale_height,
              "storm scud is not shallower than a calm bank (%.1f vs %.1f)",
              scud_gf.scale_height, calm_gf.scale_height);
        CHECK(scud_gf.density > calm_gf.density, "the storm is thinner than the calm fog (%.2f vs %.2f)",
              scud_gf.density, calm_gf.density);
        printf("PASS: storm scud is shallower (%.1f) and denser (%.2f) than a calm bank (%.1f / %.2f)\n",
               scud_gf.scale_height, scud_gf.density, calm_gf.scale_height, calm_gf.density);
    }

    /* ---- worse visibility means more fog, monotonically ---- */
    {
        float prev = -1.0f;
        for (float v = 900.0f; v >= 60.0f; v *= 0.6f) {
            GroundFogInputs in = clear_noon();
            in.fog_wash = 0.4f;
            in.visibility = v;
            GroundFog gf;
            ground_fog_eval(&in, &gf);
            CHECK(prev < 0.0f || gf.density >= prev, "density fell as visibility got worse (%.3f at %.0f)",
                  gf.density, v);
            prev = gf.density;
        }
        printf("PASS: density rises monotonically as visibility closes in\n");
    }

    /* ---- IT OBEYS THE LIGHTING: a lightning strike lights the fog and whitens it ---- */
    {
        GroundFogInputs dark = stormy();
        GroundFogInputs struck = stormy();
        GroundFog before, after;
        struck.flash_boost = 0.30f;      /* the rule's own ceiling */
        ground_fog_eval(&dark, &before);
        ground_fog_eval(&struck, &after);
        float lb = lum(before.r, before.g, before.b), la = lum(after.r, after.g, after.b);
        CHECK(la > lb, "a strike did not light the fog (%.3f -> %.3f)", lb, la);
        CHECK(after.flash > 0.9f, "the flash did not reach the fog (%.2f)", after.flash);
        /* Broadband light: the channels converge on white, so the fog whitens as well as brightens. */
        float spread_b = fabsf(before.r - before.b), spread_a = fabsf(after.r - after.b);
        CHECK(spread_a < spread_b, "the strike tinted the fog (spread %.3f -> %.3f) instead of whitening it",
              spread_b, spread_a);
        printf("PASS: a strike lights the fog %.3f -> %.3f luminance and whitens it\n", lb, la);
    }

    /* ---- occlusion darkens it, exposure lifts it: the same knobs the walls use ---- */
    {
        GroundFogInputs bright = stormy();
        GroundFogInputs occluded = stormy();
        GroundFog b, o;
        bright.occlusion = 0.0f;
        occluded.occlusion = 0.78f;
        ground_fog_eval(&bright, &b);
        ground_fog_eval(&occluded, &o);
        CHECK(lum(o.r, o.g, o.b) < lum(b.r, b.g, b.b),
              "occlusion did not darken the fog (%.3f vs %.3f)", lum(o.r, o.g, o.b), lum(b.r, b.g, b.b));

        GroundFogInputs lift = stormy();
        GroundFog l;
        lift.exposure = 1.6f;
        ground_fog_eval(&lift, &l);
        CHECK(lum(l.r, l.g, l.b) > lum(o.r, o.g, o.b), "exposure did not lift the fog");
        printf("PASS: occlusion darkens the fog and exposure lifts it\n");
    }

    /* ---- the bleed the walls receive is the colour the fog takes ---- */
    {
        GroundFogInputs in = stormy();
        GroundFog a, b;
        ground_fog_eval(&in, &a);
        in.light_bleed = 0.0f;          /* no scattered light at all */
        ground_fog_eval(&in, &b);
        CHECK(a.r != b.r || a.g != b.g || a.b != b.b, "the bleed changed nothing about the fog colour");
        printf("PASS: the fog colour follows the scattered light the surfaces get\n");
    }

    /* ---- indoors there is no sky to radiate fog ---- */
    {
        GroundFogInputs in = stormy();
        GroundFog gf;
        in.interior = 1;
        ground_fog_eval(&in, &gf);
        CHECK(gf.layers == 0, "an interior grew %d ground-fog layers", gf.layers);
        printf("PASS: an enclosed level gets no ground fog\n");
    }

    /* ---- WIND blows the fog away (founder: "blows all the ... fog away") ---- */
    {
        /* Not the fully saturated storm here: a pea-souper is already clamped to 1.0, so the RATIO has to
           be measured on a bank with room to thin, or the test would be measuring the clamp. */
        GroundFogInputs still = stormy();
        GroundFogInputs windy = stormy();
        GroundFog a, b;
        still.fog_wash = 0.5f; still.visibility = 3000.0f; still.storm = 0.2f;
        windy = still;
        still.wind_thin = 1.0f;          /* still air: the rules' own "untouched" value */
        windy.wind_thin = 0.4f;          /* a gale: the floor of weather-wind-fog-thin-permille */
        ground_fog_eval(&still, &a);
        ground_fog_eval(&windy, &b);
        CHECK(fabsf(b.density - a.density * 0.4f) < 1.0e-4f,
              "wind did not thin the density proportionally (%.3f vs %.3f)", b.density, a.density);
        CHECK(ground_fog_density_at(&b, 2.0f) < ground_fog_density_at(&a, 2.0f),
              "the wind thinned the density field but not the fog at head height");
        printf("PASS: a gale thins the pooled fog to %.2f of its still-air density\n", b.density / a.density);

        /* and a saturated bank still gets thinner, it just cannot get denser than the clamp */
        GroundFogInputs soup = stormy();
        GroundFogInputs soup_windy = stormy();
        GroundFog s, sw;
        soup.wind_thin = 1.0f;
        soup_windy.wind_thin = 0.4f;
        ground_fog_eval(&soup, &s);
        ground_fog_eval(&soup_windy, &sw);
        CHECK(sw.density < s.density, "a gale did not thin an already-saturated fog bank (%.3f vs %.3f)",
              sw.density, s.density);
    }

    /* ---- no wind data is not the same thing as maximum wind ---- */
    {
        GroundFogInputs none = stormy();
        GroundFogInputs dead = stormy();
        GroundFog a, b;
        none.wind_thin = 0.0f;           /* host predates the wind rules */
        dead.wind_thin = 0.0001f;        /* the rules' floor, essentially */
        ground_fog_eval(&none, &a);
        ground_fog_eval(&dead, &b);
        CHECK(a.density > 0.0f && a.density > b.density,
              "0 wind_thin was treated as no fog (%.3f vs %.3f) instead of no data", a.density, b.density);
        printf("PASS: 0 wind_thin means \"no wind data\", not \"wind removed all the fog\"\n");
    }

    /* ---- layers stack upward with decreasing alpha, all sane and all above the ground ---- */
    {
        GroundFogInputs in = stormy();
        GroundFog gf;
        ground_fog_eval(&in, &gf);
        float prev_a = 2.0f, prev_y = -1.0e9f;
        for (int i = 0; i < gf.layers; i++) {
            float a = ground_fog_layer_alpha(&gf, i);
            float y = ground_fog_layer_y(&gf, i);
            CHECK(a > 0.0f && a <= 0.55f, "layer %d alpha out of range: %.3f", i, a);
            CHECK(a <= prev_a, "layer %d is more opaque than the one below it", i);
            CHECK(y >= prev_y, "layer %d sits below layer %d", i, i - 1);
            CHECK(y >= gf.base_y, "layer %d is below the ground (%.2f < %.2f)", i, y, gf.base_y);
            prev_a = a;
            prev_y = y;
        }
        CHECK(ground_fog_layer_alpha(&gf, -1) == 0.0f, "a negative layer index returned alpha");
        CHECK(ground_fog_layer_alpha(&gf, gf.layers) == 0.0f, "a layer past the top returned alpha");
        printf("PASS: %d layers stack upward, thinning from %.2f to %.2f\n",
               gf.layers, ground_fog_layer_alpha(&gf, 0), ground_fog_layer_alpha(&gf, gf.layers - 1));
    }

    /* ---- colour is always finite and inside [0,1], whatever is thrown at it ---- */
    {
        const float weird[] = { -5.0f, 0.0f, 0.5f, 1.0f, 99.0f };
        for (int i = 0; i < 5; i++) {
            for (int j = 0; j < 5; j++) {
                GroundFogInputs in = stormy();
                GroundFog gf;
                in.fog_wash = weird[i];
                in.visibility = weird[j];
                in.occlusion = weird[j];
                in.exposure = weird[i];
                in.flash_boost = weird[j];
                in.light_bleed = weird[i];
                ground_fog_eval(&in, &gf);
                CHECK(isfinite(gf.r) && isfinite(gf.g) && isfinite(gf.b), "non-finite colour at %d/%d", i, j);
                CHECK(gf.r >= 0.0f && gf.r <= 1.0f && gf.g >= 0.0f && gf.g <= 1.0f &&
                      gf.b >= 0.0f && gf.b <= 1.0f, "colour out of range at %d/%d: %.2f %.2f %.2f",
                      i, j, gf.r, gf.g, gf.b);
                CHECK(gf.density >= 0.0f && gf.density <= 1.0f, "density out of range at %d/%d", i, j);
                CHECK(gf.layers == 0 || (gf.thickness > 0.0f && gf.scale_height > 0.0f),
                      "layers without a thickness at %d/%d", i, j);
            }
        }
        printf("PASS: 25 hostile input combinations stay finite and in range\n");
    }

    /* ---- deterministic: same inputs, same fog, bit for bit (every client agrees) ---- */
    {
        GroundFogInputs in = stormy();
        GroundFog a, b;
        ground_fog_eval(&in, &a);
        ground_fog_eval(&in, &b);
        CHECK(memcmp(&a, &b, sizeof a) == 0, "the same inputs produced different fog");
        GroundFog none;
        ground_fog_eval(NULL, &none);
        CHECK(none.layers == 0, "a NULL input produced fog");
        ground_fog_eval(&in, NULL);   /* must not crash */
        printf("PASS: deterministic to the bit, and total on NULL\n");
    }

    printf(fails ? "FAILURES: %d\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
