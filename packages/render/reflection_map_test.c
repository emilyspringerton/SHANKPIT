/* reflection_map_test.c -- the reflection environment and its per-material strengths, headless.
 *
 * This box has no GL context, so what is checked here is everything that is NOT a GL call: the sphere
 * map's pixels (deterministic, sky above ground, a hot spot for a highlight to travel across, every
 * pixel opaque) and the PARENA strengths that decide how mirror-like a material is. The GL plumbing in
 * reflection.h is compiled by `make check-windows` (the mingw lobby build includes it through
 * material_shaders.h) and is NOT run here -- stated rather than implied.
 */
#include <stdio.h>
#include <string.h>

#include "reflection_map.h"
#include "../simulation/material_rules_host.h"

static int g_fail = 0, g_checks = 0;
static void ok(int cond, const char *what) {
    g_checks++;
    if (!cond) { g_fail++; printf("FAIL  %s\n", what); }
    else printf("ok    %s\n", what);
}

#define SIZE 64
static unsigned char px[SIZE * SIZE * 4];
static unsigned char px2[SIZE * SIZE * 4];

static int lum(int x, int y) {
    int i = (y * SIZE + x) * 4;
    return px[i] + px[i + 1] + px[i + 2];
}

int main(void) {
    reflection_map_build(px, SIZE);
    reflection_map_build(px2, SIZE);
    ok(memcmp(px, px2, sizeof px) == 0, "the map is deterministic (same bytes every build)");

    {
        static unsigned char seen[65536 / 8];
        int opaque = 1, colors = 0, i;
        memset(seen, 0, sizeof seen);
        for (i = 0; i < SIZE * SIZE; i++) {
            unsigned h;
            if (px[i * 4 + 3] != 255) { opaque = 0; break; }
            h = ((unsigned)px[i * 4] * 7u + (unsigned)px[i * 4 + 1] * 13u + (unsigned)px[i * 4 + 2] * 29u) & 65535u;
            if (!(seen[h >> 3] & (1u << (h & 7u)))) { seen[h >> 3] |= (unsigned char)(1u << (h & 7u)); colors++; }
        }
        printf("      %d distinct colours (hashed) in a %dx%d map\n", colors, SIZE, SIZE);
        ok(opaque, "every texel is opaque (a sphere map is a solid image)");
        ok(colors > 200, "the map is a real, graded environment, not a flat colour");
    }

    /* Sphere-map orientation: row 0 is overhead, the last row is underfoot. A shiny surface must read
     * as bright sky above and dark ground below, or every highlight would look upside-down. */
    {
        int sky = lum(8, 4), ground = lum(8, SIZE - 4);
        printf("      sky luminance %d, ground luminance %d\n", sky, ground);
        ok(sky > ground + 60, "the sky end is much brighter than the ground end");
        ok(px[(4 * SIZE + 8) * 4 + 2] > px[((SIZE - 4) * SIZE + 8) * 4 + 2] + 30,
           "and the sky is bluer than the ground (not just brighter)");
    }

    /* The hot spot is what makes a highlight visibly travel as the camera turns. */
    {
        int hot_x = (int)(0.34f * (SIZE - 1)), hot_y = (int)(0.30f * (SIZE - 1));
        int hot = lum(hot_x, hot_y), cold = lum((int)(0.60f * (SIZE - 1)), (int)(0.85f * (SIZE - 1)));
        printf("      hot spot luminance %d vs a dull texel %d\n", hot, cold);
        ok(hot > cold + 120, "the mirror-ball hot spot is really there");
    }

    /* The streaks: a couple of cold verticals in the horizon band. */
    {
        int streak = lum((int)(0.20f * (SIZE - 1)), (int)(0.50f * (SIZE - 1)));
        int beside = lum((int)(0.30f * (SIZE - 1)), (int)(0.50f * (SIZE - 1)));
        printf("      streak luminance %d, beside it %d\n", streak, beside);
        ok(streak > beside + 30, "the reflected-sign streaks are visible");
    }

    /* A degenerate size is refused rather than writing past the buffer. */
    {
        unsigned char tiny[16];
        memset(tiny, 0xAB, sizeof tiny);
        reflection_map_build(tiny, 0);
        reflection_map_build(tiny, 1);
        ok(tiny[0] == 0xAB && tiny[15] == 0xAB, "size < 2 writes nothing at all");
    }

    /* ---- strengths are PARENA's ------------------------------------------------------------- */
    {
        int mirror = mat_reflection_strength_permille(0);
        int metal = mat_reflection_strength_permille(1);
        int glass = mat_reflection_strength_permille(2);
        int brick = mat_reflection_strength_permille(3);
        int bogus = mat_reflection_strength_permille(-7);
        printf("      strengths: mirror %d, metal %d, glass %d, other %d\n", mirror, metal, glass, brick);
        ok(mirror > metal && metal > glass, "mirror reflects most, then metal, then glass");
        ok(mirror <= 1000 && glass > 0, "all three are inside 0..1000 and all three actually reflect");
        ok(brick == 0, "a non-reflective material costs nothing at all");
        ok(bogus == 0, "an out-of-range kind is 0, not a surprise mirror");
        ok(mirror == 900 && glass == 320, "the shipped numbers are the ones in material_rules.prn");
    }

    printf("\n%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
