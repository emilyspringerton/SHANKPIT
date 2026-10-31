/* reflection.h -- a lightweight environment reflection for metal and glass, with no assets at all.
 *
 * Founder: "we want to implement basic reflection in metal and glass materials i guess make a mirror
 * material? it needs to be lightweight".
 *
 * WHAT THIS IS: fixed-function SPHERE MAPPING (GL_SPHERE_MAP), the cheapest real reflection GL 1.x
 * offers. The driver computes the lookup coordinate from the vertex normal and the camera basis, so
 * there is no render target, no second scene pass, no cube map, no shader, and no per-frame CPU work
 * -- one extra textured draw of the same 36-vertex box, additively blended over the base pass.
 *
 * WHAT IT IS NOT, said plainly: a sphere map is a photograph of a mirror ball, so the reflection is
 * approximate. It has no parallax (it does not slide as you walk past), it cannot reflect the actual
 * level in front of a surface, and a flat mirror built from it looks like brushed chrome rather than a
 * true mirror. That is the trade a 1996 renderer makes, and it is the right one here: a real planar
 * or cube reflection costs an extra scene render per frame, which this target cannot pay.
 *
 * THE MAP: generated procedurally at first use (no image file, matching proctex_* and every other
 * render asset in this engine). It reads as a bright sky above a darker horizon, a warm band of
 * low sun, and a few cool vertical streaks -- enough that a curved highlight MOVES as the camera does,
 * which is the entire visual cue that a surface is shiny. Tint and strength are per-material.
 *
 * STATE CONTRACT: reflection_begin() saves what it changes and reflection_end() puts it back --
 * texture binding, TEXTURE_GEN_S/T, GL_TEXTURE_2D, blend func, depth mask, colour. Callers may nest
 * nothing, and must call end() even if they draw nothing (every draw helper here does).
 *
 * Requires immediate mode with per-vertex NORMALS (sphere mapping reads them); the engine's own
 * box drawing already supplies them. No GL header is included here on purpose: include this after
 * <SDL2/SDL_opengl.h>, the same contract material_shaders.h uses.
 */
#ifndef SHANKPIT_REFLECTION_H
#define SHANKPIT_REFLECTION_H

#include "reflection_map.h"

#define REFLECTION_MAP_SIZE 64

typedef struct {
    GLuint tex;
    GLint prev_tex;      /* the binding in force before an active pass, restored by _end() */
    int active;
    int ready;
    int draws;           /* diagnostics: how many reflective draws have happened */
} ReflectionState;

static ReflectionState g_reflection;

/* reflection_init -- creates the sphere map. Safe to call twice; a no-op once ready. Called lazily by
 * reflection_begin() so a host cannot forget it (and so it never runs before a GL context exists). */
static void reflection_init(void) {
    static unsigned char pixels[REFLECTION_MAP_SIZE * REFLECTION_MAP_SIZE * 4];
    if (g_reflection.ready) return;
    reflection_map_build(pixels, REFLECTION_MAP_SIZE);
    glGenTextures(1, &g_reflection.tex);
    glBindTexture(GL_TEXTURE_2D, g_reflection.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);   /* GL 1.1: GL_CLAMP_TO_EDGE is 1.2 */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, REFLECTION_MAP_SIZE, REFLECTION_MAP_SIZE, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    g_reflection.ready = 1;
    g_reflection.draws = 0;
}

static int reflection_ready(void) {
    if (!g_reflection.ready) reflection_init();
    return g_reflection.ready;
}

/* reflection_begin -- sets up the additive sphere-map pass. `strength_permille` is how bright the
 * reflection reads (0 draws nothing; the caller should skip the draw entirely at 0). `tint` is an RGB
 * multiplier, which is what makes the same map read as chrome, steel or cyan glass. */
