#include "proc_tex.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void proctex_init(void) {
    /* deterministic generators do not require runtime init */
}

static float fracf(float v) {
    return v - floorf(v);
}

static float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

static float smoothstep01(float t) {
    return t * t * (3.0f - 2.0f * t);
}

static uint32_t hash_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static float hash_2d(int x, int y, int seed) {
    uint32_t h = hash_u32((uint32_t)(x * 374761393 + y * 668265263 + seed * 362437));
    return (h & 0x00ffffffU) / 16777215.0f;
}

static float value_noise_2d(float x, float y, int seed) {
    int xi = (int)floorf(x);
    int yi = (int)floorf(y);
    float tx = smoothstep01(fracf(x));
    float ty = smoothstep01(fracf(y));

    float n00 = hash_2d(xi, yi, seed);
    float n10 = hash_2d(xi + 1, yi, seed);
    float n01 = hash_2d(xi, yi + 1, seed);
    float n11 = hash_2d(xi + 1, yi + 1, seed);

    float nx0 = lerpf(n00, n10, tx);
    float nx1 = lerpf(n01, n11, tx);
    return lerpf(nx0, nx1, ty);
}

static float fbm_2d(float x, float y, int seed) {
    float sum = 0.0f;
    float amp = 0.5f;
    float freq = 1.0f;
    for (int i = 0; i < 4; ++i) {
        sum += amp * value_noise_2d(x * freq, y * freq, seed + i * 67);
        freq *= 2.0f;
        amp *= 0.5f;
    }
    return sum;
}

int proc_tex_create(ProcTexture *t, int w, int h) {
    if (!t || w <= 0 || h <= 0) return 0;
    memset(t, 0, sizeof(*t));
    t->pixels = (unsigned char *)malloc((size_t)w * (size_t)h * 4u);
    if (!t->pixels) return 0;

    t->width = w;
    t->height = h;
    glGenTextures(1, &t->tex_id);
    if (t->tex_id == 0) {
        free(t->pixels);
        memset(t, 0, sizeof(*t));
        return 0;
    }

    glBindTexture(GL_TEXTURE_2D, t->tex_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
    return 1;
}

void proc_tex_destroy(ProcTexture *t) {
    if (!t) return;
    if (t->tex_id != 0) {
        glDeleteTextures(1, &t->tex_id);
    }
    free(t->pixels);
    memset(t, 0, sizeof(*t));
}

void proc_tex_upload(ProcTexture *t) {
    if (!t || t->tex_id == 0 || !t->pixels) return;
    glBindTexture(GL_TEXTURE_2D, t->tex_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t->width, t->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, t->pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void proctex_upload_to_gl(ProcTexture *t) {
    proc_tex_upload(t);
}

void proctex_make_noise_rgba(ProcTexture *t, int w, int h, uint32_t seed) {
    if (!t || !t->pixels || t->width != w || t->height != h) return;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t idx = ((size_t)y * (size_t)w + (size_t)x) * 4u;
            float u = (float)x / (float)w;
            float v = (float)y / (float)h;
            float grain = value_noise_2d(u * 26.0f, v * 26.0f, (int)seed);
            float scan = 0.9f + 0.1f * sinf(v * 130.0f + (float)(seed & 63U));
            float base = (0.45f + 0.55f * grain) * scan;
            if (base > 1.0f) base = 1.0f;
            unsigned char c = (unsigned char)(base * 255.0f);
            t->pixels[idx + 0] = c;
            t->pixels[idx + 1] = c;
            t->pixels[idx + 2] = c;
            t->pixels[idx + 3] = 255;
        }
    }
}

void proctex_make_glitch_marks_rgba(ProcTexture *t, int w, int h, uint32_t seed) {
    if (!t || !t->pixels || t->width != w || t->height != h) return;
    memset(t->pixels, 0, (size_t)w * (size_t)h * 4u);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float n = value_noise_2d((float)x * 0.24f, (float)y * 0.24f, (int)(seed + 911));
            float diag = fracf((float)x * 0.07f + (float)y * 0.11f + (float)(seed & 255U) * 0.001f);
            if (n > 0.82f && diag > 0.35f && diag < 0.52f) {
                size_t idx = ((size_t)y * (size_t)w + (size_t)x) * 4u;
                t->pixels[idx + 0] = 240;
                t->pixels[idx + 1] = 30;
                t->pixels[idx + 2] = 220;
                t->pixels[idx + 3] = 100;
            }
        }
    }
}

