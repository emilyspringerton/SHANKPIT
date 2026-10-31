#ifndef SHANKPIT_BR_STADIUM_H
#define SHANKPIT_BR_STADIUM_H

/* br_stadium.h -- Bedrock Racers' stadium, natively: the SDF the car drives on and the exact mesh
 * the renderer draws, both generated from the SAME PARENA numbers as the wasm game
 * (PARENA/stdlib/bedrockracers/web/br_stadium.prn -> br_stadium.c).
 *
 * WHY THIS SHAPE: the arena is a rounded box -- floor, four walls and the ceiling joined by 70-unit
 * quarter-pipes -- so a wall is just a steeper bit of floor and the curl at the top is a road that
 * goes over your head. Wall-riding needs a CONTINUOUS normal, which is why the authoritative shape is
 * an SDF (one normal per point) and the faceted shell is only for destruction and drawing.
 *
 * The mesh is EXACT, not a shrink-wrapped approximation: six flat faces inset by the fillet radius,
 * twelve quarter-cylinder edges, eight eighth-sphere corners. Every emitted vertex therefore sits on
 * the SDF surface to floating-point precision, which is what makes the fixed-function lighting
 * (retro_lighting) interpolate across the joint without a visible band.
 *
 * Deliberately NOT here: the four goal shafts (they are separate SDF terms and a separate mesh), the
 * baked floor texture, and any collision response -- those belong to the physics step and the level
 * loader, and faking them here would hide the work that is actually still missing.
 */

#include <math.h>
#include "br_stadium_host.h"

#define BR_STADIUM_MAX_QUADS 4096

typedef struct { float p[3]; float n[3]; } BrStadiumVert;
typedef struct { BrStadiumVert v[4]; } BrStadiumQuad;
typedef struct { BrStadiumQuad q[BR_STADIUM_MAX_QUADS]; int count; } BrStadiumMesh;

/* ---- queries -------------------------------------------------------------------------------------- */
static inline float br_stadium_free(float x, float y, float z) { return stadium_free(x, y, z); }

/* The surface normal at a point: the SDF's gradient, by central difference. Points INTO the open air
 * (out of the wall), which is what a car's fans clamp its up-axis to. */
static inline void br_stadium_normal(float x, float y, float z, float n[3]) {
    const float e = 0.5f;
    float dx = stadium_free(x + e, y, z) - stadium_free(x - e, y, z);
    float dy = stadium_free(x, y + e, z) - stadium_free(x, y - e, z);
    float dz = stadium_free(x, y, z + e) - stadium_free(x, y, z - e);
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len < 1e-6f) { n[0] = 0.0f; n[1] = 1.0f; n[2] = 0.0f; return; }
    n[0] = dx / len; n[1] = dy / len; n[2] = dz / len;
}

static inline int br_stadium_open(float x, float y, float z) { return stadium_open(x, y, z); }

/* The fans' hold on a surface, 0..1000 permille of the car's weight (PARENA's curve). */
static inline int br_stadium_grip_permille(int speed_milli) { return stadium_grip_permille(speed_milli); }

/* ---- mesh ----------------------------------------------------------------------------------------- */
static inline void br_stadium_mesh_init(BrStadiumMesh *m) { if (m) m->count = 0; }

static inline BrStadiumQuad *br_stadium_quad(BrStadiumMesh *m) {
    if (!m || m->count >= BR_STADIUM_MAX_QUADS) return 0;
    return &m->q[m->count++];
}

static inline void br_stadium_vert(BrStadiumQuad *qd, int i,
                                   float x, float y, float z,
                                   float nx, float ny, float nz) {
    qd->v[i].p[0] = x; qd->v[i].p[1] = y; qd->v[i].p[2] = z;
    qd->v[i].n[0] = nx; qd->v[i].n[1] = ny; qd->v[i].n[2] = nz;
}

/* shrunk half-extents and the room's centre (the fillet radius is measured from these faces) */
static inline void br_stadium_dims(float H[3], float C[3]) {
    H[0] = stadium_half_x() - stadium_radius();
    H[1] = stadium_height() * 0.5f - stadium_radius();
    H[2] = stadium_half_z() - stadium_radius();
    C[0] = 0.0f;
    C[1] = stadium_center_y();
    C[2] = 0.0f;
}

