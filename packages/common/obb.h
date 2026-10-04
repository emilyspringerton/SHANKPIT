#ifndef SHANKPIT_OBB_H
#define SHANKPIT_OBB_H
/* Oriented boxes + ramps (founder real-time, 2026-10-04: "native rotated box support, we need to
 * handle a non grid world" / "check a box on a cube to turn it into a ramp ... still a cube behind
 * the scenes but when it renders in shankpit it will be a real ramp").
 *
 * A NOCK cube stays a Box (center + full extents, see physics.h) plus an optional BoxOrient: a
 * local->world rotation matrix built from the editor's Euler degrees (three.js 'XYZ' order, so
 * the editor and the game agree exactly) and a ramp flag. Every shape is a convex polytope in the
 * box's local frame described by up to 7 half-spaces n.x <= c: the 6 slabs, plus for a ramp the
 * sloped face  d*y - h*z <= 0  (surface rises toward local +z, solid below it). One generic
 * code path then serves collision (sphere vs inflated polytope), segment tracing and ground-height
 * queries for rotated cubes and ramps alike. Pure math, no globals -- unit tested in
 * apps/tests/test_obb.c. */
#include <math.h>

typedef struct {
    float m[9];          /* row-major local->world rotation */
    unsigned char ramp;  /* 1 = wedge (sloped face rises toward local +z) */
} BoxOrient;

#define OBB_PLANES 7

static inline void orient_from_euler_deg(BoxOrient *o, float rx_deg, float ry_deg, float rz_deg, int ramp) {
    const float k = 0.01745329252f;
    float cx = cosf(rx_deg * k), sx = sinf(rx_deg * k);
    float cy = cosf(ry_deg * k), sy = sinf(ry_deg * k);
    float cz = cosf(rz_deg * k), sz = sinf(rz_deg * k);
    /* R = Rx * Ry * Rz (three.js Euler 'XYZ') */
    o->m[0] = cy * cz;                 o->m[1] = -cy * sz;                o->m[2] = sy;
    o->m[3] = cx * sz + sx * sy * cz;  o->m[4] = cx * cz - sx * sy * sz;  o->m[5] = -sx * cy;
    o->m[6] = sx * sz - cx * sy * cz;  o->m[7] = sx * cz + cx * sy * sz;  o->m[8] = cx * cy;
    o->ramp = (unsigned char)(ramp ? 1 : 0);
}

static inline void orient_identity(BoxOrient *o) { orient_from_euler_deg(o, 0, 0, 0, 0); }

static inline int orient_is_trivial(const BoxOrient *o) {
    if (o->ramp) return 0;
    return fabsf(o->m[0] - 1.0f) < 1e-5f && fabsf(o->m[4] - 1.0f) < 1e-5f && fabsf(o->m[8] - 1.0f) < 1e-5f;
}

/* world vector -> local (multiply by R^T) */
static inline void orient_vec_to_local(const BoxOrient *o, const float v[3], float out[3]) {
    out[0] = o->m[0] * v[0] + o->m[3] * v[1] + o->m[6] * v[2];
    out[1] = o->m[1] * v[0] + o->m[4] * v[1] + o->m[7] * v[2];
    out[2] = o->m[2] * v[0] + o->m[5] * v[1] + o->m[8] * v[2];
}
/* local vector -> world (multiply by R) */
static inline void orient_vec_to_world(const BoxOrient *o, const float v[3], float out[3]) {
    out[0] = o->m[0] * v[0] + o->m[1] * v[1] + o->m[2] * v[2];
    out[1] = o->m[3] * v[0] + o->m[4] * v[1] + o->m[5] * v[2];
    out[2] = o->m[6] * v[0] + o->m[7] * v[1] + o->m[8] * v[2];
}

/* Fill the local-space half-spaces n.x <= c. Returns the plane count (6, or 7 for a ramp).
   w/h/d are FULL extents. */
