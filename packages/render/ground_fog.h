/* ground_fog.h -- weather fog that pools ON THE GROUND, lit by the unified environment matrix.
 *
 * The world fog that already exists (sky_weather_fog_on) is GL_EXP2 in DISTANCE: everything fades
 * with range, evenly from your boots to the top of the sky and from the camera to the horizon. Real
 * fog does not do that. Radiation fog, dawn mist and the scud under a storm all pool in the bottom
 * few metres -- you can stand in pea soup and still see the top of a tower against the sky. That
 * height term is what this file adds, layered ON TOP of the distance fog rather than replacing it.
 *
 * "Obeys the global volumetric lighting" is the other half, and it is why the model takes an
 * EnvMatrix instead of a hard-coded grey: fog is lit. It takes the sky's own weather-matched fog
 * colour, mixes in the same scattered-light bleed the walls get, is dimmed by the atmosphere's
 * occlusion and lifted by its exposure, and a lightning strike brightens AND whitens it -- a bolt is
 * broadband light, not a grey flash. Same numbers, same frame, one atmosphere: the fog cannot drift
 * out of agreement with the sun and the storm because it is reading the values the rest of the
 * renderer just resolved, not deriving a second set.
 *
 * Deliberately GL-free and pure (no GLuint, no <SDL2/...>): this is the exact code the headless
 * test exercises AND the exact code the lobby draws with, which is the same split
 * retro_sky_eval.h / env_matrix.c already use. The drawing (a few stacked translucent sheets) is the
 * host's job and lives in the lobby, because it is the only part that needs a GPU.
 *
 * Not modelled, named rather than faked: no per-sample ray marching, no light shafts through the
 * fog, no local fog volumes, no shadowing of the fog itself. Those need a deferred pass or a
 * shadow-map lookup per pixel; this is a forward renderer on fixed-function GL, and a stack of lit
 * sheets is what it can actually pay for. */
#ifndef SHANKPIT_GROUND_FOG_H
#define SHANKPIT_GROUND_FOG_H

#include <math.h>
#include <string.h>

typedef struct {
    /* SkyWeather's own already-smoothed fields -- the same numbers driving the dome and the clouds. */
    float fog_wash;          /* 0..1: how much the sky itself is swallowed by fog */
    float visibility;        /* meteorological visibility in WORLD units, <= 0 when there is no fog */
    float cover, storm;      /* 0..1 weather intensities */
    float fog_r, fog_g, fog_b;   /* SkyWeather.fog[] -- the lit colour the sky already fades into */
    float ground_y;          /* ground height under the camera, world units */
    /* The unified environment matrix, same frame, same values the surfaces were lit with. */
    float occlusion;         /* 0..0.78 how much sky light the weather blocks */
    float light_bleed;       /* 0..0.32 the share the atmosphere scatters instead */
    float bleed_r, bleed_g, bleed_b;   /* that scattered light, in the sky's own colour */
    float exposure;          /* >= 1 low-end compensation for the dimming */
    float flash_boost;       /* 0..0.30 extra scattered light a lightning strike throws right now */
    int interior;            /* 1 = enclosed level: no sky to radiate fog, so no fog pools indoors */
} GroundFogInputs;

typedef struct {
    int layers;              /* 0 = nothing to draw this frame */
    float base_y;            /* world y the pool sits on */
    float thickness;         /* total height the fog occupies, world units */
    float scale_height;      /* world units for the density to fall to 1/e */
    float density;           /* 0..1 at ground level -- the pea-soup number */
    float r, g, b;           /* colour, already lit by the environment matrix, 0..1 */
    float flash;             /* 0..1 amount a strike is lighting the fog right now */
    int lit;                 /* 0 when almost no light reaches it (draw it darker, not brighter) */
} GroundFog;

/* Resolves the frame's ground fog. Never fails; a clear sky, an interior or a NULL input all
 * resolve to layers == 0 and every other field zeroed, which is a no-op for the draw. */
void ground_fog_eval(const GroundFogInputs *in, GroundFog *out);

/* Density at a height ABOVE THE GROUND (world units), exponential falloff. 1.0 at the ground, 1/e at
 * scale_height, and effectively gone by the top of `thickness` -- the property that lets you see a
 * tower's top while standing in the soup. */
float ground_fog_density_at(const GroundFog *gf, float y_above_ground);

/* Alpha for sheet `i` of `gf->layers`. The bottom sheet carries most of the opacity and the stack
 * thins upward; capped well below 1 so a storm is fog, not a grey wall. */
float ground_fog_layer_alpha(const GroundFog *gf, int i);

/* World y for sheet `i` -- what the host actually draws at. Kept here so the draw and the test
 * cannot disagree about where the layers are. */
float ground_fog_layer_y(const GroundFog *gf, int i);

/* ---- implementation ------------------------------------------------------------------------ */

static float gf_clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static float gf_mix(float a, float b, float t) { return a + (b - a) * t; }

