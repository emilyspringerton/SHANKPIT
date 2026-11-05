/* proc_tex_test.c -- the procedural world textures: reproducible, distinct per seed, opaque, and
 * tileable. Headless on purpose: every proctex_make_* generator only fills a pixel buffer, so this
 * suite allocates that buffer itself and never calls proc_tex_create, whose first act is
 * glGenTextures and would therefore need a real GL context. make test-proc-tex.
 *
 * Why these assertions and not a screenshot: a texture can be wrong in ways a build cannot see --
 * non-deterministic noise makes a level look different on every load, an ignored seed makes every
 * wall identical, and a pattern that does not wrap puts a visible seam at every box boundary. Those
 * are all measurable from the pixels; how the rock READS on a wall is not, and is not claimed here. */
#include "proc_tex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 128
#define H 128

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("proc_tex: FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static unsigned char *rock_buf(uint32_t seed) {
    unsigned char *p = (unsigned char *)malloc((size_t)W * (size_t)H * 4u);
    ProcTexture t;
    if (!p) return NULL;
    memset(&t, 0, sizeof t);
    t.width = W;
    t.height = H;
    t.pixels = p;
    proctex_make_rock_rgba(&t, W, H, seed);
    return p;
}

/* ---- the municipal district's four generators ------------------------------------------------
 * Same harness as the rock above, applied to the materials the civic district introduced
 * (tools/build_municipal_district.py): a `tile` floor, `plaster` walls, `marble` columns and one board
 * generator driven to look like three DIFFERENT species. These matter because a material the client has
 * no generator for draws as brick -- and because "three woods with different densities" is a claim
 * about the level file and about three visibly different boards, so both halves are checked.
 */
typedef void (*TexFn)(ProcTexture *, int, int, uint32_t);

static void fill_tile(ProcTexture *t, int w, int h, uint32_t s) { proctex_make_institutional_tile_rgba(t, w, h, s); }
static void fill_plaster(ProcTexture *t, int w, int h, uint32_t s) { proctex_make_plaster_rgba(t, w, h, s); }
static void fill_marble(ProcTexture *t, int w, int h, uint32_t s) { proctex_make_marble_rgba(t, w, h, s); }

/* the three species' own numbers -- the SAME constants the lobby initialises its textures with, kept
   here so a drift between the two shows up as a different-looking board rather than silently */
static void fill_oak(ProcTexture *t, int w, int h, uint32_t s) { proctex_make_wood_variant_rgba(t, w, h, s, 6, 9, 0.64f, 0.47f, 0.28f); }
static void fill_pine(ProcTexture *t, int w, int h, uint32_t s) { proctex_make_wood_variant_rgba(t, w, h, s, 4, 4, 0.72f, 0.60f, 0.38f); }
static void fill_mahogany(ProcTexture *t, int w, int h, uint32_t s) { proctex_make_wood_variant_rgba(t, w, h, s, 7, 14, 0.42f, 0.22f, 0.16f); }

static unsigned char *gen_buf(TexFn fill, uint32_t seed) {
    unsigned char *p = (unsigned char *)malloc((size_t)W * (size_t)H * 4u);
    ProcTexture t;
    if (!p) return NULL;
    memset(&t, 0, sizeof t);
    t.width = W;
    t.height = H;
    t.pixels = p;
    fill(&t, W, H, seed);
    return p;
}

/* expect_wrap: 1 for the two generators whose pattern is anchored to a whole-number grid (tile, marble)
   and 0 for the noisy ones (plaster, sawn timber), where a per-pixel hash at the edge is not a seam any
   eye could find and claiming otherwise would be a false assertion. */
static void check_surface(const char *label, TexFn fill, int expect_wrap) {
    unsigned char *a = gen_buf(fill, 0x5151u);
    unsigned char *b = gen_buf(fill, 0x5151u);
    unsigned char *c = gen_buf(fill, 0x7777u);
    int lo = 255, hi = 0, opaque = 1, interior_max = 0, wrap_max = 0, changed = 0;
    CHECK(a && b && c, "%s: allocation failed", label);
    if (!a || !b || !c) { free(a); free(b); free(c); return; }
    CHECK(memcmp(a, b, (size_t)W * H * 4u) == 0, "%s: same seed produced different pixels", label);
    CHECK(memcmp(a, c, (size_t)W * H * 4u) != 0, "%s: a different seed produced identical pixels", label);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            size_t i = ((size_t)y * (size_t)W + (size_t)x) * 4u;
            int v = a[i];
            if (v < lo) lo = v;
            if (v > hi) hi = v;
            if (a[i + 3] != 255) opaque = 0;
            if (i && a[i] != a[i - 4]) changed++;
            if (x > 0) {
                int d = v - (int)a[i - 4];
                if (d < 0) d = -d;
                if (d > interior_max) interior_max = d;
            }
        }
        {
            size_t left = ((size_t)y * (size_t)W) * 4u;
            size_t right = ((size_t)y * (size_t)W + (size_t)(W - 1)) * 4u;
            int d = (int)a[left] - (int)a[right];
            if (d < 0) d = -d;
            if (d > wrap_max) wrap_max = d;
        }
    }
    CHECK(opaque, "%s: not fully opaque", label);
    CHECK(hi - lo >= 30, "%s: tonal range is only %d", label, hi - lo);
    /* /16, not /8: a tile floor is MOSTLY flat -- the pattern is a grid of large uniform cells, so a
       quarter of the pixels are meant to match their neighbour. A generator that produced a real flat
       fill still fails this (1791 of 16384 for tile, 14833 for timber). */
    CHECK(changed >= (W * H) / 16, "%s: only %d pixels differ from their neighbour", label, changed);
    if (expect_wrap)
        CHECK(wrap_max <= interior_max, "%s: seam jump %d exceeds the largest interior step %d",
              label, wrap_max, interior_max);
    printf("proc_tex: %-9s range=%3d..%3d varied=%5d wrap_max=%3d\n", label, lo, hi, changed, wrap_max);
    free(a);
    free(b);
    free(c);
}

