#include "env_matrix.h"

#include <math.h>
#include <string.h>

/* The PARENA side (packages/simulation/lighting_rules.c, generated from
 * PARENA/stdlib/shankpit/lighting_rules.prn by scripts/gen_lighting_rules.sh). Every lighting number
 * this file hands out came from one of these; permille in, permille out, scalars only. */
int env_occlusion(int cover, int rain, int storm, int fog_wash, int interior);
int env_light_bleed(int occlusion, int interior);
int env_exposure(int occlusion);
int env_ambient_floor(int occlusion, int ambient_level, int interior);
int env_emissive_scale(int ambient_permille);
int env_emitter_range_scale(int visibility);
int env_emitter_spread_scale(int range_scale);
int env_flash_boost(int flash);

static float clamp01f(float v) {
    if (!(v > 0.0f)) return 0.0f;   /* also catches NaN */
    if (v > 1.0f) return 1.0f;
    return v;
}

static float clampf(float v, float lo, float hi) {
    if (!(v > lo)) return lo;
    if (v > hi) return hi;
    return v;
}

/* 0..1 float -> 0..1000 permille, the unit every lighting_rules function speaks. */
static int permille(float v) {
    return (int)(clamp01f(v) * 1000.0f + 0.5f);
}

/* World units -> the integer the emitter rule takes. Non-positive means "no weather fog", which the
 * rule turns into full reach; a missing reading must never shorten a light. */
static int world_units(float v) {
    if (!(v > 0.0f)) return 0;
    if (v > 1000000.0f) return 1000000;
    return (int)(v + 0.5f);
}

void env_matrix_eval(const EnvInputs *in, EnvMatrix *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
    EnvInputs fallback;
    if (!in) {
        memset(&fallback, 0, sizeof fallback);
        in = &fallback;
    }

    int interior = in->interior ? 1 : 0;
    int level = in->ambient_level;
    if (level < 0) level = 0;
    if (level > 10) level = 10;
    int vis = world_units(in->visibility);

    int occ = env_occlusion(permille(in->cover), permille(in->rain), permille(in->storm),
                            permille(in->fog_wash), interior);
    out->occlusion = occ * 0.001f;

    int bleed = env_light_bleed(occ, interior);

    /* Lightning is a light source, not a screen tint: a strike throws its own scattered light into
     * the same bleed channel the rest of the atmosphere uses. That is what makes a bolt light the
     * storm deck and the walls under it instead of only flashing the overlay. */
    out->flash_boost = env_flash_boost(permille(in->flash)) * 0.001f;
    out->light_bleed = clamp01f(bleed * 0.001f + out->flash_boost);

    /* Scattered light takes the sky's own current colour, so a storm's glow is the storm's colour
     * rather than a generic grey. A caller that passes no colour gets a neutral cool haze. A strike
     * pushes that colour toward white by the same fraction, because a bolt is broadband light. */
    float base_r = (in->fog_r > 0.0f) ? in->fog_r : 0.42f;
    float base_g = (in->fog_g > 0.0f) ? in->fog_g : 0.46f;
    float base_b = (in->fog_b > 0.0f) ? in->fog_b : 0.52f;
    float whiten = clamp01f(out->flash_boost / 0.30f);
    base_r += (1.0f - base_r) * whiten;
    base_g += (1.0f - base_g) * whiten;
    base_b += (1.0f - base_b) * whiten;
    out->bleed_r = clamp01f(base_r * out->light_bleed);
    out->bleed_g = clamp01f(base_g * out->light_bleed);
    out->bleed_b = clamp01f(base_b * out->light_bleed);

    out->exposure = env_exposure(occ) * 0.001f + out->flash_boost * 0.5f;
    out->ambient_floor = env_ambient_floor(occ, level, interior) * 0.001f;

    /* Emissives are scaled by what the scene actually delivers (the floor), not by the raw ambient
     * the weather left behind -- the floor is the number the surfaces are really lit to. */
    int floor_pm = (int)(out->ambient_floor * 1000.0f + 0.5f);
    out->emissive_scale = env_emissive_scale(floor_pm) * 0.001f;

    out->emitter_range_scale = env_emitter_range_scale(vis) * 0.001f;
    out->emitter_spread_scale =
        env_emitter_spread_scale((int)(out->emitter_range_scale * 1000.0f + 0.5f)) * 0.001f;

    out->exposure = clampf(out->exposure, 1.0f, 4.0f);
    out->ambient_floor = clamp01f(out->ambient_floor);
    out->emissive_scale = clampf(out->emissive_scale, 0.25f, 4.0f);
    out->emitter_range_scale = clampf(out->emitter_range_scale, 0.05f, 1.0f);
    out->emitter_spread_scale = clampf(out->emitter_spread_scale, 1.0f, 3.0f);
}