static void ground_fog_eval_impl(const GroundFogInputs *in, GroundFog *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
    if (!in) return;

    float wash = gf_clamp01(in->fog_wash);
    float storm = gf_clamp01(in->storm);
    float cover = gf_clamp01(in->cover);

    /* How much fog there is to pool. fog_wash is the sky's own "swallowed by fog" number, so the
     * ground fog can never appear before the sky admits there is fog. A storm drags scud down on top
     * of that, overcast thickens it a little, and a real visibility under ~900 units means the fog is
     * the scene rather than a mood. */
    float density = wash;
    if (in->visibility > 0.0f && in->visibility < 900.0f)
        density += (1.0f - in->visibility / 900.0f) * 0.55f;
    density += cover * 0.06f;
    density *= 0.55f + 0.45f * storm;
    density = gf_clamp01(density);

    /* Indoors there is no sky to radiate fog, and a pool of it inside a sealed room would be a bug,
     * not weather. Same reasoning as env_matrix's interior case. */
    if (in->interior) density = 0.0f;
    if (density < 0.02f) return;   /* layers stays 0: a bit-for-bit no-op draw */

    out->layers = 3;
    out->density = density;
    out->base_y = in->ground_y;
    /* Storm scud is shallow and it sits low; radiation fog is a deeper, softer pool. */
    out->scale_height = 15.0f - 8.0f * storm;
    if (out->scale_height < 5.0f) out->scale_height = 5.0f;
    out->thickness = out->scale_height * 2.6f;

    /* Lit, not tinted. Start from the sky's weather-matched fog colour, mix in the scattered bleed
     * the walls are getting this frame, then scale by how much light survives the cloud deck. */
    float bleed = gf_clamp01(in->light_bleed / 0.32f);   /* 0..1 across the rule's own 0..0.32 range */
    float occ = in->occlusion < 0.0f ? 0.0f : (in->occlusion > 1.0f ? 1.0f : in->occlusion);
    float exposure = in->exposure < 0.0f ? 0.0f : (in->exposure > 4.0f ? 4.0f : in->exposure);
    float flash = gf_clamp01(in->flash_boost / 0.30f);   /* 0..1 across the rule's own 0..0.30 range */
    float lit = exposure * (1.0f - occ * 0.55f);
    if (lit < 0.0f) lit = 0.0f;
    if (lit > 2.0f) lit = 2.0f;

    float r = gf_mix(in->fog_r, in->bleed_r, bleed);
    float g = gf_mix(in->fog_g, in->bleed_g, bleed);
    float b = gf_mix(in->fog_b, in->bleed_b, bleed);
    r = gf_clamp01(r * lit);
    g = gf_clamp01(g * lit);
    b = gf_clamp01(b * lit);

    /* A bolt is broadband light: it whitens the fog as well as brightening it, so a strike reads as
     * a flash through the soup instead of the soup briefly becoming beige. */
    if (flash > 0.0f) {
        float t = flash * 0.55f;
        r = gf_mix(r, 1.0f, t);
        g = gf_mix(g, 1.0f, t);
        b = gf_mix(b, 1.0f, t);
    }

    out->r = r;
    out->g = g;
    out->b = b;
    out->flash = flash;
    out->lit = lit > 0.05f ? 1 : 0;
}

static float ground_fog_density_at_impl(const GroundFog *gf, float y_above_ground) {
    if (!gf || gf->layers <= 0 || gf->scale_height <= 0.0f) return 0.0f;
    if (y_above_ground <= 0.0f) return gf->density;
    return gf->density * expf(-y_above_ground / gf->scale_height);
}

static float ground_fog_layer_y_impl(const GroundFog *gf, int i) {
    if (!gf || i < 0 || i >= gf->layers) return 0.0f;
    /* Sheet 0 is the pool itself; the rest climb through the thickness. */
    float t = (float)i / (float)gf->layers;
    return gf->base_y + t * gf->thickness;
}

static float ground_fog_layer_alpha_impl(const GroundFog *gf, int i) {
    if (!gf || i < 0 || i >= gf->layers) return 0.0f;
    float y = ground_fog_layer_y_impl(gf, i);
    float h = y - gf->base_y;
    float a = ground_fog_density_at_impl(gf, h) * 0.62f;
    if (a > 0.55f) a = 0.55f;
    if (a < 0.0f) a = 0.0f;
    return a;
}

/* The header is the implementation on purpose: header-only like structure.h / material_draw.h, so
 * there is no extra translation unit to add to four build lists, and the lobby and the test are
 * provably running the same instructions. These aliases keep the public names short. */
#define ground_fog_eval ground_fog_eval_impl
#define ground_fog_density_at ground_fog_density_at_impl
#define ground_fog_layer_alpha ground_fog_layer_alpha_impl
#define ground_fog_layer_y ground_fog_layer_y_impl

#endif /* SHANKPIT_GROUND_FOG_H */
