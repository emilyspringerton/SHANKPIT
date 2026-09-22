#include "sky_weather.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define SKYW_PI 3.14159265f
#define SKYW_R 90.0f

static inline float sw_clamp(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline float sw_mix(float a, float b, float t) { return a + (b - a) * t; }
static inline float sw_smooth(float a, float b, float x) { float t = sw_clamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
static inline void sw_mix3(float *o, const float *a, const float *b, float t) { for (int i = 0; i < 3; i++) o[i] = sw_mix(a[i], b[i], t); }
static inline unsigned int sw_rng(unsigned int *s) { *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s; }
static inline float sw_rf(unsigned int *s) { return (float)(sw_rng(s) & 0xFFFFFF) / 16777216.0f; }
static inline float sw_hash2(int x, int y) {
    unsigned int h = (unsigned int)x * 374761393u + (unsigned int)y * 668265263u; h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}
static inline float sw_vnoise(float x, float y) {
    int xi = (int)floorf(x), yi = (int)floorf(y); float fx = x - (float)xi, fy = y - (float)yi;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    return sw_mix(sw_mix(sw_hash2(xi, yi), sw_hash2(xi + 1, yi), fx), sw_mix(sw_hash2(xi, yi + 1), sw_hash2(xi + 1, yi + 1), fx), fy);
}

void sky_weather_init(SkyWeather *s) {
    memset(s, 0, sizeof(*s));
    sky_weather_cfg_defaults(&s->cfg);
    { const SkwWeatherProfile *w = &s->cfg.weather[0]; s->dark = w->darkness; s->grey = w->grey; s->fogadd = w->fog; memcpy(s->tint, w->cloud_tint, 12); memcpy(s->grey_tone, w->grey_tone, 12); }
    unsigned int r = 0xB16B00B5u;
    for (int i = 0; i < SKYW_STARS; i++) {
        float y = 0.02f + sw_rf(&r) * 0.98f, a = sw_rf(&r) * 2 * SKYW_PI, rr = sqrtf(1 - y * y);
        s->star[i][0] = rr * cosf(a); s->star[i][1] = y; s->star[i][2] = rr * sinf(a);
        float b = sw_rf(&r); s->star[i][3] = 0.35f + 0.65f * b * b;
    }
    for (int i = 0; i < SKYW_CLOUDS; i++) {
        s->cloud[i][0] = sw_rf(&r) * 2 * SKYW_PI;
        s->cloud[i][1] = (5.0f + 78.0f * powf(sw_rf(&r), 1.05f)) * SKYW_PI / 180.0f;
        s->cloud[i][2] = 0.18f + 0.28f * sw_rf(&r) + 0.10f * (1.0f - s->cloud[i][1] / 1.4f);
        s->cloud[i][3] = (float)i / (float)SKYW_CLOUDS;     /* low index = appears first (clear-weather wisps) */
    }
    /* glow: radial gaussian; cloud: fBm puff with soft edge and a lit top */
    enum { G = 64, C = 128 };
    static unsigned char gt[G * G * 4], ct[C * C * 4];
    for (int y = 0; y < G; y++) for (int x = 0; x < G; x++) {
        float dx = (x - G / 2 + 0.5f) / (G / 2), dy = (y - G / 2 + 0.5f) / (G / 2), d2 = dx * dx + dy * dy;
        float a = expf(-d2 * 4.5f) * (d2 < 1 ? 1 : 0);
        unsigned char *p = &gt[(y * G + x) * 4]; p[0] = p[1] = p[2] = 255; p[3] = (unsigned char)(a * 255);
    }
    for (int y = 0; y < C; y++) for (int x = 0; x < C; x++) {
        float u = (x + 0.5f) / C * 2 - 1, v = (y + 0.5f) / C * 2 - 1;
        float d = sqrtf(u * u * 0.8f + v * v * 1.7f);                 /* wide, flat puffs */
        float n = 0, amp = 0.55f, f = 3.0f;
        for (int o = 0; o < 5; o++) { n += amp * sw_vnoise(u * f + 11.0f * (float)o, v * f + 5.0f * (float)o); amp *= 0.5f; f *= 2.0f; }
        float a = sw_smooth(0.20f, 0.62f, n * (1.15f - d) + (1.0f - d) * 0.45f - 0.22f);
        float shade = sw_clamp(0.72f + 0.28f * (-v * 0.8f + n * 0.6f), 0.55f, 1.0f);   /* v<0 is the top of the cloud */
        unsigned char *p = &ct[(y * C + x) * 4];
        p[0] = p[1] = p[2] = (unsigned char)(shade * 255); p[3] = (unsigned char)(a * 255);
    }
    glGenTextures(1, &s->glow_tex); glBindTexture(GL_TEXTURE_2D, s->glow_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, G, G, 0, GL_RGBA, GL_UNSIGNED_BYTE, gt);
    glGenTextures(1, &s->cloud_tex); glBindTexture(GL_TEXTURE_2D, s->cloud_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, C, C, 0, GL_RGBA, GL_UNSIGNED_BYTE, ct);
    s->bolt_seed = 0x1234567u; s->cover = 0.24f; s->minute = 720; s->ready = 1;
}

void sky_weather_set_config(SkyWeather *s, const SkyWeatherConfig *c) { s->cfg = *c; sky_weather_cfg_sanitize(&s->cfg, SKYW_STARS, SKYW_CLOUDS); }

int sky_weather_load_config(SkyWeather *s, const char *path, char *err, size_t errsz) {
    SkyWeatherConfig c = s->cfg; int r = sky_weather_cfg_load(&c, path, SKYW_STARS, SKYW_CLOUDS, err, errsz);
    if (r >= 0) s->cfg = c;
    return r;
}

void sky_weather_update(SkyWeather *s, float minute, int weather, unsigned int now_ms) {
    float dt = s->last_ms ? sw_clamp((float)(now_ms - s->last_ms) / 1000.0f, 0, 0.25f) : 1.0f;
    s->last_ms = now_ms; s->minute = minute; s->weather = weather;
    const SkyWeatherConfig *cf = &s->cfg;
    int w = weather < 0 ? 0 : (weather > 3 ? 3 : weather);
    const SkwWeatherProfile *wp = &cf->weather[w];
    float k = 1.0f - expf(-cf->smoothing * dt);
    s->cover += (wp->cover - s->cover) * k; s->rain += (wp->rain - s->rain) * k; s->storm += (wp->storm - s->storm) * k;
    s->dark += (wp->darkness - s->dark) * k; s->grey += (wp->grey - s->grey) * k; s->fogadd += (wp->fog - s->fogadd) * k;
    for (int i = 0; i < 3; i++) { s->tint[i] += (wp->cloud_tint[i] - s->tint[i]) * k; s->grey_tone[i] += (wp->grey_tone[i] - s->grey_tone[i]) * k; }

    float a = 2 * SKYW_PI * (minute - 360.0f) / 1440.0f;
    float sx = cosf(a), sy = sinf(a), sz = cf->sun_tilt;
    float n = sqrtf(sx * sx + sy * sy + sz * sz);
    s->sun[0] = sx / n; s->sun[1] = sy / n; s->sun[2] = sz / n;
    s->moon[0] = -s->sun[0]; s->moon[1] = -s->sun[1]; s->moon[2] = s->sun[2] * 0.6f + 0.2f;
    { float m = sqrtf(s->moon[0] * s->moon[0] + s->moon[1] * s->moon[1] + s->moon[2] * s->moon[2]); for (int i = 0; i < 3; i++) s->moon[i] /= m; }
    float e = s->sun[1]; s->sun_e = e;

    /* palettes keyed on sun elevation: full day / golden hour / twilight / night */
    const float *zen_day = cf->day.zenith, *hor_day = cf->day.horizon, *zen_tw = cf->twilight.zenith, *hor_tw = cf->twilight.horizon;
    const float *zen_night = cf->night.zenith, *hor_night = cf->night.horizon;
    int morning = minute < 720.0f;
    const float *zg = morning ? cf->dawn.zenith : cf->golden.zenith, *hg = morning ? cf->dawn.horizon : cf->golden.horizon;
    float zd[3], hd[3];
    if (e > 0.30f) { memcpy(zd, zen_day, 12); memcpy(hd, hor_day, 12); }
    else if (e > 0.0f) { float t = e / 0.30f; sw_mix3(zd, zg, zen_day, t); sw_mix3(hd, hg, hor_day, t); }
    else if (e > -0.18f) { float t = (e + 0.18f) / 0.18f; sw_mix3(zd, zen_tw, zg, t); sw_mix3(hd, hor_tw, hg, t); }
    else { float t = sw_clamp((e + 0.42f) / 0.24f, 0, 1); sw_mix3(zd, zen_night, zen_tw, t * 0.35f); sw_mix3(hd, hor_night, hor_tw, t * 0.35f); }

    /* weather: cloud cover greys and darkens the whole sky, storms go bruise-dark */
    float lum_h = 0.3f * hd[0] + 0.59f * hd[1] + 0.11f * hd[2], lum_z = 0.3f * zd[0] + 0.59f * zd[1] + 0.11f * zd[2];
    float dark = 1.0f - s->dark;
    float grey_h[3] = { lum_h * s->grey_tone[0], lum_h * s->grey_tone[1], lum_h * s->grey_tone[2] };
    float grey_z[3] = { lum_z * s->grey_tone[0] * 0.87f, lum_z * s->grey_tone[1] * 0.92f, lum_z * s->grey_tone[2] * 0.97f };
    float gm = sw_clamp(s->grey, 0, 0.95f);
    for (int i = 0; i < 3; i++) {
        s->hor[i] = sw_mix(hd[i], grey_h[i], gm) * dark;
        s->zen[i] = sw_mix(zd[i], grey_z[i], gm) * sw_clamp(dark - 0.06f * s->storm, 0, 1);
    }
    s->night = sw_clamp(-e * 2.6f + 0.15f, 0, 1);

    /* horizon glow around the sun at sunrise/sunset, hidden by heavy cloud */
    s->glow_amt = sw_smooth(-0.22f, 0.02f, e) * (1.0f - sw_smooth(0.05f, 0.42f, e)) * (1.0f - 0.85f * s->cover);
    s->glow[0] = 1.0f; s->glow[1] = morning ? 0.52f : 0.42f; s->glow[2] = morning ? 0.36f : 0.20f;
    /* sunlight tint used for clouds */
    float warm = 1.0f - sw_smooth(0.05f, 0.45f, e);
    s->light[0] = 1.0f; s->light[1] = sw_mix(1.0f, 0.62f, warm); s->light[2] = sw_mix(1.0f, 0.42f, warm);
    float lit = sw_clamp(0.18f + 1.0f * sw_smooth(-0.25f, 0.30f, e), 0.10f, 1.0f) * (1.0f - 0.55f * s->cover) + 0.10f;
    for (int i = 0; i < 3; i++) s->light[i] *= lit * s->tint[i];
    { float ni[3] = { 0.10f, 0.13f, 0.26f }; float nt = s->night; for (int i = 0; i < 3; i++) s->light[i] = sw_mix(s->light[i], ni[i] * (1.0f - 0.4f * s->cover), nt * 0.8f); }

    for (int i = 0; i < 3; i++) { s->fog[i] = s->hor[i]; s->clear[i] = s->hor[i]; }
    s->fog_density = cf->fog_base + 0.0016f * s->night + s->fogadd;

    /* lightning: storms only. Deterministic-ish schedule off the frame clock. */
    if (s->flash > 0) s->flash = sw_clamp(s->flash - dt * 3.2f, 0, 1);
    if (s->bolt_age >= 0) s->bolt_age += dt;
    if (s->storm > 0.6f) {
        if (s->next_bolt_ms == 0) s->next_bolt_ms = now_ms + 2500;
        if (now_ms >= s->next_bolt_ms) {
            s->flash = 1.0f; s->bolt_age = 0;
            float ang = sw_rf(&s->bolt_seed) * 2 * SKYW_PI; s->flash_dx = cosf(ang); s->flash_dz = sinf(ang);
            s->next_bolt_ms = now_ms + 3500 + (unsigned)(sw_rf(&s->bolt_seed) * 9000);
        }
    } else { s->next_bolt_ms = 0; }
}

/* Strip the camera translation from the current modelview so the sky sits at infinity but still rotates with the view. */
static void sw_push_sky_matrix(void) {
    float m[16]; glGetFloatv(GL_MODELVIEW_MATRIX, m);
    m[12] = m[13] = m[14] = 0; glPushMatrix(); glLoadMatrixf(m);
}

void sky_weather_draw(SkyWeather *s) {
    if (!s->ready) return;
    glMatrixMode(GL_MODELVIEW); sw_push_sky_matrix();
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_FOG); glDisable(GL_LIGHTING); glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* --- dome: vertex-coloured gradient + sun-side glow; extends below the horizon so the seam never shows --- */
    const int R = 28, S = 48;
    for (int i = 0; i < R; i++) {
        float e0 = -0.35f + (SKYW_PI / 2 + 0.35f) * (float)i / R, e1 = -0.35f + (SKYW_PI / 2 + 0.35f) * (float)(i + 1) / R;
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= S; j++) {
            float az = 2 * SKYW_PI * (float)j / S;
            for (int k = 0; k < 2; k++) {
                float el = k ? e1 : e0, y = sinf(el), rr = cosf(el);
                float d[3] = { rr * cosf(az), y, rr * sinf(az) };
                float h = sw_clamp(y, 0, 1);
                float t = powf(h, s->cfg.horizon_curve);                                  /* fast rise off the horizon, long gentle zenith */
                float c[3]; sw_mix3(c, s->hor, s->zen, t);
                float dot = d[0] * s->sun[0] + d[1] * s->sun[1] + d[2] * s->sun[2];
                float g = powf(sw_clamp(dot, 0, 1), 5.0f) * s->glow_amt * (1.0f - 0.6f * t) + powf(sw_clamp(dot, 0, 1), 60.0f) * s->glow_amt * 0.5f;
                for (int q = 0; q < 3; q++) c[q] = sw_clamp(c[q] + s->glow[q] * g * 0.85f, 0, 1);
                if (y < 0) for (int q = 0; q < 3; q++) c[q] = sw_mix(c[q], s->hor[q] * 0.75f, sw_clamp(-y * 2.5f, 0, 1));  /* ground-side haze */
                glColor3f(c[0], c[1], c[2]); glVertex3f(d[0] * SKYW_R, d[1] * SKYW_R, d[2] * SKYW_R);
            }
        }
        glEnd();
    }

    /* --- stars: fade in with darkness, hidden by cloud, twinkle --- */
    float star_a = sw_clamp(s->night * 1.25f - 0.10f, 0, 1) * (1.0f - sw_clamp(s->cover * 1.3f, 0, 1)) * s->cfg.star_brightness;
    int nstars = (int)s->cfg.star_count;
    if (star_a > 0.01f) {
        glEnable(GL_POINT_SMOOTH);
        for (int pass = 0; pass < 2; pass++) {
            glPointSize(pass ? 2.6f : 1.4f);
            glBegin(GL_POINTS);
            for (int i = 0; i < nstars; i++) {
                int big = s->star[i][3] > 0.78f; if (big != pass) continue;
                float tw = 0.75f + 0.25f * sinf((float)s->last_ms * 0.003f * (0.5f + s->star[i][3]) + (float)i * 1.7f);
                float b = s->star[i][3] * tw * star_a;
                float warm = (i % 5 == 0) ? 0.85f : 1.0f;
                glColor4f(0.85f * warm + 0.1f, 0.9f, warm < 1 ? 0.75f : 1.0f, b);
                glVertex3f(s->star[i][0] * SKYW_R * 0.98f, s->star[i][1] * SKYW_R * 0.98f, s->star[i][2] * SKYW_R * 0.98f);
            }
            glEnd();
        }
    }

    /* --- sun and moon: additive halo + a crisp disc --- */
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, s->glow_tex);
    float up[3] = { 0, 1, 0 };
    for (int body = 0; body < 2; body++) {
        const float *d = body ? s->moon : s->sun;
        float vis = body ? sw_smooth(-0.05f, -0.25f, s->sun_e) : sw_smooth(-0.14f, 0.02f, s->sun_e);
        vis *= 1.0f - 0.92f * sw_clamp(s->cover * 1.05f, 0, 1);
        if (vis < 0.01f || d[1] < -0.12f) continue;
        float right[3] = { up[1] * d[2] - up[2] * d[1], up[2] * d[0] - up[0] * d[2], up[0] * d[1] - up[1] * d[0] };
        float rl = sqrtf(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]); for (int q = 0; q < 3; q++) right[q] /= rl;
        float u2[3] = { d[1] * right[2] - d[2] * right[1], d[2] * right[0] - d[0] * right[2], d[0] * right[1] - d[1] * right[0] };
        float sz_disc = body ? s->cfg.moon_size : s->cfg.sun_size, sz_halo = sz_disc * (body ? 3.8f : 6.8f);
        float c0 = body ? 0.62f : 1.0f, c1 = body ? 0.72f : (0.82f + 0.15f * sw_smooth(0.05f, 0.4f, s->sun_e)), c2 = body ? 1.0f : (0.55f + 0.4f * sw_smooth(0.0f, 0.5f, s->sun_e));
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        for (int layer = 0; layer < 2; layer++) {
            float sz = layer ? sz_halo * 0.42f : sz_halo, al = (layer ? 0.55f : 0.42f) * vis;
            glColor4f(c0, c1, c2, al);
            glBegin(GL_QUADS);
            for (int q = 0; q < 4; q++) {
                float sx = (q == 0 || q == 3) ? -1.f : 1.f, sy = (q < 2) ? -1.f : 1.f;
                glTexCoord2f(sx * 0.5f + 0.5f, sy * 0.5f + 0.5f);
                glVertex3f(d[0] * SKYW_R * 0.95f + (right[0] * sx + u2[0] * sy) * sz, d[1] * SKYW_R * 0.95f + (right[1] * sx + u2[1] * sy) * sz, d[2] * SKYW_R * 0.95f + (right[2] * sx + u2[2] * sy) * sz);
            }
            glEnd();
        }
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_TEXTURE_2D);
        glColor4f(body ? 0.93f : 1.0f, body ? 0.95f : 0.96f, body ? 1.0f : 0.86f, vis);
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(d[0] * SKYW_R * 0.94f, d[1] * SKYW_R * 0.94f, d[2] * SKYW_R * 0.94f);
        for (int q = 0; q <= 24; q++) {
            float a2 = 2 * SKYW_PI * (float)q / 24;
            glVertex3f(d[0] * SKYW_R * 0.94f + (right[0] * cosf(a2) + u2[0] * sinf(a2)) * sz_disc, d[1] * SKYW_R * 0.94f + (right[1] * cosf(a2) + u2[1] * sinf(a2)) * sz_disc, d[2] * SKYW_R * 0.94f + (right[2] * cosf(a2) + u2[2] * sinf(a2)) * sz_disc);
        }
        glEnd();
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, s->glow_tex);
    }

    /* --- clouds: textured puffs on the dome, drifting; cover picks how many are shown and how solid --- */
    glBindTexture(GL_TEXTURE_2D, s->cloud_tex);
    float drift = (float)s->last_ms * 0.000004f * s->cfg.cloud_speed;
    int nclouds = (int)s->cfg.cloud_count;
    float thick = sw_clamp(0.30f + s->cover * 0.75f, 0, 1);
    for (int i = 0; i < nclouds; i++) {
        float thr = (float)i / (float)(nclouds > 0 ? nclouds : 1);
        float appear = sw_clamp((s->cover * 1.15f - thr * 0.95f) * 4.0f, 0, 1);
        if (appear <= 0.01f) continue;
        float az = s->cloud[i][0] + drift * (1.0f + 0.5f * (float)(i % 3)), el = s->cloud[i][1] * (1.0f - 0.10f * s->cover) + 0.03f * s->cover;
        float d[3] = { cosf(el) * cosf(az), sinf(el), cosf(el) * sinf(az) };
        float right[3] = { up[1] * d[2] - up[2] * d[1], up[2] * d[0] - up[0] * d[2], up[0] * d[1] - up[1] * d[0] };
        float rl = sqrtf(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]); for (int q = 0; q < 3; q++) right[q] /= rl;
        float u2[3] = { d[1] * right[2] - d[2] * right[1], d[2] * right[0] - d[0] * right[2], d[0] * right[1] - d[1] * right[0] };
        float sz = s->cloud[i][2] * s->cfg.cloud_scale * SKYW_R * (0.85f + 1.5f * s->cover * s->cover), sy = sz * (0.50f + 0.34f * s->cover);
        float sundot = sw_clamp(d[0] * s->sun[0] + d[1] * s->sun[1] + d[2] * s->sun[2], 0, 1);
        float rim = powf(sundot, 6.0f) * (1.0f - sw_smooth(0.2f, 0.5f, s->sun_e)) * (1.0f - s->cover * 0.6f);
        float cr = s->light[0] + rim * 0.55f, cg = s->light[1] + rim * 0.25f, cb = s->light[2] + rim * 0.05f;
        float haze = sw_clamp(1.0f - el * 2.2f, 0, 1) * 0.35f;   /* low clouds melt into the horizon colour */
        cr = sw_mix(cr, s->hor[0], haze); cg = sw_mix(cg, s->hor[1], haze); cb = sw_mix(cb, s->hor[2], haze);
        float flip = (i & 1) ? -1.f : 1.f;
        glColor4f(sw_clamp(cr, 0, 1), sw_clamp(cg, 0, 1), sw_clamp(cb, 0, 1), appear * thick * (0.55f + 0.4f * s->cover));
        glBegin(GL_QUADS);
        for (int q = 0; q < 4; q++) {
            float qx = (q == 0 || q == 3) ? -1.f : 1.f, qy = (q < 2) ? -1.f : 1.f;
            glTexCoord2f(qx * 0.5f * flip + 0.5f, qy * 0.5f + 0.5f);
            glVertex3f(d[0] * SKYW_R * 0.9f + right[0] * qx * sz + u2[0] * qy * sy, d[1] * SKYW_R * 0.9f + right[1] * qx * sz + u2[1] * qy * sy, d[2] * SKYW_R * 0.9f + right[2] * qx * sz + u2[2] * qy * sy);
        }
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST);
    glPopMatrix();
}

