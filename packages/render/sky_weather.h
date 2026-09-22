#ifndef SKY_WEATHER_H
#define SKY_WEATHER_H

/* sky_weather.h -- procedural sky + weather rendering. Ported in from BIG_O
 * (day/packages/common/bigo_sky.h) as part of the real engine merge: BIG_O's tech becomes
 * first-class SHANKPIT tech, not a separate app. Upgrades the old retro_sky.c (fixed fast orbit,
 * no weather, no config) with a real day/night clock + 4-state weather system (clear/overcast/
 * rain/storm), driving both the dome/cloud/precip visuals here and (via sky_weather_light_state,
 * see retro_lighting.c) the scene's ambient/sun/moon/fog lighting.
 *
 * GL 1.x immediate mode, no extra assets beyond a config file (assets/skybox/default.cfg) --
 * matches this repo's own "no shader/VBO pipeline pulled in" convention for apps/lobby's client.
 * Split into a real .c/.h pair (retro_sky.c's own convention) rather than kept header-only-inline
 * the way BIG_O had it, so it compiles as one real translation unit like every other file in this package.
 *
 * Driven by two numbers from the day/night clock: minute-of-day (0..1440, fractional ok) and
 * weather (0 CLEAR 1 OVERCAST 2 RAIN 3 STORM). Typical frame:
 *   sky_weather_update(&sky, minute, weather, now_ms);
 *   glClearColor(sky.clear[0], sky.clear[1], sky.clear[2], 1); glClear(...);
 *   ... set projection + camera view (gluLookAt) ...
 *   sky_weather_draw(&sky);                          // dome, sun/moon, stars, clouds
 *   sky_weather_fog_on(&sky);  ...draw the world...  sky_weather_fog_off();
 *   sky_weather_draw_precip(&sky, cam_x, cam_y, cam_z, now_ms);  // rain streaks + lightning bolt
 *   sky_weather_draw_grade(&sky, win_w, win_h);       // screen-space night tint + lightning flash
 * Sun path: rises east 06:00, zenith 12:00, sets west 18:00 (slightly tilted south); moon opposite. */

#include <SDL2/SDL_opengl.h>

#include "sky_weather_cfg.h"

#define SKYW_STARS 320
#define SKYW_CLOUDS 44
#define SKYW_RAIN 700

typedef struct {
    int ready;
    SkyWeatherConfig cfg;
    float dark, grey, fogadd, tint[3], grey_tone[3];   /* smoothed profile values (eased toward cfg.weather[w]) */
    GLuint glow_tex, cloud_tex;
    float star[SKYW_STARS][4];     /* x y z dir, brightness */
    float cloud[SKYW_CLOUDS][4];   /* azimuth, elevation, size, appear-threshold */
    float cover, rain, storm;      /* smoothed 0..1 weather intensities */
    float minute;                  /* current minute-of-day */
    int weather;
    unsigned int last_ms;
    float flash, flash_dx, flash_dz;
    unsigned int next_bolt_ms, bolt_seed;
    float bolt_age;
    /* derived each update */
    float sun[3], moon[3], sun_e;  /* sun direction, moon direction, sun elevation (-1..1) */
    float zen[3], hor[3], glow[3], glow_amt, light[3], clear[3], fog[3], fog_density, night;
} SkyWeather;

void sky_weather_init(SkyWeather *s);

/* Swap in a config (already sanitized or from sky_weather_cfg_defaults). The sky eases to the new
 * look rather than popping. */
void sky_weather_set_config(SkyWeather *s, const SkyWeatherConfig *c);
/* Load a skybox file on top of the current config. Returns keys applied, or -1 with `err` (config
 * left unchanged). */
int sky_weather_load_config(SkyWeather *s, const char *path, char *err, size_t errsz);

void sky_weather_update(SkyWeather *s, float minute, int weather, unsigned int now_ms);
void sky_weather_draw(SkyWeather *s);

void sky_weather_fog_on(const SkyWeather *s);
void sky_weather_fog_off(void);

/* Rain streaks around the camera (world space) and the storm's lightning bolt. Call after the
 * world, before the HUD. */
void sky_weather_draw_precip(SkyWeather *s, float cx, float cy, float cz, unsigned int now_ms);

/* Screen-space grading: blue night tint, storm gloom, lightning flash. Draws in ortho over the
 * scene; HUD comes after. */
void sky_weather_draw_grade(const SkyWeather *s, int win_w, int win_h);

#endif