/* ---- Seamlessly-tileable noise (for real GL_REPEAT world-surface textures) ----
 *
 * value_noise_2d/fbm_2d above cut off at the texture's edges -- fine for a one-shot cosmetic
 * overlay, wrong for anything tiled across world geometry (a visible seam every repeat). These
 * wrap the integer lattice at `period` before hashing, so a texture generated with period equal
 * to its own cell count repeats with no seam. Doubling the period alongside frequency for each fbm
 * octave keeps every octave's own lattice wrapping at exactly the texture's edge, not just the
 * base one. */
static float hash_2d_wrapped(int x, int y, int period, int seed) {
    int wx = ((x % period) + period) % period;
    int wy = ((y % period) + period) % period;
    return hash_2d(wx, wy, seed);
}

static float value_noise_2d_tiled(float x, float y, int period, int seed) {
    int xi = (int)floorf(x);
    int yi = (int)floorf(y);
    float tx = smoothstep01(fracf(x));
    float ty = smoothstep01(fracf(y));

    float n00 = hash_2d_wrapped(xi, yi, period, seed);
    float n10 = hash_2d_wrapped(xi + 1, yi, period, seed);
    float n01 = hash_2d_wrapped(xi, yi + 1, period, seed);
    float n11 = hash_2d_wrapped(xi + 1, yi + 1, period, seed);

    float nx0 = lerpf(n00, n10, tx);
    float nx1 = lerpf(n01, n11, tx);
    return lerpf(nx0, nx1, ty);
}

static float fbm_2d_tiled(float x, float y, int base_period, int seed) {
    float sum = 0.0f;
    float amp = 0.5f;
    float freq = 1.0f;
    int period = base_period;
    for (int i = 0; i < 3; ++i) {
        sum += amp * value_noise_2d_tiled(x * freq, y * freq, period, seed + i * 67);
        freq *= 2.0f;
        period *= 2;
        amp *= 0.5f;
    }
    return sum;
}

/* proctex_make_ground_rgba: a chunky, banded stone/dirt tile for real floor/terrain geometry.
 * `cells` noise cells span the tile so GL_REPEAT gives visible-but-seamless variation rather than
 * one giant blurry gradient; banding (not a smooth gradient) is deliberate -- it's what reads as
 * a real texture at a distance instead of a soft shading gradient, matching the blocky, chunky
 * look of period-appropriate (Quake/Half-Life-era) tile textures rather than a modern smooth one. */
void proctex_make_ground_rgba(ProcTexture *t, int w, int h, uint32_t seed) {
    if (!t || !t->pixels || t->width != w || t->height != h) return;
    const int cells = 6;
    const int bands = 5;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t idx = ((size_t)y * (size_t)w + (size_t)x) * 4u;
            float u = ((float)x / (float)w) * (float)cells;
            float v = ((float)y / (float)h) * (float)cells;
            float n = fbm_2d_tiled(u, v, cells, (int)seed);
            float banded = floorf(n * (float)bands) / (float)bands;
            float shade = 0.55f + 0.45f * banded;
            if (shade > 1.0f) shade = 1.0f;
            /* Earthy stone/dirt tint -- distinct from the brick tint below on purpose, so a
             * floor and a wall never read as the exact same material by accident. */
            t->pixels[idx + 0] = (unsigned char)(shade * 168.0f);
            t->pixels[idx + 1] = (unsigned char)(shade * 150.0f);
            t->pixels[idx + 2] = (unsigned char)(shade * 122.0f);
            t->pixels[idx + 3] = 255;
        }
    }
}

/* proctex_make_wall_brick_rgba: a real running-bond brick pattern (standard half-brick offset on
 * alternating rows) for wall/box geometry, so walls read as a distinct built material from the
 * ground rather than sharing one generic "noisy tile" for everything. */
