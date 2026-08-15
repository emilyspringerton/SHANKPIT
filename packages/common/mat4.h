// mat4.h — S144-02 Stage B: minimal Mat4 type for GOLDENBAND's gband_mesh_rig
// integration. SHANKPIT's renderer is otherwise 100% legacy fixed-function
// (glBegin/glVertex3f, glRotatef/glTranslatef on the GL matrix stack) --
// this type exists only for the new shader/VBO path (packages/render/
// gl_shader.h) and gband_mesh_rig, not as a general replacement for the
// fixed-function pipeline. Ported from REDGARDEN/packages/common/mat4.h;
// trimmed to what gband_mesh_rig.c actually calls (identity/multiply/
// translate/rotate_y) plus mat4_scale for parity -- no perspective/
// orbit_view helpers, since SHANKPIT's camera/projection already comes from
// the legacy gluPerspective/gluLookAt pipeline (captured via glGetFloatv,
// see draw_scene's g_frame_vp), not built here.
#ifndef MAT4_H
#define MAT4_H

#include <math.h>

/* Column-major 4x4, matching OpenGL's expected layout for glUniformMatrix4fv. */
typedef struct { float m[16]; } Mat4;

static inline Mat4 mat4_identity(void) {
    Mat4 r = {0};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

static inline Mat4 mat4_multiply(const Mat4 *a, const Mat4 *b) {
    Mat4 r = {0};
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += a->m[k * 4 + row] * b->m[col * 4 + k];
            }
            r.m[col * 4 + row] = sum;
        }
    }
    return r;
}

static inline Mat4 mat4_translate(float x, float y, float z) {
    Mat4 r = mat4_identity();
    r.m[12] = x; r.m[13] = y; r.m[14] = z;
    return r;
}

static inline Mat4 mat4_scale(float sx, float sy, float sz) {
    Mat4 r = mat4_identity();
    r.m[0] = sx; r.m[5] = sy; r.m[10] = sz;
    return r;
}

/* Rotation about the vertical (Y) axis by angle_rad. Right-handed, matching
 * this file's column-major/OpenGL convention -- same rotation direction as
 * glRotatef(deg, 0,1,0) on the legacy matrix stack, so facing math derived
 * against one is consistent with the other. */
static inline Mat4 mat4_rotate_y(float angle_rad) {
    Mat4 r = mat4_identity();
    float c = cosf(angle_rad), s = sinf(angle_rad);
    r.m[0] = c;  r.m[2] = -s;
    r.m[8] = s;  r.m[10] = c;
    return r;
}

#endif