void sky_weather_fog_on(const SkyWeather *s) {
    float c[4] = { s->fog[0], s->fog[1], s->fog[2], 1 };
    glEnable(GL_FOG); glFogi(GL_FOG_MODE, GL_EXP2); glFogfv(GL_FOG_COLOR, c); glFogf(GL_FOG_DENSITY, s->fog_density);
}
void sky_weather_fog_off(void) { glDisable(GL_FOG); }

void sky_weather_draw_precip(SkyWeather *s, float cx, float cy, float cz, unsigned int now_ms) {
    if (!s->ready) return;
    glDisable(GL_TEXTURE_2D); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (s->rain > 0.03f) {
        int n = (int)(SKYW_RAIN * s->rain);
        float t = (float)now_ms * 0.001f, wind = 0.18f + 0.30f * s->storm, speed = 22.0f + 10.0f * s->storm;
        glLineWidth(1.0f);
        glBegin(GL_LINES);
        for (int i = 0; i < n; i++) {
            float hx = sw_hash2(i, 1), hz = sw_hash2(i, 2), hp = sw_hash2(i, 3), hl = sw_hash2(i, 4);
            float x = (hx - 0.5f) * 30.0f, z = (hz - 0.5f) * 30.0f;
            float fall = fmodf(hp * 24.0f + t * speed, 24.0f);          /* 0..24 */
            float y = 12.0f - fall;
            float len = 0.7f + 0.9f * hl;
            glColor4f(0.72f, 0.80f, 0.92f, 0.10f + 0.22f * s->rain);
            glVertex3f(cx + x + wind * y * 0.0f, cy + y, cz + z);
            glVertex3f(cx + x + wind * len, cy + y + len * 1.6f, cz + z);
        }
        glEnd();
    }
    /* bolt: a jagged polyline from the cloud deck down toward the horizon, fading fast */
    if (s->storm > 0.6f && s->bolt_age >= 0 && s->bolt_age < 0.28f) {
        unsigned int r = (unsigned int)(s->bolt_seed * 2654435761u) | 1u;
        float fade = 1.0f - s->bolt_age / 0.28f;
        float bx = cx + s->flash_dx * 55.0f, bz = cz + s->flash_dz * 55.0f, by = cy + 46.0f;
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        for (int pass = 0; pass < 2; pass++) {
            glLineWidth(pass ? 2.2f : 7.0f);
            glColor4f(0.85f, 0.90f, 1.0f, (pass ? 1.0f : 0.55f) * fade);
            glBegin(GL_LINE_STRIP);
            unsigned int rr = r; float x = bx, y = by, z = bz;
            for (int k = 0; k < 11; k++) {
                glVertex3f(x, y, z);
                x += (sw_rf(&rr) - 0.5f) * 7.0f; z += (sw_rf(&rr) - 0.5f) * 7.0f; y -= 4.0f;
            }
            glEnd();
        }
    }
    glLineWidth(1.0f);
    glDisable(GL_BLEND);
}

void sky_weather_draw_grade(const SkyWeather *s, int win_w, int win_h) {
    float a_night = 0.48f * s->night * (1.0f - 0.35f * s->cover), a_gloom = 0.20f * s->storm + 0.08f * s->rain, a_flash = 0.55f * s->flash * s->flash;
    if (a_night < 0.005f && a_gloom < 0.005f && a_flash < 0.005f) return;
    glDisable(GL_DEPTH_TEST); glDisable(GL_FOG); glDisable(GL_TEXTURE_2D); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0, win_w, 0, win_h, -1, 1);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    struct { float r, g, b, a; } layers[3] = { { 0.02f, 0.05f, 0.16f, a_night }, { 0.04f, 0.05f, 0.08f, a_gloom }, { 0.85f, 0.90f, 1.0f, a_flash } };
    for (int i = 0; i < 3; i++) {
        if (layers[i].a < 0.005f) continue;
        glColor4f(layers[i].r, layers[i].g, layers[i].b, layers[i].a);
        glBegin(GL_QUADS); glVertex2f(0, 0); glVertex2f((float)win_w, 0); glVertex2f((float)win_w, (float)win_h); glVertex2f(0, (float)win_h); glEnd();
    }
    glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW);
    glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST);
}