void proctex_make_wall_brick_rgba(ProcTexture *t, int w, int h, uint32_t seed) {
    if (!t || !t->pixels || t->width != w || t->height != h) return;
    const int cols = 8;
    const int rows = 4;
    const float mortar = 0.08f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t idx = ((size_t)y * (size_t)w + (size_t)x) * 4u;
            float u = (float)x / (float)w;
            float v = (float)y / (float)h;

            int row = (int)(v * (float)rows);
            float row_offset = (row % 2 == 0) ? 0.0f : (0.5f / (float)cols);
            float col_f = fracf(u * (float)cols + row_offset);
            float row_f = fracf(v * (float)rows);
            int is_mortar = (col_f < mortar) || (row_f < mortar);

            float brick_id_x = floorf(u * (float)cols + row_offset);
            float shade_n = hash_2d((int)brick_id_x, row, (int)seed);
            float brick_shade = 0.55f + 0.30f * shade_n;

            if (is_mortar) {
                unsigned char c = (unsigned char)(150.0f + 25.0f * shade_n);
                t->pixels[idx + 0] = c;
                t->pixels[idx + 1] = c;
                t->pixels[idx + 2] = c;
            } else {
                t->pixels[idx + 0] = (unsigned char)(brick_shade * 156.0f);
                t->pixels[idx + 1] = (unsigned char)(brick_shade * 90.0f);
                t->pixels[idx + 2] = (unsigned char)(brick_shade * 72.0f);
            }
            t->pixels[idx + 3] = 255;
        }
    }
}

/* proctex_make_concrete_rgba: mottled, low-contrast gray noise (no repeating pattern at all,
 * unlike brick's mortar grid) -- concrete reads as a flat, matte, poured surface. */
void proctex_make_concrete_rgba(ProcTexture *t, int w, int h, uint32_t seed) {
    if (!t || !t->pixels || t->width != w || t->height != h) return;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t idx = ((size_t)y * (size_t)w + (size_t)x) * 4u;
            float n = hash_2d(x / 3, y / 3, (int)seed) * 0.5f + hash_2d(x, y, (int)seed + 71) * 0.5f;
            unsigned char c = (unsigned char)(120.0f + 40.0f * n);
            t->pixels[idx + 0] = c;
            t->pixels[idx + 1] = c;
            t->pixels[idx + 2] = (unsigned char)(c * 1.02f > 255.0f ? 255 : c * 1.02f);
            t->pixels[idx + 3] = 255;
        }
    }
}

/* proctex_make_wood_rgba: horizontal plank rows with a darker seam between planks, plus a fine
 * horizontal grain-noise streak within each plank -- reads as a distinct built material from both
 * brick (vertical mortar joints) and concrete (no pattern at all). */
void proctex_make_wood_rgba(ProcTexture *t, int w, int h, uint32_t seed) {
    if (!t || !t->pixels || t->width != w || t->height != h) return;
    const int planks = 5;
    const float seam = 0.06f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t idx = ((size_t)y * (size_t)w + (size_t)x) * 4u;
            float v = (float)y / (float)h;
            int plank = (int)(v * (float)planks);
            float row_f = fracf(v * (float)planks);
            int is_seam = row_f < seam;
            float plank_shade = 0.6f + 0.25f * hash_2d(plank, 0, (int)seed);
            float grain = hash_2d(x / 2, plank, (int)seed + 13) * 0.15f;
            float shade = plank_shade + grain;
            if (is_seam) shade *= 0.45f;
            t->pixels[idx + 0] = (unsigned char)(shade * 168.0f);
            t->pixels[idx + 1] = (unsigned char)(shade * 118.0f);
            t->pixels[idx + 2] = (unsigned char)(shade * 74.0f);
            t->pixels[idx + 3] = 255;
        }
    }
}

/* proctex_make_metal_rgba: cool-gray brushed horizontal streaks -- a real base coat for the
 * "shiny/metal" material; the actual mirror-bright highlight is the specular shader pass, not
 * this texture (a flat metal texture under real specular lighting is the correct real-game look,
 * not a texture trying to fake shininess on its own). */
void proctex_make_metal_rgba(ProcTexture *t, int w, int h, uint32_t seed) {
    if (!t || !t->pixels || t->width != w || t->height != h) return;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t idx = ((size_t)y * (size_t)w + (size_t)x) * 4u;
            float streak = hash_2d(x / 6, y, (int)seed) * 0.5f + hash_2d(x, y / 2, (int)seed + 5) * 0.5f;
            unsigned char c = (unsigned char)(150.0f + 55.0f * streak);
            t->pixels[idx + 0] = (unsigned char)(c * 0.92f);
            t->pixels[idx + 1] = (unsigned char)(c * 0.95f);
            t->pixels[idx + 2] = c;
            t->pixels[idx + 3] = 255;
        }
    }
}