static int reflection_begin(int strength_permille, const float tint[3]) {
    if (strength_permille <= 0) return 0;
    if (strength_permille > 1000) strength_permille = 1000;
    if (!reflection_ready()) return 0;
    if (g_reflection.active) return 0;   /* not re-entrant: the caller nests nothing, and this
                                          * refuses rather than silently corrupting the saved state */

    glGetIntegerv(GL_TEXTURE_BINDING_2D, &g_reflection.prev_tex);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_reflection.tex);
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
    glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
    glEnable(GL_TEXTURE_GEN_S);
    glEnable(GL_TEXTURE_GEN_T);
    /* Sphere mapping reads the vertex normal through the current MODELVIEW, so the modelview must be
     * the world/camera one with no model transform bolted on. This engine bakes transforms into the
     * vertices, so it already is -- documented here because a caller that ever adds a glTranslate
     * around this pass would get a reflection that swims. */

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);   /* additive: a highlight, never an opaque coat */
    glDepthMask(GL_FALSE);               /* it sits on a surface already written this frame */
    glColor4f(tint[0] * (float)strength_permille / 1000.0f,
              tint[1] * (float)strength_permille / 1000.0f,
              tint[2] * (float)strength_permille / 1000.0f, 1.0f);
    g_reflection.active = 1;
    return 1;
}

static void reflection_end(void) {
    if (!g_reflection.active) return;
    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_GEN_T);
    glBindTexture(GL_TEXTURE_2D, (GLuint)g_reflection.prev_tex);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    g_reflection.active = 0;
}

/* reflection_draw_box -- one reflective box, the same world-space 36-vertex shape the material
 * shaders use (x/y/z is the CENTRE, w/h/d full extents, matching physics.h's own Box convention).
 * Returns 1 if it drew. */
static int reflection_draw_box(float x, float y, float z, float w, float h, float d,
                               int strength_permille, const float tint[3]) {
    float hw = w * 0.5f, hh = h * 0.5f, hd = d * 0.5f;
    if (!reflection_begin(strength_permille, tint)) return 0;

    glBegin(GL_QUADS);
    /* top (+y) */
    glNormal3f(0.0f, 1.0f, 0.0f);
    glVertex3f(x - hw, y + hh, z + hd); glVertex3f(x + hw, y + hh, z + hd);
    glVertex3f(x + hw, y + hh, z - hd); glVertex3f(x - hw, y + hh, z - hd);
    /* bottom (-y) */
    glNormal3f(0.0f, -1.0f, 0.0f);
    glVertex3f(x - hw, y - hh, z - hd); glVertex3f(x + hw, y - hh, z - hd);
    glVertex3f(x + hw, y - hh, z + hd); glVertex3f(x - hw, y - hh, z + hd);
    /* front (+z) */
    glNormal3f(0.0f, 0.0f, 1.0f);
    glVertex3f(x - hw, y - hh, z + hd); glVertex3f(x + hw, y - hh, z + hd);
    glVertex3f(x + hw, y + hh, z + hd); glVertex3f(x - hw, y + hh, z + hd);
    /* back (-z) */
    glNormal3f(0.0f, 0.0f, -1.0f);
    glVertex3f(x + hw, y - hh, z - hd); glVertex3f(x - hw, y - hh, z - hd);
    glVertex3f(x - hw, y + hh, z - hd); glVertex3f(x + hw, y + hh, z - hd);
    /* left (-x) */
    glNormal3f(-1.0f, 0.0f, 0.0f);
    glVertex3f(x - hw, y - hh, z - hd); glVertex3f(x - hw, y - hh, z + hd);
    glVertex3f(x - hw, y + hh, z + hd); glVertex3f(x - hw, y + hh, z - hd);
    /* right (+x) */
    glNormal3f(1.0f, 0.0f, 0.0f);
    glVertex3f(x + hw, y - hh, z + hd); glVertex3f(x + hw, y - hh, z - hd);
    glVertex3f(x + hw, y + hh, z - hd); glVertex3f(x + hw, y + hh, z + hd);
    glEnd();

    reflection_end();
    g_reflection.draws++;
    return 1;
}

#endif /* SHANKPIT_REFLECTION_H */