/* one flat face: `axis` fixed at +-sign, the other two swept over their shrunk extents */
static inline void br_stadium_face(BrStadiumMesh *m, int axis, float sgn, int q) {
    static const int U[3] = { 1, 0, 0 };
    static const int V[3] = { 2, 2, 1 };
    float H[3], C[3];
    int u, v;
    br_stadium_dims(H, C);
    u = U[axis];
    v = V[axis];
    for (int i = 0; i < q; i++) {
        for (int j = 0; j < q; j++) {
            BrStadiumQuad *qd = br_stadium_quad(m);
            if (!qd) return;
            for (int k = 0; k < 4; k++) {
                float fu = (float)(i + ((k == 1 || k == 2) ? 1 : 0)) / (float)q;
                float fv = (float)(j + ((k >= 2) ? 1 : 0)) / (float)q;
                float p[3] = { 0.0f, 0.0f, 0.0f };
                float n[3] = { 0.0f, 0.0f, 0.0f };
                p[axis] = C[axis] + sgn * (H[axis] + stadium_radius());
                p[u] = C[u] + (-H[u] + 2.0f * H[u] * fu);
                p[v] = C[v] + (-H[v] + 2.0f * H[v] * fv);
                n[axis] = -sgn;   /* toward the room's air, i.e. the gradient of `free` */
                br_stadium_vert(qd, k, p[0], p[1], p[2], n[0], n[1], n[2]);
            }
        }
    }
}

/* one quarter-cylinder fillet between the faces normal to axes i and j, running along axis k */
static inline void br_stadium_edge(BrStadiumMesh *m, int i, int j, float si, float sj, int q) {
    int k = 3 - i - j;
    float H[3], C[3];
    br_stadium_dims(H, C);
    for (int a = 0; a < q; a++) {
        for (int b = 0; b < q; b++) {
            BrStadiumQuad *qd = br_stadium_quad(m);
            if (!qd) return;
            for (int c = 0; c < 4; c++) {
                int ia = a + ((c == 1 || c == 2) ? 1 : 0);
                int ib = b + ((c >= 2) ? 1 : 0);
                float th = (float)ia * (1.57079632679f / (float)q);
                float t = -H[k] + 2.0f * H[k] * ((float)ib / (float)q);
                float out_i = si * cosf(th);   /* radially OUT of the arc centre */
                float out_j = sj * sinf(th);
                float n[3] = { 0.0f, 0.0f, 0.0f };
                float p[3] = { 0.0f, 0.0f, 0.0f };
                n[i] = -out_i;   /* the normal points back toward the air */
                n[j] = -out_j;
                p[i] = C[i] + si * H[i] + stadium_radius() * out_i;
                p[j] = C[j] + sj * H[j] + stadium_radius() * out_j;
                p[k] = C[k] + t;
                br_stadium_vert(qd, c, p[0], p[1], p[2], n[0], n[1], n[2]);
            }
        }
    }
}

/* one eighth-sphere corner */
static inline void br_stadium_corner(BrStadiumMesh *m, float sx, float sy, float sz, int q) {
    float H[3], C[3];
    br_stadium_dims(H, C);
    for (int a = 0; a < q; a++) {
        for (int b = 0; b < q; b++) {
            BrStadiumQuad *qd = br_stadium_quad(m);
            if (!qd) return;
            for (int c = 0; c < 4; c++) {
                int ia = a + ((c == 1 || c == 2) ? 1 : 0);
                int ib = b + ((c >= 2) ? 1 : 0);
                float th = (float)ia * (1.57079632679f / (float)q);   /* polar, from +y */
                float ph = (float)ib * (1.57079632679f / (float)q);   /* azimuth in the xz quadrant */
                float nx = sx * sinf(th) * cosf(ph);
                float ny = sy * cosf(th);
                float nz = sz * sinf(th) * sinf(ph);
                float r = stadium_radius();
                br_stadium_vert(qd, c,
                                sx * H[0] + r * nx,
                                C[1] + sy * H[1] + r * ny,
                                sz * H[2] + r * nz,
                                -nx, -ny, -nz);   /* position outward, normal toward the air */
            }
        }
    }
}

/* br_stadium_build_mesh -- the whole shell at `quality` subdivisions per face/edge/corner.
 * quality 4 is 416 quads and already reads smooth under fixed-function lighting; 6 is 936. */
static inline int br_stadium_build_mesh(int quality, BrStadiumMesh *m) {
    if (!m) return 0;
    br_stadium_mesh_init(m);
    if (quality < 1) quality = 1;
    for (int axis = 0; axis < 3; axis++) {
        br_stadium_face(m, axis, 1.0f, quality);
        br_stadium_face(m, axis, -1.0f, quality);
    }
    for (int i = 0; i < 3; i++) {
        for (int j = i + 1; j < 3; j++) {
            br_stadium_edge(m, i, j, 1.0f, 1.0f, quality);
            br_stadium_edge(m, i, j, 1.0f, -1.0f, quality);
            br_stadium_edge(m, i, j, -1.0f, 1.0f, quality);
            br_stadium_edge(m, i, j, -1.0f, -1.0f, quality);
        }
    }
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sy = -1; sy <= 1; sy += 2) {
            for (int sz = -1; sz <= 1; sz += 2) {
                br_stadium_corner(m, (float)sx, (float)sy, (float)sz, quality);
            }
        }
    }
    return m->count;
}

#endif /* SHANKPIT_BR_STADIUM_H */
