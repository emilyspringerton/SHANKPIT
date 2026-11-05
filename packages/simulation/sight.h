#ifndef SHANKPIT_SIGHT_H
#define SHANKPIT_SIGHT_H

/* sight.h -- does solid geometry stand between two points?
 *
 * Founder (2026-11-05): "also require line of site". Until now nothing in the simulation tested
 * occlusion at all: ai_gather_perception's "visible" was a range + field-of-view cone, so a clerk saw
 * through the courthouse wall as soon as you were inside its cone, and a verdict scorch burned through
 * a bench. The renderer had the boxes; the simulation had no query against them.
 *
 * This is that query, and it is deliberately pure: no GL, no level_boxes.h, no allocation. The host
 * copies its solid boxes into SightBox (once per level load, not per frame) and story_ai asks it two
 * questions -- can this body see that one, and can this mark reach that player.
 *
 * Real 3D segment-vs-AABB (slab test), so a waist-high bench does NOT block an eye at 1.25 m while a
 * wall does, and a scorch on the floor is stopped by anything that rises above its own height. Boxes are
 * treated as their axis-aligned extents: a rotated or ramp box blocks slightly more than it draws, which
 * is the safe direction for a sight test (you cannot see through a corner because the query rounded it
 * outward). Endpoints INSIDE a box do not count as blocked by that box -- standing in a doorway is not
 * standing inside a wall, and a zero-length ray must read as clear.
 *
 * Named limits: no per-pixel visibility, no translucency (glass blocks), no dynamic bodies (a player or
 * an AI is not a SightBox), and no max-range term here -- range belongs to the caller, which already has
 * vision_range and VERDICT_SCORCH_RADIUS. A world with no boxes installed returns "clear" for every
 * query, which is exactly the old behaviour for hosts that have not federated their geometry yet. */

#include <math.h>

typedef struct {
    float x, y, z;   /* centre, same convention as LevelBox */
    float w, h, d;   /* FULL extents (not half), same convention as LevelBox */
    int solid;       /* 0 = decorative / walk-through: never blocks a sightline */
} SightBox;

static inline int sight_point_inside(const SightBox *b, float x, float y, float z) {
    return fabsf(x - b->x) <= b->w * 0.5f &&
           fabsf(y - b->y) <= b->h * 0.5f &&
           fabsf(z - b->z) <= b->d * 0.5f;
}

/* One box against the segment p0->p1, in segment-parameter space (0..1). Returns 1 when the segment
 * crosses the box's interior between the endpoints. */
static inline int sight_box_blocks(const SightBox *b, float x0, float y0, float z0,
                                   float x1, float y1, float z1) {
    const float o[3]   = { x0, y0, z0 };
    const float dir[3] = { x1 - x0, y1 - y0, z1 - z0 };
    const float c[3]   = { b->x, b->y, b->z };
    const float half[3] = { b->w * 0.5f, b->h * 0.5f, b->d * 0.5f };
    float tmin = 0.0f, tmax = 1.0f;
    int a;
    if (!b->solid) return 0;
    if (sight_point_inside(b, x0, y0, z0)) return 0;   /* the eye is in this box: not a blocker */
    if (sight_point_inside(b, x1, y1, z1)) return 0;   /* the target is in it: same reasoning */
    for (a = 0; a < 3; a++) {
        float lo = c[a] - half[a], hi = c[a] + half[a];
        if (fabsf(dir[a]) < 1.0e-6f) {
            if (o[a] < lo || o[a] > hi) return 0;      /* parallel and outside: misses */
        } else {
            float t1 = (lo - o[a]) / dir[a];
            float t2 = (hi - o[a]) / dir[a];
            if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
            if (t1 > tmin) tmin = t1;
            if (t2 < tmax) tmax = t2;
            if (tmin > tmax) return 0;
        }
    }
    /* A hit exactly at an endpoint is the box you are standing against, not the wall in front of it. */
    return tmax > 1.0e-4f && tmin < 1.0f - 1.0e-4f;
}

/* 1 when any solid box stands between the two points. */
static inline int sight_segment_blocked(const SightBox *boxes, int count,
                                        float x0, float y0, float z0,
                                        float x1, float y1, float z1) {
    int i;
    if (!boxes || count <= 0) return 0;
    for (i = 0; i < count; i++) {
        if (sight_box_blocks(&boxes[i], x0, y0, z0, x1, y1, z1)) return 1;
    }
    return 0;
}

static inline int sight_segment_clear(const SightBox *boxes, int count,
                                      float x0, float y0, float z0,
                                      float x1, float y1, float z1) {
    return !sight_segment_blocked(boxes, count, x0, y0, z0, x1, y1, z1);
}

#endif /* SHANKPIT_SIGHT_H */
