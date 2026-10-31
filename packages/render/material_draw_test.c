/* material_draw_test.c -- the material draw-path rule (packages/render/material_draw.h) and the
 * reflection numbers the mirror path depends on (PARENA material_rules.prn).
 *
 * The gap this closes (E3): a level could already name a material, but nothing selected the mirror
 * pass, so a mirrored surface drew as an ordinary brick-textured box. The claims pinned here:
 *   * the three authored shader words resolve to the three draw paths, and everything else -- NULL,
 *     empty, an unknown word, a capitalised typo -- falls back to OPAQUE rather than inventing a
 *     fourth kind;
 *   * the deferred paths are exactly glass and mirror (the two the opaque pass must skip);
 *   * the mirror path's own PARENA strength is the strongest of the reflective kinds, and brick /
 *     concrete / wood cost nothing -- i.e. selecting the mirror pass actually buys something.
 *
 * Build/run: make test-material-draw.
 */
#include <stdio.h>
#include <string.h>

#include "material_draw.h"

/* PARENA-generated (packages/simulation/material_rules.c). */
int mat_reflection_strength_permille(int kind);

/* Material reflect kinds, kept in step with material_shaders.h's MATERIAL_REFLECT_* by hand -- the
 * same manual cross-boundary sync that file's own comment already accepts. */
#define REFLECT_MIRROR 0
#define REFLECT_METAL  1
#define REFLECT_GLASS  2

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

int main(void) {
    /* 1. the vocabulary: three words, three paths */
    CHECK(material_draw_path(SHADER_NAME_MIRROR) == MAT_DRAW_MIRROR, "mirror did not resolve to the mirror pass");
    CHECK(material_draw_path(SHADER_NAME_GLASS) == MAT_DRAW_GLASS, "glass did not resolve to the glass pass");
    CHECK(material_draw_path("standard") == MAT_DRAW_OPAQUE, "standard did not resolve to the opaque pass");
    CHECK(strcmp(SHADER_NAME_MIRROR, "mirror") == 0 && strcmp(SHADER_NAME_GLASS, "glass") == 0,
          "the authored words drifted from the strings the renderer compares against");
    printf("PASS: mirror / glass / standard select three distinct draw paths\n");

    /* 2. anything else is a wall, not a fourth kind */
    CHECK(material_draw_path(NULL) == MAT_DRAW_OPAQUE, "a null shader name was treated as deferred");
    CHECK(material_draw_path("") == MAT_DRAW_OPAQUE, "an empty shader name was treated as deferred");
    CHECK(material_draw_path("metal") == MAT_DRAW_OPAQUE, "an unknown shader name was treated as deferred");
    CHECK(material_draw_path("Mirror") == MAT_DRAW_OPAQUE, "a capitalised typo silently became a mirror");
    CHECK(material_draw_path("mirror ") == MAT_DRAW_OPAQUE, "a trailing space silently became a mirror");
    printf("PASS: null / empty / unknown / capitalised / padded names fall back to the opaque pass\n");

    /* 3. deferred == exactly the two passes drawn after the opaque geometry */
    CHECK(material_draw_is_deferred(MAT_DRAW_GLASS) == 1, "glass was not deferred");
    CHECK(material_draw_is_deferred(MAT_DRAW_MIRROR) == 1, "mirror was not deferred");
    CHECK(material_draw_is_deferred(MAT_DRAW_OPAQUE) == 0, "the opaque pass was deferred");
    CHECK(material_draw_is_deferred(99) == 0, "an out-of-range path was deferred");
    printf("PASS: only glass and mirror skip the opaque pass\n");

    /* 4. the mirror path is worth selecting: PARENA rates it the strongest reflector, and the
          non-reflective surfaces stay at zero (so they keep costing exactly nothing) */
    {
        int mirror = mat_reflection_strength_permille(REFLECT_MIRROR);
        int metal = mat_reflection_strength_permille(REFLECT_METAL);
        int glass = mat_reflection_strength_permille(REFLECT_GLASS);
        CHECK(mirror > metal && metal > glass && glass > 0,
              "reflection strengths are not ordered mirror>metal>glass>0 (got %d/%d/%d)", mirror, metal, glass);
        CHECK(mirror == 900, "the mirror strength moved from 900 permille to %d -- retune deliberately, not by accident", mirror);
        CHECK(mat_reflection_strength_permille(17) == 0, "an unknown reflect kind reflects something");
        printf("PASS: mirror %d > metal %d > glass %d > other 0 permille\n", mirror, metal, glass);
    }

    if (fails) { printf("material_draw_test: %d FAILED\n", fails); return 1; }
    printf("material_draw_test: all checks passed\n");
    return 0;
}