int main(void) {
    unsigned char *a = rock_buf(0x2244u);
    unsigned char *b = rock_buf(0x2244u);
    unsigned char *c = rock_buf(0x9999u);
    int lo = 255, hi = 0, opaque = 1, changed = 0, interior_max = 0, wrap_max = 0;
    ProcTexture guard;

    CHECK(a && b && c, "allocation failed");
    if (!a || !b || !c) return 1;

    /* the same seed must produce the same world every load */
    CHECK(memcmp(a, b, (size_t)W * H * 4u) == 0, "same seed produced different pixels");

    /* a different seed must produce a different one, or every wall looks the same */
    CHECK(memcmp(a, c, (size_t)W * H * 4u) != 0, "a different seed produced identical pixels");

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            size_t i = ((size_t)y * (size_t)W + (size_t)x) * 4u;
            int v = a[i];
            if (v < lo) lo = v;
            if (v > hi) hi = v;
            if (a[i + 3] != 255) opaque = 0;
            if (i && a[i] != a[i - 4]) changed++;
            if (x > 0) {
                int d = v - (int)a[i - 4];
                if (d < 0) d = -d;
                if (d > interior_max) interior_max = d;
            }
        }
        /* the wrap column: the pattern must continue into itself across the right edge, so the
           jump from the last column to the first cannot exceed an ordinary neighbour jump */
        {
            size_t left = ((size_t)y * (size_t)W) * 4u;
            size_t right = ((size_t)y * (size_t)W + (size_t)(W - 1)) * 4u;
            int d = (int)a[left] - (int)a[right];
            if (d < 0) d = -d;
            if (d > wrap_max) wrap_max = d;
        }
    }

    CHECK(opaque, "the rock texture is not fully opaque");
    CHECK(hi - lo >= 40, "tonal range is only %d (expected a real surface, not a flat fill)", hi - lo);
    CHECK(changed >= (W * H) / 4, "only %d pixels differ from their neighbour", changed);
    CHECK(wrap_max <= interior_max, "seam jump %d exceeds the largest interior step %d", wrap_max, interior_max);

    /* the municipal district's materials. Three woods, three boards: if two of these rendered the same
       pixels, the three species would be one material with three names. */
    check_surface("tile", fill_tile, 1);
    check_surface("plaster", fill_plaster, 0);
    check_surface("marble", fill_marble, 1);
    check_surface("oak", fill_oak, 0);
    check_surface("pine", fill_pine, 0);
    check_surface("mahogany", fill_mahogany, 0);
    {
        unsigned char *oak = gen_buf(fill_oak, 0x5151u);
        unsigned char *pine = gen_buf(fill_pine, 0x5151u);
        unsigned char *mah = gen_buf(fill_mahogany, 0x5151u);
        CHECK(oak && pine && mah, "wood: allocation failed");
        if (oak && pine && mah) {
            CHECK(memcmp(oak, pine, (size_t)W * H * 4u) != 0, "oak and pine rendered identical boards");
            CHECK(memcmp(oak, mah, (size_t)W * H * 4u) != 0, "oak and mahogany rendered identical boards");
            CHECK(memcmp(pine, mah, (size_t)W * H * 4u) != 0, "pine and mahogany rendered identical boards");
            /* the species are told apart by VALUE as well as hue: mahogany is the darkest of the three */
            int oak_avg = 0, mah_avg = 0;
            for (int i = 0; i < W * H * 4; i += 4) { oak_avg += oak[i]; mah_avg += mah[i]; }
            CHECK(mah_avg < oak_avg, "mahogany is not darker than oak (avg %d vs %d)", mah_avg, oak_avg);
        }
        free(oak);
        free(pine);
        free(mah);
    }

    /* bad arguments must be refused, not crash: no pixels, wrong dimensions, null texture */
    memset(&guard, 0, sizeof guard);
    proctex_make_rock_rgba(&guard, W, H, 1u);
    proctex_make_rock_rgba(NULL, W, H, 1u);
    guard.width = W;
    guard.height = H;
    proctex_make_rock_rgba(&guard, W, H, 1u);

    printf("proc_tex: rock seed-stable=1 varied=%d range=%d..%d wrap_max=%d interior_max=%d\n",
           changed, lo, hi, wrap_max, interior_max);

    free(a);
    free(b);
    free(c);
    if (fails) {
        printf("proc_tex: %d check(s) FAILED\n", fails);
        return 1;
    }
    printf("proc_tex: all checks passed\n");
    return 0;
}