/* proctex_make_ips_panel_rgba -- founder real-time, 2026-09-14: "can we design a material for an
 * IPS light its going to need a special shader build it in." A real, distinct texture for the new
 * SHADER_IPS_LIGHT material (packages/render/material_shaders.h) -- deliberately NOT reusing the
 * brick fallback material_texture_for_name would otherwise give an unrecognized name, since a
 * light fixture needs to visually read as a glowing panel, not brick with a tint. A fine pixel-
 * cell grid (real IPS/LCD panel reference: individual backlit cells behind a thin dark grid,
 * unlike brick's mortar joints or wood's plank seams) on a bright, cool white-blue base -- the
 * same real glow_color tone draw_material_emissive_box's own additive pass uses, so the texture
 * and the emissive overlay read as one consistent light source, not two different colors fighting.
 */
void proctex_make_ips_panel_rgba(ProcTexture *t, int w, int h, uint32_t seed) {
    if (!t || !t->pixels || t->width != w || t->height != h) return;
    const int cell = 6; /* pixel-cell size -- fine enough to read as a panel grid, not a checkerboard */
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t idx = ((size_t)y * (size_t)w + (size_t)x) * 4u;
            int gx = x % cell, gy = y % cell;
            int on_grid_line = (gx == 0 || gy == 0);
            float cell_shimmer = 0.92f + 0.08f * hash_2d(x / cell, y / cell, (int)seed);
            float base = on_grid_line ? 0.55f : 1.0f;
            float shade = base * cell_shimmer;
            t->pixels[idx + 0] = (unsigned char)(shade * 217.0f); /* 0.85 * 255 */
            t->pixels[idx + 1] = (unsigned char)(shade * 235.0f); /* 0.92 * 255 */
            t->pixels[idx + 2] = (unsigned char)(shade * 255.0f > 255.0f ? 255.0f : shade * 255.0f);
            t->pixels[idx + 3] = 255;
        }
    }
}

void proc_tex_fill_emily_vibe(ProcTexture *t, float seed, float t_sec) {
    if (!t || !t->pixels) return;

    const float drift_x = t_sec * 0.05f;
    const float drift_y = t_sec * 0.03f;
    const int iseed = (int)(seed * 4096.0f);

    for (int y = 0; y < t->height; ++y) {
        for (int x = 0; x < t->width; ++x) {
            size_t idx = ((size_t)y * (size_t)t->width + (size_t)x) * 4u;
            float u = (float)x / (float)t->width;
            float v = (float)y / (float)t->height;

            float fog = fbm_2d(u * 2.5f + drift_x, v * 2.5f + drift_y, iseed);
            float grain = value_noise_2d(u * 28.0f + drift_x * 5.0f, v * 28.0f + drift_y * 4.0f, iseed + 1009);

            float stripe_n = value_noise_2d((u + v) * 16.0f + t_sec * 0.1f, (v - u) * 8.0f, iseed + 2203);
            float diag = fracf((u * 11.0f + v * 15.0f) + t_sec * 0.08f);
            float glitch = (stripe_n > 0.74f && diag > 0.35f && diag < 0.6f) ? 1.0f : 0.0f;

            float r = 0.05f + 0.10f * fog + 0.05f * grain;
            float g = 0.22f + 0.45f * fog + 0.08f * grain;
            float b = 0.28f + 0.52f * fog + 0.08f * grain;

            r += 0.30f * glitch;
            g += 0.02f * glitch;
            b += 0.22f * glitch;

            if (r > 1.0f) r = 1.0f;
            if (g > 1.0f) g = 1.0f;
            if (b > 1.0f) b = 1.0f;

            t->pixels[idx + 0] = (unsigned char)(r * 255.0f);
            t->pixels[idx + 1] = (unsigned char)(g * 255.0f);
            t->pixels[idx + 2] = (unsigned char)(b * 255.0f);
            t->pixels[idx + 3] = 255;
        }
    }
}
