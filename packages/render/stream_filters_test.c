/* stream_filters_test.c -- `make test-stream-filters`.
 *
 * The post chain's whole value is that it is CHEAP and PREDICTABLE, so these are the laws, not the vibes:
 *   1. neutral changes nothing at all (bit-identical, and it says so by returning 0),
 *   2. levels/gamma land on hand-computed bytes,
 *   3. the convolution kernel is OBS's own matrix and a hand-built image proves it,
 *   4. borders are never written, so a sharpened frame has no dark rim,
 *   5. vignette darkens edges and leaves the centre alone; grain is deterministic,
 *   6. the generated PARENA table equals the header's defaults for every preset and parameter.
 */
#include <stdio.h>
#include <string.h>

#include "stream_filters.h"
#include "filter_rules_host.h"

static int fails = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { printf("FAIL %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } \
} while (0)

static void fill(unsigned char *b, int n, int v) { for (int i = 0; i < n; i++) b[i] = (unsigned char)v; }

int main(void) {
    /* ---- 1. neutral is free and bit-identical ---- */
    {   unsigned char img[12] = { 0, 1, 2, 3, 250, 251, 252, 253, 17, 33, 49, 65 };
        unsigned char copy[12];
        FilterChain ch;
        FilterConfig cfg;
        memcpy(copy, img, sizeof img);
        filter_config_from_rules(&cfg, 0, 1234u);
        filter_chain_build(&ch, &cfg);
        CHECK(ch.neutral == 1, "preset 0 should be structurally neutral");
        CHECK(filter_apply(&ch, img, 2, 2) == 0, "a neutral chain reported work");
        CHECK(memcmp(img, copy, sizeof img) == 0, "a neutral chain changed bytes");
        /* a hand-built config that happens to be neutral must be free too */
        memset(&cfg, 0, sizeof cfg);
        cfg.kernel[4] = 1; cfg.divisor = 1; cfg.white = 255;
        cfg.gamma_permille = 1000; cfg.saturation_permille = 1000;
        filter_chain_build(&ch, &cfg);
        CHECK(ch.neutral == 1, "a hand-built neutral config is not recognised");
    }

    /* ---- 2. levels and gamma, hand-computed ---- */
    CHECK(filter_levels_gamma(16, 16, 235, 1000) == 0, "black point should clip to 0");
    CHECK(filter_levels_gamma(235, 16, 235, 1000) == 255, "white point should reach 255");
    CHECK(filter_levels_gamma(128, 16, 235, 1000) == 130, "128 with 16..235 = %d, want 130",
          filter_levels_gamma(128, 16, 235, 1000));
    CHECK(filter_levels_gamma(0, 16, 235, 1000) == 0, "below black clips");
    CHECK(filter_levels_gamma(255, 16, 235, 1000) == 255, "above white clips");
    CHECK(filter_levels_gamma(200, 0, 255, 1000) == 200, "linear must be identity");
    {   int g2 = filter_levels_gamma(128, 0, 255, 2000);
        int gh = filter_levels_gamma(128, 0, 255, 500);
        CHECK(g2 == 64 || g2 == 65, "gamma 2000 of 128 = %d, want 64/65", g2);
        CHECK(gh == 180 || gh == 181, "gamma 500 of 128 = %d, want ~180", gh);
        CHECK(filter_levels_gamma(0, 0, 255, 2000) == 0 && filter_levels_gamma(255, 0, 255, 2000) == 255,
              "gamma must pin both endpoints");
    }
    CHECK(filter_levels_gamma(99, 100, 100, 1000) == 0, "degenerate levels: below");
    CHECK(filter_levels_gamma(101, 100, 100, 1000) == 255, "degenerate levels: above");

    /* ---- 3. convolution -- hand-derived on a 5x4 image ---- */
    {   /* all black except ONE bright pixel at (2,1) */
        unsigned char img[5 * 4 * 3];
        int sharp[9] = { 0, -1, 0, -1, 5, -1, 0, -1, 0 };
        memset(img, 0, sizeof img);
        for (int c = 0; c < 3; c++) img[(1 * 5 + 2) * 3 + c] = 100;
        CHECK(filter_convolve_rgb24(img, 5, 4, sharp, 1, 0) == 1, "convolution refused a legal width");
        for (int c = 0; c < 3; c++) {
            /* the bright pixel itself: 5 * 100 = 500, clamped */
            CHECK(img[(1 * 5 + 2) * 3 + c] == 255, "centre = %d, want 255", img[(1 * 5 + 2) * 3 + c]);
            /* its left neighbour sees it as -1 * 100 -> clamps to 0 */
            CHECK(img[(1 * 5 + 1) * 3 + c] == 0, "neighbour = %d, want 0", img[(1 * 5 + 1) * 3 + c]);
        }
        /* the border (row 0, column 0) must be untouched: that is the "no dark rim" property */
        CHECK(img[0] == 0 && img[(0 * 5 + 2) * 3] == 0, "a border pixel was written");
    }
    {   /* a constant image survives a normalised blur unchanged */
        unsigned char img[6 * 5 * 3];
        int blur[9] = { 1, 2, 1, 2, 4, 2, 1, 2, 1 };
        fill(img, sizeof img, 50);
        CHECK(filter_convolve_rgb24(img, 6, 5, blur, 16, 0) == 1, "blur refused");
        int bad = 0;
        for (int i = 0; i < (int)sizeof img; i++) if (img[i] != 50) bad++;
        CHECK(bad == 0, "a flat image came out non-flat (%d bytes)", bad);
    }
    {   /* too wide is refused, not truncated or overrun */
        static unsigned char wide[FILTER_MAX_WIDTH * 3 + 3];
        int k[9] = { 0, 0, 0, 0, 1, 0, 0, 0, 0 };
        CHECK(filter_convolve_rgb24(wide, FILTER_MAX_WIDTH + 1, 3, k, 1, 0) == 0, "an over-wide frame was accepted");
        CHECK(filter_convolve_rgb24(wide, 2, 3, k, 1, 0) == 0, "a too-narrow frame was accepted");
    }

    /* ---- 4. saturation around luma ---- */
    {   unsigned char px[3] = { 10, 200, 60 };
        filter_apply_saturation(px, 1, 1, 0);
        int luma = (10 * 77 + 200 * 150 + 60 * 29) >> 8;
        CHECK(px[0] == luma && px[1] == luma && px[2] == luma, "saturation 0 not grey (%d %d %d, luma %d)",
              px[0], px[1], px[2], luma);
        CHECK(luma == 126, "luma of (10,200,60) = %d, want 126", luma); /* 32510/256 = 126.99 */
        {   unsigned char same[3] = { 10, 200, 60 };
            filter_apply_saturation(same, 1, 1, 1000);
            CHECK(same[0] == 10 && same[1] == 200 && same[2] == 60, "saturation 1000 changed a pixel");
        }
    }

    /* ---- 5. vignette and grain ---- */
    {   unsigned char img[33 * 33 * 3];   /* odd: (16,16) is a real centre pixel, distance exactly 0 */
        fill(img, sizeof img, 200);
        filter_apply_vignette(img, 33, 33, 1000);
        int centre = img[(16 * 33 + 16) * 3];
        int corner = img[0];
        CHECK(centre == 200, "vignette darkened the exact centre to %d", centre);
        CHECK(corner < centre, "vignette did not darken the corner (%d vs %d)", corner, centre);
        CHECK(corner >= 0 && corner <= 200, "vignette went out of range: %d", corner);
    }
    {   unsigned char a[16 * 8 * 3], b[16 * 8 * 3];
        fill(a, sizeof a, 128);
        memcpy(b, a, sizeof a);
        filter_apply_grain(a, 16, 8, 500, 77u);
        filter_apply_grain(b, 16, 8, 500, 77u);
        CHECK(memcmp(a, b, sizeof a) == 0, "grain is not deterministic for one seed");
        filter_apply_grain(b, 16, 8, 500, 78u);
        CHECK(memcmp(a, b, sizeof a) != 0, "a different seed produced identical grain");
    }

    /* ---- 6. a full preset actually changes a frame, and the PARENA table matches the defaults ---- */
    {   unsigned char img[64 * 48 * 3];
        unsigned char before[64 * 48 * 3];
        FilterChain ch;
        FilterConfig cfg;
        fill(img, sizeof img, 120);
        memcpy(before, img, sizeof img);
        filter_config_from_rules(&cfg, 3, 9u);       /* GRITTY: levels + gamma + sat + vignette + grain */
        filter_chain_build(&ch, &cfg);
        CHECK(ch.neutral == 0, "a graded preset reported neutral");
        CHECK(filter_apply(&ch, img, 64, 48) == 1, "a graded chain reported no work");
        CHECK(memcmp(img, before, sizeof img) != 0, "a graded preset left the frame untouched");
    }

    filter_rules_install();
    CHECK(filter_preset_count() == filter_def_preset_count(),
          "PARENA presets %d vs %d", filter_preset_count(), filter_def_preset_count());
    for (int p = 0; p < filter_def_preset_count(); p++) {
        for (int i = 0; i < 9; i++)
            CHECK(filter_kernel(p, i) == filter_def_kernel(p, i),
                  "PARENA kernel(%d,%d) = %d, header %d", p, i, filter_kernel(p, i), filter_def_kernel(p, i));
        CHECK(filter_divisor(p) == filter_def_divisor(p), "PARENA divisor preset %d", p);
        CHECK(filter_offset(p) == filter_def_offset(p), "PARENA offset preset %d", p);
        CHECK(filter_black(p) == filter_def_black(p), "PARENA black preset %d", p);
        CHECK(filter_white(p) == filter_def_white(p), "PARENA white preset %d", p);
        CHECK(filter_gamma(p) == filter_def_gamma(p), "PARENA gamma preset %d", p);
        CHECK(filter_saturation(p) == filter_def_saturation(p), "PARENA saturation preset %d", p);
        CHECK(filter_vignette(p) == filter_def_vignette(p), "PARENA vignette preset %d", p);
        CHECK(filter_grain(p) == filter_def_grain(p), "PARENA grain preset %d", p);
    }

    /* ---- 6. LIGHT (the player setting): dropping the kernel removes ONLY the neighbourhood pass ---- */
    {   unsigned char img[5 * 4 * 3], full[5 * 4 * 3], light[5 * 4 * 3];
        FilterConfig f;
        FilterChain ch;
        memset(&f, 0, sizeof f);
        f.divisor = 1; f.white = 255; f.gamma_permille = 1000; f.saturation_permille = 1000; f.seed = 1u;
        f.kernel[1] = f.kernel[3] = f.kernel[5] = f.kernel[7] = -1;
        f.kernel[4] = 5;
        for (int i = 0; i < (int)sizeof img; i++) img[i] = (unsigned char)((i * 37) % 256);
        memcpy(full, img, sizeof img);
        filter_chain_build(&ch, &f);
        CHECK(filter_apply(&ch, full, 5, 4) == 1, "a graded frame reported no work");
        CHECK(memcmp(full, img, sizeof img) != 0, "the sharpen kernel changed nothing on a contrast image");
        /* same config, kernel dropped to a centre pass: the colour grade stays, the neighbours are not read */
        f.kernel[1] = f.kernel[3] = f.kernel[5] = f.kernel[7] = 0;
        f.kernel[4] = 1;
        memcpy(light, img, sizeof img);
        filter_chain_build(&ch, &f);
        filter_apply(&ch, light, 5, 4);
        CHECK(memcmp(light, img, sizeof img) == 0, "LIGHT still changed pixels with a neutral LUT");
    }

    if (fails) { printf("stream_filters: %d FAILED\n", fails); return 1; }
    printf("stream_filters: all checks passed\n");
    return 0;
}
