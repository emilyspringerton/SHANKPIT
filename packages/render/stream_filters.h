#ifndef SHANKPIT_STREAM_FILTERS_H
#define SHANKPIT_STREAM_FILTERS_H

/* stream_filters.h -- the post-processing chain, applied to the PROGRAM FEED on its way out.
 *
 * Where it sits: apps/lobby's capture block does glReadPixels(GL_RGB) into a bottom-up rgb24 buffer and
 * hands it to stream_out_push (packages/render/stream_out.h), which flips and writes it to the encoder.
 * THIS runs between those two, on the same buffer, so the stream gets the grade and the player's own
 * viewport does not -- which is what an OBS filter chain does, and what a screen-space shader here could
 * not do without also grading the HUD.
 *
 * What it is NOT: a second renderer, a second capture path, or a shader. It is 256-byte lookup tables
 * plus integer arithmetic over the bytes that already exist, in this order, each pass skipping itself
 * when it is neutral:
 *
 *   levels + gamma (LUT)  ->  saturation  ->  3x3 convolution  ->  vignette  ->  grain
 *
 * THREE LAWS this file keeps, because getting them wrong is how post stacks ruin a build:
 *
 *   1. NEUTRAL IS FREE AND BIT-IDENTICAL. Preset 0 leaves every byte exactly as it arrived and does no
 *      work at all -- filter_apply returns 0 without touching memory. A machine with no rules installed
 *      must produce the same stream it produced before this existed.
 *   2. NO HEAP, NO FRAME-TIME ALLOCATION. The convolution keeps two scanline copies on the stack (the
 *      paste's own `harder` note: a malloc per frame in the capture path is a frame-time bug). Widths
 *      above FILTER_MAX_WIDTH are refused, not truncated.
 *   3. DETERMINISM. The LUT is built once per preset change; grain is a hash of (x, y, seed), never a
 *      clock and never rand(), so the same frame grades the same way twice and a recording reproduces.
 *
 * The numbers are PARENA's (stdlib/shankpit/filter_rules.prn -> filter_rules.c, installed by
 * filter_rules_host.h). The built-in defaults below MUST equal that table; stream_filters_test asserts it.
 */
#include <math.h>
#include <string.h>

#define FILTER_LUT_SIZE   256
#define FILTER_MAX_WIDTH  2048   /* the stack scanline buffers; a wider feed is refused, never overflowed */

typedef struct {
    int (*preset_count)(void);
    int (*kernel)(int preset, int index);
    int (*divisor)(int preset);
    int (*offset)(int preset);
    int (*black)(int preset);
    int (*white)(int preset);
    int (*gamma_permille)(int preset);
    int (*saturation_permille)(int preset);
    int (*vignette_permille)(int preset);
    int (*grain_permille)(int preset);
} FilterRules;

static inline int filter_def_preset_count(void) { return 5; }

static inline int filter_def_kernel(int preset, int index) {
    if (index < 0 || index > 8) return 0;
    int edge = (index == 1 || index == 3 || index == 5 || index == 7);
    int corner = (index == 0 || index == 2 || index == 6 || index == 8);
    if (preset == 1 || preset == 4) {
        if (edge) return -1;
        return index == 4 ? 5 : 0;
    }
    if (preset == 2) {
        if (index == 4) return 4;
        if (edge) return 2;
        if (corner) return 1;
        return 0;
    }
    return index == 4 ? 1 : 0;
}

static inline int filter_def_divisor(int preset) { return preset == 2 ? 16 : 1; }
static inline int filter_def_offset(int preset) { (void)preset; return 0; }

