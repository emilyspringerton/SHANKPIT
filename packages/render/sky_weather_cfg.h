/* sky_weather_cfg.h -- the configurable half of SHANKPIT's weather-aware skybox: palettes,
 * celestial bodies, clouds, and one profile per weather. Ported in from BIG_O
 * (day/packages/common/bigo_skycfg.h) as part of the real engine merge -- BIG_O's tech becomes
 * first-class SHANKPIT tech, not a separate app. Renamed Bigo-prefixed types and bsc_ helpers to
 * SkyWeather/skw_ to match this repo's own naming, logic unchanged.
 *
 * Pure C (no GL) so it is unit-tested and loadable on any host. sky_weather.h renders from a
 * SkyWeatherConfig, and the weather (0 CLEAR 1 OVERCAST 2 RAIN 3 STORM) selects a profile, so the
 * sky changes with the weather and a mod maker can restyle everything by editing a text file (see
 * assets/skybox/default.cfg for every key).
 *
 * File format: one `key v1 [v2 v3]` per line; `#` starts a comment; keys are case-insensitive.
 * Unknown keys and bad values are errors (reported with the line number), never silently ignored.
 * Colours are RGB 0..1. */
#ifndef SKY_WEATHER_CFG_H
#define SKY_WEATHER_CFG_H
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float zenith[3], horizon[3]; } SkwPalette;

typedef struct {
    float cover;        /* target cloud cover 0..1 */
    float rain;         /* target rain intensity 0..1 */
    float storm;        /* target storm intensity 0..1 (lightning above 0.6) */
    float darkness;     /* 0..1: how much the sky and light are dimmed */
    float grey;         /* 0..1: how much colour is drained from the sky toward overcast grey */
    float fog;          /* extra fog density added on top of the base */
    float cloud_tint[3];/* multiplier on cloud colour */
    float grey_tone[3]; /* the grey the sky drains toward (relative to its luminance): lets a profile be sickly green, dusty red... */
} SkwWeatherProfile;

typedef struct {
    SkwPalette day, golden, dawn, twilight, night;
    float sun_size, moon_size;          /* disc radius in dome units (default 3.8 / 3.4) */
    float sun_tilt;                     /* z component of the sun path: how far it swings off the east-west line */
    float star_count, star_brightness;  /* <= SKYW_STARS; brightness multiplier */
    float cloud_count, cloud_scale, cloud_speed; /* <= SKYW_CLOUDS; puff size multiplier; drift multiplier */
    float horizon_curve;                /* gradient exponent: lower = the horizon colour climbs higher */
    float fog_base;                     /* fog density with clear air */
    float smoothing;                    /* weather transition rate, 1/s (default 0.6) */
    SkwWeatherProfile weather[4];
} SkyWeatherConfig;

static inline void skw_set3(float *d, float a, float b, float c) { d[0] = a; d[1] = b; d[2] = c; }

static inline void sky_weather_cfg_defaults(SkyWeatherConfig *c) {
    memset(c, 0, sizeof(*c));
    skw_set3(c->day.zenith, 0.13f, 0.36f, 0.78f);      skw_set3(c->day.horizon, 0.60f, 0.79f, 0.95f);
    skw_set3(c->golden.zenith, 0.20f, 0.30f, 0.58f);   skw_set3(c->golden.horizon, 1.00f, 0.55f, 0.28f);
    skw_set3(c->dawn.zenith, 0.24f, 0.30f, 0.60f);     skw_set3(c->dawn.horizon, 1.00f, 0.62f, 0.50f);
    skw_set3(c->twilight.zenith, 0.06f, 0.07f, 0.24f); skw_set3(c->twilight.horizon, 0.42f, 0.24f, 0.36f);
    skw_set3(c->night.zenith, 0.008f, 0.014f, 0.055f); skw_set3(c->night.horizon, 0.03f, 0.05f, 0.13f);
    c->sun_size = 3.8f; c->moon_size = 3.4f; c->sun_tilt = 0.30f;
    c->star_count = 320; c->star_brightness = 1.0f;
    c->cloud_count = 44; c->cloud_scale = 1.0f; c->cloud_speed = 1.0f;
    c->horizon_curve = 0.50f; c->fog_base = 0.0020f; c->smoothing = 0.6f;
    static const struct { float cover, rain, storm, dark, grey, fog; float tint[3]; } W[4] = {
        { 0.24f, 0.00f, 0.00f, 0.00f, 0.06f, 0.0001f, { 1.00f, 1.00f, 1.00f } },
        { 0.88f, 0.00f, 0.00f, 0.24f, 0.80f, 0.0009f, { 0.90f, 0.92f, 0.96f } },
        { 0.97f, 0.75f, 0.15f, 0.38f, 0.86f, 0.0056f, { 0.74f, 0.77f, 0.84f } },
        { 1.00f, 1.00f, 1.00f, 0.66f, 0.85f, 0.0100f, { 0.62f, 0.62f, 0.70f } } };
    for (int i = 0; i < 4; i++) {
        SkwWeatherProfile *w = &c->weather[i];
        w->cover = W[i].cover; w->rain = W[i].rain; w->storm = W[i].storm; w->darkness = W[i].dark; w->grey = W[i].grey; w->fog = W[i].fog;
        memcpy(w->cloud_tint, W[i].tint, sizeof(w->cloud_tint));
        skw_set3(w->grey_tone, 0.98f, 1.00f, 1.08f);
    }
}

