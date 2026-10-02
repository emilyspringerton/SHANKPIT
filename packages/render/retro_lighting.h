#ifndef RETRO_LIGHTING_H
#define RETRO_LIGHTING_H

typedef enum RetroLightingPreset {
    RETRO_LIGHTING_DYNAMIC = 0,
    RETRO_LIGHTING_DAY_STATIC,
    RETRO_LIGHTING_SUNSET_STATIC,
    RETRO_LIGHTING_NIGHT_MOONLIT,
    RETRO_LIGHTING_INTERIOR_FLAT,
} RetroLightingPreset;

typedef struct RetroLightingState {
    RetroLightingPreset preset;

    float ambient_r;
    float ambient_g;
    float ambient_b;
    float ambient_intensity;

    float fog_r;
    float fog_g;
    float fog_b;
    float fog_near;
    float fog_far;

    float sun_dir_x;
    float sun_dir_y;
    float sun_dir_z;
    float sun_r;
    float sun_g;
    float sun_b;
    float sun_intensity;

    float moon_dir_x;
    float moon_dir_y;
    float moon_dir_z;
    float moon_r;
    float moon_g;
    float moon_b;
    float moon_intensity;

    int allow_moonlight;
} RetroLightingState;

void retro_lighting_eval(float time_sec, RetroLightingPreset preset, RetroLightingState *out_state);
/* Flat fill light (0..~0.4) added to every surface's ambient term -- the pause-menu ambient slider
   (PARENA stdlib/shankpit/ambient_rules.prn maps the slider level to this). 0 = the original look. */
void retro_lighting_set_ambient_boost(float boost);
void retro_lighting_eval_surface_rgb(const RetroLightingState *state,
                                     float nx, float ny, float nz,
                                     float ambient_floor,
                                     float *out_r, float *out_g, float *out_b);
const char *retro_lighting_preset_name(RetroLightingPreset preset);

#endif