static inline int filter_def_black(int preset) {
    return preset == 2 ? 16 : (preset == 3 ? 24 : (preset == 4 ? 8 : 0));
}
static inline int filter_def_white(int preset) {
    return preset == 2 ? 235 : (preset == 3 ? 220 : (preset == 4 ? 248 : 255));
}
static inline int filter_def_gamma(int preset) {
    return preset == 2 ? 1100 : (preset == 3 ? 1200 : (preset == 4 ? 900 : 1000));
}
static inline int filter_def_saturation(int preset) {
    return preset == 2 ? 900 : (preset == 3 ? 700 : (preset == 4 ? 1300 : 1000));
}
static inline int filter_def_vignette(int preset) {
    return preset == 1 ? 300 : (preset == 2 ? 700 : (preset == 3 ? 900 : (preset == 4 ? 500 : 0)));
}
static inline int filter_def_grain(int preset) {
    return preset == 2 ? 120 : (preset == 3 ? 250 : (preset == 4 ? 180 : 0));
}

static const FilterRules g_filter_defaults = {
    filter_def_preset_count, filter_def_kernel, filter_def_divisor, filter_def_offset,
    filter_def_black, filter_def_white, filter_def_gamma, filter_def_saturation,
    filter_def_vignette, filter_def_grain,
};
static const FilterRules *g_filter_rules = &g_filter_defaults;

static inline void filter_set_rules(const FilterRules *r) { g_filter_rules = r ? r : &g_filter_defaults; }
static inline const FilterRules *filter_rules(void) { return g_filter_rules; }

/* ---- the resolved configuration of one preset ------------------------------------------------ */
typedef struct {
    int kernel[9];
    int divisor;            /* > 0, clamped on build */
    int offset;
    int black, white;       /* 0..255 input points */
    int gamma_permille;     /* 1000 = linear */
    int saturation_permille;
    int vignette_permille;  /* 0 = none */
    int grain_permille;     /* 0 = none */
    unsigned int seed;      /* grain's identity: same seed, same frame, forever */
} FilterConfig;

static inline int filter_clamp_u8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

static inline unsigned int filter_hash(unsigned int x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}

static inline void filter_config_from_rules(FilterConfig *c, int preset, unsigned int seed) {
    const FilterRules *r = g_filter_rules;
    if (!c) return;
    memset(c, 0, sizeof *c);
    for (int i = 0; i < 9; i++) c->kernel[i] = r->kernel(preset, i);
    c->divisor = r->divisor(preset);
    if (c->divisor <= 0) c->divisor = 1;
    c->offset = r->offset(preset);
    c->black = filter_clamp_u8(r->black(preset));
    c->white = filter_clamp_u8(r->white(preset));
    c->gamma_permille = r->gamma_permille(preset);
    if (c->gamma_permille < 100) c->gamma_permille = 100;
    if (c->gamma_permille > 5000) c->gamma_permille = 5000;
    c->saturation_permille = r->saturation_permille(preset);
    c->vignette_permille = r->vignette_permille(preset);
    c->grain_permille = r->grain_permille(preset);
    c->seed = seed ? seed : 1u;
}

/* Is this configuration a no-op? Checked structurally, not by trusting a preset number: a hand-built
   config that happens to be neutral must also cost nothing. */
static inline int filter_config_neutral(const FilterConfig *c) {
    if (!c) return 1;
    if (c->divisor != 1 || c->offset != 0) return 0;
    for (int i = 0; i < 9; i++) {
        int want = (i == 4) ? 1 : 0;
        if (c->kernel[i] != want) return 0;
    }
    return c->black == 0 && c->white == 255 && c->gamma_permille == 1000 &&
           c->saturation_permille == 1000 && c->vignette_permille <= 0 && c->grain_permille <= 0;
}

/* levels -> gamma, one byte at a time, into a LUT. Built on preset change, never per pixel. */
static inline int filter_levels_gamma(int v, int black, int white, int gamma_permille) {
    if (white <= black) return v <= black ? 0 : 255;   /* degenerate levels are a threshold, never a divide by zero */
    int t = ((v - black) * 255) / (white - black);
    t = filter_clamp_u8(t);
    if (gamma_permille == 1000) return t;
    {
        float n = (float)t / 255.0f;
        float g = (float)gamma_permille / 1000.0f;
        return filter_clamp_u8((int)(powf(n, g) * 255.0f + 0.5f));
    }
}

