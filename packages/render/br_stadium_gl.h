/* br_stadium_gl.h -- draws the native Bedrock Racers stadium in SHANKPIT's immediate-mode GL.
 *
 * The mesh comes from br_stadium.h (built once at load from PARENA's shape), so the renderer holds no
 * geometry of its own and cannot drift from the SDF the physics runs on. Normals are carried per
 * vertex from the same surface, which is what makes fixed-function lighting and the fog wash run
 * smoothly across the floor-to-wall curl instead of showing a facet seam.
 *
 * Deliberately no textures, no GLSL, no VBO: this is the same path every other world box in the
 * lobby draws through, so the stadium costs the pipeline nothing new.
 *
 * Include AFTER the file that includes <GL/gl.h> (apps/lobby/src/main.c does); no GL header is
 * included here so the headless test can link its own stubs.
 */
#ifndef SHANKPIT_BR_STADIUM_GL_H
#define SHANKPIT_BR_STADIUM_GL_H

#include "br_stadium.h"

/* One tint per surface band, keyed off the normal: the flat track reads light, the walls darken as
 * they curl over, the ceiling is a shadowed lid. Brightness rather than hue, so the stadium sits in
 * whatever palette the level and the weather matrix are already using. */
static inline void br_stadium_shade(const float n[3], float tint) {
    float shade = 0.46f + 0.34f * (n[1] > 0.0f ? n[1] : 0.0f);
    if (shade > 1.0f) shade = 1.0f;
    glColor3f(0.62f * shade * tint, 0.64f * shade * tint, 0.66f * shade * tint);
}

/* br_stadium_gl_draw -- the whole shell. Call inside whatever transform the world boxes use. */
static inline void br_stadium_gl_draw(const BrStadiumMesh *m, float tint) {
    if (!m) return;
    glBegin(GL_QUADS);
    for (int i = 0; i < m->count; i++) {
        const BrStadiumQuad *q = &m->q[i];
        for (int k = 0; k < 4; k++) {
            br_stadium_shade(q->v[k].n, tint);
            glNormal3fv(q->v[k].n);
            glVertex3fv(q->v[k].p);
        }
    }
    glEnd();
}

/* The edge frame, cheap: eight corner posts and the four ridges along the track. Enough to read the
 * bowl's volume at distance without drawing the shell twice. */
static inline void br_stadium_gl_draw_guides(float tint) {
    float H[3], C[3];
    br_stadium_dims(H, C);
    glBegin(GL_LINES);
    glColor3f(0.36f * tint, 0.42f * tint, 0.50f * tint);
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sz = -1; sz <= 1; sz += 2) {
            glVertex3f((float)sx * H[0], C[1] + H[1] + stadium_radius(), (float)sz * H[2]);
            glVertex3f((float)sx * H[0], C[1] - H[1] - stadium_radius(), (float)sz * H[2]);
        }
    }
    for (int sz = -1; sz <= 1; sz += 2) {
        glVertex3f(-H[0], C[1] - H[1] - stadium_radius(), (float)sz * H[2]);
        glVertex3f(H[0], C[1] - H[1] - stadium_radius(), (float)sz * H[2]);
    }
    for (int sx = -1; sx <= 1; sx += 2) {
        glVertex3f((float)sx * H[0], C[1] - H[1] - stadium_radius(), -H[2]);
        glVertex3f((float)sx * H[0], C[1] - H[1] - stadium_radius(), H[2]);
    }
    glEnd();
}

#endif /* SHANKPIT_BR_STADIUM_GL_H */