static inline int obb_local_planes(float w, float h, float d, int ramp, float n[OBB_PLANES][3], float c[OBB_PLANES]) {
    static const float axes[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    float half[3] = { w * 0.5f, h * 0.5f, d * 0.5f };
    for (int i = 0; i < 6; i++) {
        n[i][0] = axes[i][0]; n[i][1] = axes[i][1]; n[i][2] = axes[i][2];
        c[i] = half[i / 2];
    }
    if (!ramp) return 6;
    float len = sqrtf(d * d + h * h);
    if (len < 1e-6f) return 6;
    n[6][0] = 0.0f; n[6][1] = d / len; n[6][2] = -h / len;
    c[6] = 0.0f; /* the slope plane passes through the box center */
    return 7;
}

static inline float obb_bound_radius(float w, float h, float d) {
    return 0.5f * sqrtf(w * w + h * h + d * d);
}

/* Sphere (world center cx,cy,cz, radius r) vs the oriented polytope centred at (bx,by,bz).
   Treats the shape as inflated by r along each face (exact for faces, slightly boxy at edges).
   Returns 1 on overlap, with the world-space exit normal and penetration depth. */
static inline int obb_sphere_push(float bx, float by, float bz, float w, float h, float d, const BoxOrient *o,
                                  float cx, float cy, float cz, float r, float out_n[3], float *out_depth) {
    float R = obb_bound_radius(w, h, d) + r;
    float dx = cx - bx, dy = cy - by, dz = cz - bz;
    if (dx * dx + dy * dy + dz * dz > R * R) return 0;
    float n[OBB_PLANES][3], c[OBB_PLANES];
    int np = obb_local_planes(w, h, d, o->ramp, n, c);
    float rel[3] = { dx, dy, dz }, l[3];
    orient_vec_to_local(o, rel, l);
    float best = -1e30f;
    int bi = -1;
    for (int i = 0; i < np; i++) {
        float dist = n[i][0] * l[0] + n[i][1] * l[1] + n[i][2] * l[2] - c[i];
        if (dist >= r) return 0; /* separated by this plane */
        if (dist > best) { best = dist; bi = i; }
    }
    if (bi < 0) return 0;
    orient_vec_to_world(o, n[bi], out_n);
    *out_depth = r - best;
    return 1;
}

/* Segment o + t*dir, t in [0,tmax_in], vs the polytope. Returns 1 on a front-face hit with the
   smallest t in [0, tmax_in]; writes world normal. */
static inline int obb_segment_hit(float bx, float by, float bz, float w, float h, float d, const BoxOrient *o,
                                  float ox, float oy, float oz, float dx, float dy, float dz,
                                  float tmax_in, float *out_t, float out_n[3]) {
    float n[OBB_PLANES][3], c[OBB_PLANES];
    int np = obb_local_planes(w, h, d, o->ramp, n, c);
    float rel[3] = { ox - bx, oy - by, oz - bz }, dir[3] = { dx, dy, dz }, lo[3], ld[3];
    orient_vec_to_local(o, rel, lo);
    orient_vec_to_local(o, dir, ld);
    float tmin = 0.0f, tmax = tmax_in;
    int enter = -1;
    for (int i = 0; i < np; i++) {
        float denom = n[i][0] * ld[0] + n[i][1] * ld[1] + n[i][2] * ld[2];
        float num = c[i] - (n[i][0] * lo[0] + n[i][1] * lo[1] + n[i][2] * lo[2]);
        if (fabsf(denom) < 1e-8f) {
            if (num < 0.0f) return 0;
            continue;
        }
        float t = num / denom;
        if (denom < 0.0f) { if (t > tmin) { tmin = t; enter = i; } }
        else if (t < tmax) tmax = t;
        if (tmin > tmax) return 0;
    }
    if (enter < 0) return 0; /* started inside: not a front-face hit */
    *out_t = tmin;
    orient_vec_to_world(o, n[enter], out_n);
    return 1;
}

/* Highest surface of the polytope above world (x,z); returns 1 and writes its world y. */
static inline int obb_ground_y(float bx, float by, float bz, float w, float h, float d, const BoxOrient *o,
                               float x, float z, float *out_y) {
    float R = obb_bound_radius(w, h, d);
    float ddx = x - bx, ddz = z - bz;
    if (ddx * ddx + ddz * ddz > R * R) return 0;
    float t, nn[3];
    float top = by + R + 1.0f;
    if (!obb_segment_hit(bx, by, bz, w, h, d, o, x, top, z, 0.0f, -(2.0f * R + 2.0f), 0.0f, 1.0f, &t, nn)) return 0;
    *out_y = top - t * (2.0f * R + 2.0f);
    return 1;
}

#endif