typedef struct { const char *key; float *ptr; int n; } SkwKey;

/* All settable keys in one table (also the documentation of what exists). Returns the count written to `out`. */
static inline int skw_keys(SkyWeatherConfig *c, SkwKey *out, int max) {
    int n = 0;
#define K(name, field, cnt) do { if (n < max) { out[n].key = name; out[n].ptr = (float *)&(field); out[n].n = cnt; n++; } } while (0)
    K("day.zenith", c->day.zenith, 3);           K("day.horizon", c->day.horizon, 3);
    K("golden.zenith", c->golden.zenith, 3);     K("golden.horizon", c->golden.horizon, 3);
    K("dawn.zenith", c->dawn.zenith, 3);         K("dawn.horizon", c->dawn.horizon, 3);
    K("twilight.zenith", c->twilight.zenith, 3); K("twilight.horizon", c->twilight.horizon, 3);
    K("night.zenith", c->night.zenith, 3);       K("night.horizon", c->night.horizon, 3);
    K("sun.size", c->sun_size, 1); K("moon.size", c->moon_size, 1); K("sun.tilt", c->sun_tilt, 1);
    K("stars.count", c->star_count, 1); K("stars.brightness", c->star_brightness, 1);
    K("clouds.count", c->cloud_count, 1); K("clouds.scale", c->cloud_scale, 1); K("clouds.speed", c->cloud_speed, 1);
    K("horizon.curve", c->horizon_curve, 1); K("fog.base", c->fog_base, 1); K("weather.smoothing", c->smoothing, 1);
    static const char *const WN[4] = { "clear", "overcast", "rain", "storm" };
    static char names[4][7][40];
    for (int i = 0; i < 4; i++) {
        SkwWeatherProfile *w = &c->weather[i];
        const char *f[7] = { "cover", "rain", "storm", "darkness", "grey", "fog", "cloud_tint" };
        for (int j = 0; j < 7; j++) snprintf(names[i][j], sizeof(names[i][j]), "weather.%s.%s", WN[i], f[j]);
        K(names[i][0], w->cover, 1); K(names[i][1], w->rain, 1); K(names[i][2], w->storm, 1); K(names[i][3], w->darkness, 1);
        K(names[i][4], w->grey, 1);  K(names[i][5], w->fog, 1);  K(names[i][6], w->cloud_tint, 3);
    }
    static char gt[4][40];
    for (int i = 0; i < 4; i++) { snprintf(gt[i], sizeof(gt[i]), "weather.%s.grey_tone", WN[i]); K(gt[i], c->weather[i].grey_tone, 3); }
#undef K
    return n;
}

/* Parse config text onto `c` (start from defaults, or from an earlier file: later files override). Returns the number of keys
 * applied, or -1 with a message in `err` ("line N: ..."). On error `c` may be partially updated: callers should parse into a copy. */
