/* material_draw.h -- which DRAW PATH a level's material takes, decided by its shader name.
 *
 * E3 in docs/NAMED_NOT_DONE.md: "the material list can't author mirrors". The mirror material has
 * existed as a draw function (packages/render/material_shaders.h's draw_material_mirror_box) since
 * the reflection pass landed, but nothing ever SELECTED it -- a level that named a mirror got an
 * ordinary brick-textured box. The selection rule is the whole gap, so the rule lives here, with no
 * GL in it: a level author writes `"shader_name": "mirror"` and this says which pass draws it.
 *
 * Why the SHADER name and not the material name: a material name is a surface's identity (its
 * friction, density, texture), a shader name is how it is drawn, and one draw path can serve several
 * materials -- glass and a mirror are both "a box the opaque pass must not draw". The renderer
 * already keys glass off `shader_name`, so mirrors key off it the same way rather than growing a
 * second, parallel vocabulary.
 *
 * Deliberately a closed, lowercase list. An authored level always writes lowercase (every shipped
 * level and every editor default does), so "Mirror" is a typo rather than a dialect: it falls back to
 * the ordinary opaque path, which draws a visible wall instead of silently inventing a fourth kind.
 *
 * No includes, no state: the lobby, a future host and this package's own test all get the same answer.
 */
#ifndef SHANKPIT_MATERIAL_DRAW_H
#define SHANKPIT_MATERIAL_DRAW_H

#include <string.h>

/* Draw paths. OPAQUE is the ordinary textured pass and the answer for anything unrecognised. */
#define MAT_DRAW_OPAQUE  0
#define MAT_DRAW_GLASS   1   /* transparent, alpha-blended, drawn after every opaque box */
#define MAT_DRAW_MIRROR  2   /* near-black base + sphere-mapped environment, drawn after glass */

static inline int material_draw_path(const char *shader_name) {
    if (!shader_name) return MAT_DRAW_OPAQUE;
    if (strcmp(shader_name, "mirror") == 0) return MAT_DRAW_MIRROR;
    if (strcmp(shader_name, "glass") == 0) return MAT_DRAW_GLASS;
    return MAT_DRAW_OPAQUE;
}

/* Does this draw path need to be collected and drawn in its own pass, after the opaque geometry?
 * Both of these do (glass so a pane never hides what is behind it, a mirror because its base is
 * near-black and its surface is the reflection). The lobby asks this instead of testing the path
 * twice with opposite polarity. */
static inline int material_draw_is_deferred(int path) {
    return path == MAT_DRAW_GLASS || path == MAT_DRAW_MIRROR;
}

/* The one word a level JSON must contain to author each deferred surface, so a host, an editor hint
 * and a test all spell it the same way. */
#define SHADER_NAME_GLASS  "glass"
#define SHADER_NAME_MIRROR "mirror"

#endif /* SHANKPIT_MATERIAL_DRAW_H */