typedef struct {
    FilterConfig cfg;
    unsigned char lut[FILTER_LUT_SIZE];   /* one table: levels+gamma are per-channel identical here */
    int neutral;
} FilterChain;

static inline void filter_chain_build(FilterChain *ch, const FilterConfig *cfg) {
    if (!ch) return;
    memset(ch, 0, sizeof *ch);
    if (cfg) ch->cfg = *cfg;
    else filter_config_from_rules(&ch->cfg, 0, 1u);
    ch->neutral = filter_config_neutral(&ch->cfg);
    for (int i = 0; i < FILTER_LUT_SIZE; i++)
        ch->lut[i] = (unsigned char)filter_levels_gamma(i, ch->cfg.black, ch->cfg.white, ch->cfg.gamma_permille);
}

/* 3x3 convolution over an rgb24 buffer, in place, borders untouched.
 * Two stack scanlines hold the rows above and at the cursor, so no pixel is read after it was written
 * and no heap is touched. `w` above FILTER_MAX_WIDTH is refused: a filter is never worth a buffer overrun. */
static inline int filter_convolve_rgb24(unsigned char *buf, int w, int h,
                                        const int kernel[9], int divisor, int offset) {
    unsigned char prev_row[FILTER_MAX_WIDTH * 3];
    unsigned char curr_row[FILTER_MAX_WIDTH * 3];
    int stride, y, x, c;

    if (!buf || w < 3 || h < 3 || w > FILTER_MAX_WIDTH) return 0;
    if (divisor == 0) divisor = 1;
    stride = w * 3;

    for (y = 1; y < h - 1; y++) {
        /* curr = the row being written; prev = the row above it, as it was BEFORE any write */
        if (y == 1) {
            memcpy(prev_row, buf + (size_t)(y - 1) * stride, (size_t)stride);
            memcpy(curr_row, buf + (size_t)y * stride, (size_t)stride);
        } else {
            memcpy(prev_row, curr_row, (size_t)stride);
            memcpy(curr_row, buf + (size_t)y * stride, (size_t)stride);
        }
        for (x = 1; x < w - 1; x++) {
            int x3 = x * 3;
            for (c = 0; c < 3; c++) {
                int sum = 0;
                sum += prev_row[(x - 1) * 3 + c] * kernel[0];
                sum += prev_row[x3 + c]           * kernel[1];
                sum += prev_row[(x + 1) * 3 + c] * kernel[2];
                sum += curr_row[(x - 1) * 3 + c] * kernel[3];
                sum += curr_row[x3 + c]           * kernel[4];
                sum += curr_row[(x + 1) * 3 + c] * kernel[5];
                /* the row below has not been written yet, so it is safe to read in place */
                sum += buf[(size_t)(y + 1) * stride + (x - 1) * 3 + c] * kernel[6];
                sum += buf[(size_t)(y + 1) * stride + x3 + c]           * kernel[7];
                sum += buf[(size_t)(y + 1) * stride + (x + 1) * 3 + c] * kernel[8];
                buf[(size_t)y * stride + x3 + c] = (unsigned char)filter_clamp_u8(sum / divisor + offset);
            }
        }
    }
    return 1;
}

/* Vignette: darken by distance from the centre, clamped so the corners are the strongest point. */
static inline void filter_apply_vignette(unsigned char *buf, int w, int h, int strength_permille) {
    int x, y, c;
    if (!buf || w < 2 || h < 2 || strength_permille <= 0) return;
    if (strength_permille > 1000) strength_permille = 1000;
    {
        float cx = (float)(w - 1) * 0.5f, cy = (float)(h - 1) * 0.5f;
        float invx = cx > 0.0f ? 1.0f / cx : 0.0f, invy = cy > 0.0f ? 1.0f / cy : 0.0f;
        for (y = 0; y < h; y++) {
            float dy = ((float)y - cy) * invy;
            for (x = 0; x < w; x++) {
                float dx = ((float)x - cx) * invx;
                float d2 = dx * dx + dy * dy;
                int scale;
                if (d2 > 1.0f) d2 = 1.0f;
                scale = 1000 - (int)((float)strength_permille * d2);
                if (scale >= 1000) continue;
                for (c = 0; c < 3; c++) {
                    int v = (int)buf[((size_t)y * w + x) * 3 + c];
                    buf[((size_t)y * w + x) * 3 + c] = (unsigned char)((v * scale) / 1000);
                }
            }
        }
    }
}