void env_matrix_apply(const EnvMatrix *m, RetroLightingState *io) {
    if (!m || !io) return;
    float keep = 1.0f - clamp01f(m->occlusion);

    /* Ambient is dimmed only gently (18% of it, at the very worst): it is the term the floor
     * protects, and a hard multiplier here is precisely what used to crush walls to black. Sun and
     * moon -- the DIRECT terms -- take the full occlusion, which is what keeps a storm reading as
     * "the sun is gone" instead of "the world got a flat dimmer switch". */
    io->ambient_intensity = clamp01f(io->ambient_intensity * (0.82f + 0.18f * keep));
    io->sun_intensity = clamp01f(io->sun_intensity * keep * keep);
    io->moon_intensity = clamp01f(io->moon_intensity * keep);

    /* The scattered share is published through env_matrix_publish (retro_lighting's sky bleed), not
     * added to the ambient colour here: bleed is atmosphere, so it must land on every surface
     * equally, not be multiplied by each face's own ambient_floor factor. */
}

/* The published state. It lives here, not in retro_lighting.c, so that the surface transform, its
 * two inputs and the setters retro_lighting.h declares all sit in one headless-testable unit --
 * retro_lighting.c is GL-bound and cannot be linked in this sandbox or in a plain CI job. */
static float g_published_floor = 0.0f;
static float g_published_bleed[3] = { 0.0f, 0.0f, 0.0f };

void retro_lighting_set_ambient_floor(float floor) { g_published_floor = clamp01f(floor); }
void retro_lighting_set_sky_bleed(float r, float g, float b) {
    g_published_bleed[0] = clamp01f(r);
    g_published_bleed[1] = clamp01f(g);
    g_published_bleed[2] = clamp01f(b);
}

void env_matrix_publish(const EnvMatrix *m) {
    if (!m) return;
    retro_lighting_set_ambient_floor(m->ambient_floor);
    retro_lighting_set_sky_bleed(m->bleed_r, m->bleed_g, m->bleed_b);
}

void env_matrix_apply_published_surface(float *r, float *g, float *b) {
    env_matrix_apply_surface(g_published_floor, g_published_bleed, r, g, b);
}

void env_matrix_apply_surface(float floor, const float bleed[3], float *r, float *g, float *b) {
    if (!r || !g || !b) return;
    float cr = *r, cg = *g, cb = *b;

    /* Scattered atmosphere light, added after the per-face terms on purpose: it arrives from every
     * direction, so it must not be multiplied by this face's own ambient_floor factor. */
    if (bleed) {
        cr += bleed[0];
        cg += bleed[1];
        cb += bleed[2];
    }

    /* THE VISIBILITY FLOOR (founder real-time, 2026-10-30: "it always looks too dark everywhere").
     * Applied as a deficit fill -- add the shortfall to the brightest channel's level, never clamp
     * each channel to the floor -- so a face keeps its hue instead of washing flat. The fill is
     * slightly cool and blue-weighted, which is what atmospheric haze actually looks like. */
    floor = clamp01f(floor);
    if (floor > 0.0f) {
        float m = cr > cg ? (cr > cb ? cr : cb) : (cg > cb ? cg : cb);
        if (m < floor) {
            float deficit = floor - m;
            cr += deficit * 0.85f;
            cg += deficit * 0.92f;
            cb += deficit;
        }
    }

    *r = clamp01f(cr);
    *g = clamp01f(cg);
    *b = clamp01f(cb);
}
