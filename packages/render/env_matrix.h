#ifndef ENV_MATRIX_H
#define ENV_MATRIX_H

/* env_matrix.h -- the host half of SHANKPIT's unified environment matrix.
 *
 * Every decision here belongs to PARENA (PARENA/stdlib/shankpit/lighting_rules.prn, generated into
 * packages/simulation/lighting_rules.c). This file converts units, feeds the results into
 * RetroLightingState, and publishes the two values that live inside the surface-lighting function.
 * It deliberately contains no lighting policy of its own -- if a number needs retuning, it changes
 * in the .prn and nothing here moves.
 *
 * WHY THIS EXISTS (founder real-time, 2026-10-30): "we have cool weather effects but it doesnt work
 * with the ambient lighting like it always looks too dark everywhere ... all the lighting needs to
 * talk to each other". Weather, the day/night preset, the pause-menu ambient slider and the emitters
 * each adjusted the scene alone and every one of them could only subtract, so a stormy night at
 * slider 0 collapsed the walls to near-black. One evaluation per frame now resolves the atmosphere
 * once and every consumer reads the same numbers: surface lighting (ambient dimming + floor + sky
 * bleed), world fog, outline/shield emissive strength, and flashlight/headlight reach and spread.
 *
 * Cost/network: pure function of values the client already has (the day/night clock and the weather
 * profile, both broadcast in the existing world-clock packet). One CPU evaluation per frame, no
 * server work, nothing new on the wire, no per-entity state -- the cheapest shape that scales to an
 * MMO client, and deterministic: the same broadcast flags produce the same matrix on every client,
 * bit for bit, so nobody sees a different world. */

#include "retro_lighting.h"

typedef struct {
    float cover, rain, storm;  /* SkyWeather's own already-smoothed 0..1 weather intensities */
    float fog_wash;            /* 0..1: how much the sky itself is swallowed by fog */
    float visibility;          /* meteorological visibility in WORLD units; <= 0 = no weather fog */
    int ambient_level;         /* pause-menu slider, 0..10 (range owned by ambient_rules.prn) */
    int interior;              /* 1 = enclosed custom level (no sky exposure) */
    float fog_r, fog_g, fog_b; /* the sky's current fog colour -- the colour scattered light takes */
    float flash;               /* 0..1 lightning bolt intensity (SkyWeather.flash), 0 when no strike */
} EnvInputs;

typedef struct {
    float occlusion;        /* 0..0.78: how much sky light the weather blocks */
    float light_bleed;      /* 0..0.32: the share the atmosphere scatters instead */
    float bleed_r, bleed_g, bleed_b;  /* that scattered light, in the sky's own colour */
    float exposure;         /* >= 1: low-end compensation for the dimming */
    float ambient_floor;    /* hard minimum for a surface's ambient term (competitive clarity) */
    float emissive_scale;   /* ~0.75..1.6 for outlines / shield FX, inverse of scene brightness */
    float emitter_range_scale;   /* 0.45..1.0 flashlight/headlight reach, shortened by haze */
    float emitter_spread_scale;  /* 1.0..1.6 the complementary widening */
    float flash_boost;      /* 0..0.30 extra scattered light a lightning strike throws right now */
} EnvMatrix;

/* Evaluates the matrix. Never fails: NULL/out-of-range inputs are clamped to a sane, total result. */
void env_matrix_eval(const EnvInputs *in, EnvMatrix *out);

/* Applies the atmospheric dimming to an already-evaluated RetroLightingState (call after
 * retro_lighting_eval + any preset tuning, where sky_weather_light_state used to sit). */
void env_matrix_apply(const EnvMatrix *m, RetroLightingState *io);

/* Hands the two globals to retro_lighting: the surface ambient floor and the scattered sky bleed.
 * Call once per frame after evaluation; every surface drawn afterwards inherits both. */
void env_matrix_publish(const EnvMatrix *m);

/* The surface transform itself: add the scattered sky bleed, then lift the term to the floor.
 * Lives here (not inside retro_lighting.c) on purpose -- it is plain scalar maths with no GL in
 * sight, so it is the exact code that runs in the game AND the code the headless test exercises.
 * retro_lighting_eval_surface_rgb is its only production caller. A NULL bleed pointer and a 0
 * floor are a bit-for-bit no-op, which is what every path that never publishes a matrix gets. */
void env_matrix_apply_surface(float floor, const float bleed[3], float *r, float *g, float *b);

/* The same transform, using whatever env_matrix_publish last handed down. This is the call
 * retro_lighting_eval_surface_rgb makes for every surface in the game, so the published state and
 * the surface maths live together in one translation unit and can be exercised with no GL. */
void env_matrix_apply_published_surface(float *r, float *g, float *b);

#endif