/* Grain: a deterministic dither. Same (x, y, seed) -> same offset, so a recording replays identically. */
static inline void filter_apply_grain(unsigned char *buf, int w, int h, int amount_permille, unsigned int seed) {
    int x, y, c;
    if (!buf || amount_permille <= 0) return;
    if (amount_permille > 1000) amount_permille = 1000;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            unsigned int hv = filter_hash((unsigned int)y * 73856093u ^
                                          (unsigned int)x * 19349663u ^
                                          (seed ? seed : 1u) * 83492791u);
            int n = (int)(hv % 511u) - 255;            /* -255..255 */
            int add = (n * amount_permille) / 1000;
            if (!add) continue;
            for (c = 0; c < 3; c++) {
                int at = (int)((size_t)y * w + x) * 3 + c;
                buf[at] = (unsigned char)filter_clamp_u8((int)buf[at] + add);
            }
        }
    }
}

/* Saturation around luma (77/150/29 over 256, integer): 0 = greyscale, 1000 = untouched, 2000 = double. */
static inline void filter_apply_saturation(unsigned char *buf, int w, int h, int permille) {
    int i, total;
    if (!buf || permille == 1000 || w <= 0 || h <= 0) return;
    total = w * h * 3;
    for (i = 0; i < total; i += 3) {
        int r = buf[i], g = buf[i + 1], b = buf[i + 2];
        int luma = (r * 77 + g * 150 + b * 29) >> 8;
        buf[i]     = (unsigned char)filter_clamp_u8(luma + ((r - luma) * permille) / 1000);
        buf[i + 1] = (unsigned char)filter_clamp_u8(luma + ((g - luma) * permille) / 1000);
        buf[i + 2] = (unsigned char)filter_clamp_u8(luma + ((b - luma) * permille) / 1000);
    }
}

/* The chain. Returns 1 when it changed something, 0 when the config is neutral (and then it has not
 * touched a single byte). `rgb` is rgb24, any row order -- the passes are row-local. */
static inline int filter_apply(FilterChain *ch, unsigned char *rgb, int w, int h) {
    int i, total;
    if (!ch || !rgb || w <= 0 || h <= 0) return 0;
    if (ch->neutral) return 0;
    total = w * h * 3;

    /* 1. levels + gamma, straight through the table */
    for (i = 0; i < total; i++) rgb[i] = ch->lut[rgb[i]];

    /* 2. saturation */
    filter_apply_saturation(rgb, w, h, ch->cfg.saturation_permille);

    /* 3. spatial */
    if (!(ch->cfg.divisor == 1 && ch->cfg.offset == 0 && ch->cfg.kernel[4] == 1 &&
          ch->cfg.kernel[0] == 0 && ch->cfg.kernel[1] == 0 && ch->cfg.kernel[2] == 0 &&
          ch->cfg.kernel[3] == 0 && ch->cfg.kernel[5] == 0 && ch->cfg.kernel[6] == 0 &&
          ch->cfg.kernel[7] == 0 && ch->cfg.kernel[8] == 0)) {
        filter_convolve_rgb24(rgb, w, h, ch->cfg.kernel, ch->cfg.divisor, ch->cfg.offset);
    }

    /* 4. lens, 5. film */
    filter_apply_vignette(rgb, w, h, ch->cfg.vignette_permille);
    filter_apply_grain(rgb, w, h, ch->cfg.grain_permille, ch->cfg.seed);
    return 1;
}

#endif /* SHANKPIT_STREAM_FILTERS_H */