static inline int sky_weather_cfg_parse(SkyWeatherConfig *c, const char *text, char *err, size_t errsz) {
    SkwKey keys[160]; int nk = skw_keys(c, keys, 160), applied = 0, line = 0;
    const char *p = text;
    while (*p) {
        char buf[256]; size_t n = 0; line++;
        while (*p && *p != '\n') { if (n + 1 < sizeof(buf)) buf[n++] = *p; p++; }
        if (*p == '\n') p++;
        buf[n] = 0;
        char *hash = strchr(buf, '#'); if (hash) *hash = 0;
        char *tok = strtok(buf, " \t\r"); if (!tok) continue;
        for (char *q = tok; *q; q++) *q = (char)tolower((unsigned char)*q);
        int found = -1;
        for (int i = 0; i < nk; i++) if (!strcmp(keys[i].key, tok)) { found = i; break; }
        if (found < 0) { snprintf(err, errsz, "line %d: unknown key '%s'", line, tok); return -1; }
        for (int v = 0; v < keys[found].n; v++) {
            char *vt = strtok(NULL, " \t\r"), *end;
            if (!vt) { snprintf(err, errsz, "line %d: '%s' needs %d value(s)", line, tok, keys[found].n); return -1; }
            float f = strtof(vt, &end);
            if (*end) { snprintf(err, errsz, "line %d: '%s' bad number '%s'", line, tok, vt); return -1; }
            keys[found].ptr[v] = f;
        }
        if (strtok(NULL, " \t\r")) { snprintf(err, errsz, "line %d: '%s' has extra values", line, tok); return -1; }
        applied++;
    }
    return applied;
}

static inline void skw_clamp1(float *f, float lo, float hi) { if (*f < lo) *f = lo; else if (*f > hi) *f = hi; }

/* Clamp everything into ranges the renderer can trust (colours 0..1, counts within the renderer's arrays). */
static inline void sky_weather_cfg_sanitize(SkyWeatherConfig *c, int max_stars, int max_clouds) {
    SkwKey keys[160]; int nk = skw_keys(c, keys, 160);
    for (int i = 0; i < nk; i++) for (int v = 0; v < keys[i].n; v++) { float *f = &keys[i].ptr[v]; if (*f != *f) *f = 0; }
    float *cols[] = { c->day.zenith, c->day.horizon, c->golden.zenith, c->golden.horizon, c->dawn.zenith, c->dawn.horizon,
                      c->twilight.zenith, c->twilight.horizon, c->night.zenith, c->night.horizon };
    for (unsigned i = 0; i < sizeof(cols) / sizeof(cols[0]); i++) for (int v = 0; v < 3; v++) skw_clamp1(&cols[i][v], 0, 1);
    for (int i = 0; i < 4; i++) {
        SkwWeatherProfile *w = &c->weather[i];
        float *u[] = { &w->cover, &w->rain, &w->storm, &w->darkness, &w->grey };
        for (int k = 0; k < 5; k++) skw_clamp1(u[k], 0, 1);
        for (int v = 0; v < 3; v++) { skw_clamp1(&w->cloud_tint[v], 0, 1.5f); skw_clamp1(&w->grey_tone[v], 0, 2); }
        skw_clamp1(&w->fog, 0, 0.05f);
    }
    skw_clamp1(&c->star_count, 0, (float)max_stars);     skw_clamp1(&c->cloud_count, 0, (float)max_clouds);
    skw_clamp1(&c->sun_size, 0.5f, 12);                  skw_clamp1(&c->moon_size, 0.5f, 12);
    skw_clamp1(&c->cloud_scale, 0.2f, 3);                skw_clamp1(&c->horizon_curve, 0.15f, 2);
    skw_clamp1(&c->fog_base, 0, 0.05f);                  skw_clamp1(&c->smoothing, 0.05f, 10);
    skw_clamp1(&c->sun_tilt, -0.9f, 0.9f);               skw_clamp1(&c->star_brightness, 0, 3);
    skw_clamp1(&c->cloud_speed, 0, 20);
}

/* Load a file (parse into a copy of *c; only replace *c if the whole file is valid). Returns keys applied, or -1 with `err`. */
static inline int sky_weather_cfg_load(SkyWeatherConfig *c, const char *path, int max_stars, int max_clouds, char *err, size_t errsz) {
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errsz, "cannot open %s", path); return -1; }
    static char text[16384]; size_t n = fread(text, 1, sizeof(text) - 1, f); text[n] = 0; fclose(f);
    SkyWeatherConfig tmp = *c;
    int r = sky_weather_cfg_parse(&tmp, text, err, errsz);
    if (r < 0) return -1;
    sky_weather_cfg_sanitize(&tmp, max_stars, max_clouds);
    *c = tmp;
    return r;
}
#endif
