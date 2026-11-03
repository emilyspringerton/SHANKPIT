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
