#ifndef SHANKPIT_BRICK_DEBRIS_H
#define SHANKPIT_BRICK_DEBRIS_H

/* brick_debris.h -- the cosmetic half of destructible brick: chunks that fly off a wall when a cell
 * cracks, tears or goes (packages/world/brick_fracture.h events). Client-only, render-side state;
 * the server never simulates any of it and nothing here can affect gameplay or the network.
 *
 * Papercraft look (PAPERCRAFT's Paper Engine): flat, irregular shards -- each chunk is a thin
 * flake with its own seeded size, spin and shade, not a clean cube. Deterministic by seed: the same
 * cell event always throws the same chunks, so two clients watching the same wall see the same
 * crumble.
 */

#include <math.h>

#define BRICK_DEBRIS_MAX 256

typedef struct {
    int active;
    float x, y, z;
    float vx, vy, vz;
    float rx, ry, rz;      /* euler angles, degrees */
    float vrx, vry, vrz;   /* spin, degrees per second */
    float sx, sy, sz;      /* flake extents */
    float shade;           /* 0.7 .. 1.15 brick tint multiplier */
    float life;            /* seconds remaining */
} BrickDebris;

typedef struct {
    BrickDebris d[BRICK_DEBRIS_MAX];
    int cursor;            /* ring: the oldest chunk is recycled when the pool is full */
} BrickDebrisPool;

static inline unsigned int brick_debris_hash(unsigned int x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
static inline float brick_debris_r01(unsigned int *s) {
    *s = brick_debris_hash(*s + 0x9E3779B9u);
    return (float)(*s & 0xFFFFu) / 65535.0f;
}

static inline void brick_debris_clear(BrickDebrisPool *p) {
    for (int i = 0; i < BRICK_DEBRIS_MAX; i++) p->d[i].active = 0;
    p->cursor = 0;
}

/* brick_debris_spawn -- `count` flakes thrown from a cell centre (cx,cy,cz) of size (cw,ch,cd).
 * `seed` should identify the event (parent/key/state) so it is reproducible. */
static inline void brick_debris_spawn(BrickDebrisPool *p, float cx, float cy, float cz,
                                      float cw, float ch, float cd, int count, unsigned int seed) {
    unsigned int s = brick_debris_hash(seed ? seed : 1u);
    float cell = cw;
    if (ch > cell) cell = ch;
    if (cd > cell) cell = cd;
    for (int i = 0; i < count; i++) {
        BrickDebris *b = &p->d[p->cursor];
        p->cursor = (p->cursor + 1) % BRICK_DEBRIS_MAX;
        b->active = 1;
        b->x = cx + (brick_debris_r01(&s) - 0.5f) * cw * 0.8f;
        b->y = cy + (brick_debris_r01(&s) - 0.5f) * ch * 0.8f;
        b->z = cz + (brick_debris_r01(&s) - 0.5f) * cd * 0.8f;
        float speed = 4.0f + brick_debris_r01(&s) * 9.0f;
        float ang = brick_debris_r01(&s) * 6.2831853f;
        b->vx = cosf(ang) * speed;
        b->vz = sinf(ang) * speed;
        b->vy = 3.0f + brick_debris_r01(&s) * 8.0f;
        b->rx = brick_debris_r01(&s) * 360.0f; b->ry = brick_debris_r01(&s) * 360.0f; b->rz = brick_debris_r01(&s) * 360.0f;
        b->vrx = (brick_debris_r01(&s) - 0.5f) * 720.0f;
        b->vry = (brick_debris_r01(&s) - 0.5f) * 720.0f;
        b->vrz = (brick_debris_r01(&s) - 0.5f) * 720.0f;
        float base = cell * (0.16f + 0.18f * brick_debris_r01(&s));
        b->sx = base * (0.8f + 0.5f * brick_debris_r01(&s));
        b->sy = base * (0.18f + 0.22f * brick_debris_r01(&s));   /* thin: a torn flake, not a cube */
        b->sz = base * (0.7f + 0.5f * brick_debris_r01(&s));
        b->shade = 0.7f + 0.45f * brick_debris_r01(&s);
        b->life = 1.6f + 1.2f * brick_debris_r01(&s);
    }
}

/* brick_debris_update -- gravity, drag, a bounce off the ground plane (y = 0), spin, expiry. */
static inline void brick_debris_update(BrickDebrisPool *p, float dt) {
    if (dt > 0.1f) dt = 0.1f;
    for (int i = 0; i < BRICK_DEBRIS_MAX; i++) {
        BrickDebris *b = &p->d[i];
        if (!b->active) continue;
        b->life -= dt;
        if (b->life <= 0.0f) { b->active = 0; continue; }
        b->vy -= 38.0f * dt;
        b->x += b->vx * dt; b->y += b->vy * dt; b->z += b->vz * dt;
        b->rx += b->vrx * dt; b->ry += b->vry * dt; b->rz += b->vrz * dt;
        if (b->y < b->sy) {
            b->y = b->sy;
            if (b->vy < 0.0f) b->vy = -b->vy * 0.28f;
            b->vx *= 0.7f; b->vz *= 0.7f; b->vrx *= 0.6f; b->vry *= 0.6f; b->vrz *= 0.6f;
        }
    }
}

static inline int brick_debris_active_count(const BrickDebrisPool *p) {
    int n = 0;
    for (int i = 0; i < BRICK_DEBRIS_MAX; i++) if (p->d[i].active) n++;
    return n;
}

#endif
