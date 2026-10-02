/* ambient_boost_test.c -- the pause-menu ambient slider (card #460): the PARENA-derived boost must
   brighten a shaded surface, level 0 must leave the original look untouched, and the boost must be
   monotonic in the slider level. make test-ambient-boost */
#include <stdio.h>
#include <string.h>
#include "retro_lighting.h"

int ambient_boost_permille(int level);   /* PARENA-generated (ambient_rules.c) */

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

int main(void) {
    RetroLightingState st;
    retro_lighting_eval(0.0f, RETRO_LIGHTING_DYNAMIC, &st);
    /* a face turned away from sun and moon: only ambient + sky fill reach it */
    float r0, g0, b0;
    retro_lighting_set_ambient_boost(0.0f);
    retro_lighting_eval_surface_rgb(&st, 0.0f, -1.0f, 0.0f, 0.6f, &r0, &g0, &b0);

    float prev = r0;
    for (int level = 0; level <= 10; level++) {
        retro_lighting_set_ambient_boost((float)ambient_boost_permille(level) * 0.001f);
        float r, g, b;
        retro_lighting_eval_surface_rgb(&st, 0.0f, -1.0f, 0.0f, 0.6f, &r, &g, &b);
        if (level == 0) CHECK(r == r0 && g == g0 && b == b0);       /* level 0 = original look, bit for bit */
        CHECK(r >= prev);                                           /* monotonic in the slider */
        CHECK(r <= 1.0f && g <= 1.0f && b <= 1.0f);                 /* never blows past white */
        prev = r;
    }
    CHECK(prev > r0 || r0 >= 1.0f);                                 /* level 10 really brightens (unless already white) */
    retro_lighting_set_ambient_boost(0.0f);
    if (g_fail) { printf("ambient_boost_test: %d FAILED\n", g_fail); return 1; }
    printf("ambient_boost_test OK (shaded face r: level0 %.3f -> level10 %.3f)\n", r0, prev);
    return 0;
}
