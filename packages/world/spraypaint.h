#ifndef SHANKPIT_SPRAYPAINT_H
#define SHANKPIT_SPRAYPAINT_H

// spraypaint.h -- engine side of the spray can (WPN_SPRAYCAN, weapon 9; founder real-time
// 2026-10-15: "a spray can ... paints on level geometry ... simulate paint thickness on the wall
// ... if you are closer to the wall and hold the paint on a spot it should drip").
//
// GL-free and trace-agnostic so packages/world/spraypaint_test.c can drive it headless: the caller
// supplies a raycast callback (apps/lobby passes trace_map). What this owns:
//   * the cone: each spray tick fires SPRAY_RAYS rays spread inside a SPRAY_HALF_ANGLE_DEG cone
//     (same cone idea as the flashlight beam) and deposits paint where they land;
//   * thickness: every deposit adds paint to the "dab" under it (same colour, same plane, close
//     enough) instead of making a new one, with amount falling off with distance -- point blank
//     deposits several times what a spray from across the room does, and a close, held spray
//     builds a thick wet spot;
//   * drips: a dab past SPRAY_DRIP_THRESHOLD on a (near-)vertical surface grows a drip that runs
//     down the wall, drawing paint out of the dab until it thins back under the threshold, and is
//     stopped by whatever surface is below it.
// Decals are cosmetic and client-side, observed per player from synced weapon/is_shooting/ammo
// (colour index) -- the same way bullet_hole.h works for remote players, no protocol change.
// Honest limits: dabs are circular soft blobs (no per-pixel mask), paint is not shared with
// late joiners (like bullet holes), drips run straight down (no sideways wander).

#include <math.h>
#include <string.h>

#define SPRAY_COLOR_COUNT_ENGINE 7
#define SPRAY_DAB_MAX 2048
#define SPRAY_RANGE 14.0f
#define SPRAY_RAYS 6
#define SPRAY_HALF_ANGLE_DEG 9.0f
#define SPRAY_TICK_MS 33            /* ~30 Hz deposit rate */
#define SPRAY_THICK_RATE 0.0060f    /* thickness per ray at point blank (falls off with distance) */
#define SPRAY_THICK_MAX 2.5f
#define SPRAY_DRIP_THRESHOLD 1.0f
#define SPRAY_DRIP_MAX_LEN 4.0f
#define SPRAY_DRIP_SPEED 0.55f      /* world units / s per unit of thickness over (threshold-0.15) */
#define SPRAY_DRIP_COST 0.16f       /* thickness consumed per world unit of drip */

static const float SPRAY_COLORS[SPRAY_COLOR_COUNT_ENGINE][3] = {
    {0.00f, 0.90f, 1.00f},  /* cyan */
    {1.00f, 0.92f, 0.10f},  /* yellow */
    {1.00f, 0.10f, 0.75f},  /* magenta */
    {0.96f, 0.96f, 0.96f},  /* white */
    {0.04f, 0.04f, 0.04f},  /* black */
    {0.50f, 0.50f, 0.52f},  /* grey */
    {0.82f, 0.71f, 0.55f},  /* tan */
};
static inline const char *spray_color_name(int c) {
    static const char *n[SPRAY_COLOR_COUNT_ENGINE] = {"CYAN", "YELLOW", "MAGENTA", "WHITE", "BLACK", "GREY", "TAN"};
    return n[(c < 0 || c >= SPRAY_COLOR_COUNT_ENGINE) ? 0 : c];
}

typedef struct {
    float x, y, z;       /* centre on the surface */
    float nx, ny, nz;    /* surface normal */
    float radius;        /* world units */
    float thickness;     /* 0..SPRAY_THICK_MAX: drives opacity and dripping */
    float drip_len;      /* current drip length (world units), runs straight down */
    float drip_max;      /* floor/obstacle stop, <0 = not yet probed */
    float drip_w;        /* drip half-width */
    int color;           /* index into SPRAY_COLORS */
    int scene_id;        /* -2 = tombstone */
} SprayDab;

typedef struct {
    SprayDab dabs[SPRAY_DAB_MAX];
    int count;
    int next;
    unsigned int rng;
} SprayPaintBuf;

/* trace(ox,oy,oz, tx,ty,tz, &hx,&hy,&hz,&nx,&ny,&nz) -> 1 on hit, same contract as trace_map. */
typedef int (*SprayTraceFn)(float, float, float, float, float, float,
                            float *, float *, float *, float *, float *, float *);

static inline unsigned int spray_rand(SprayPaintBuf *b) {
    unsigned int x = b->rng ? b->rng : 0xA341316Cu;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    b->rng = x;
    return x;
}
static inline float spray_randf(SprayPaintBuf *b) { /* [-1,1) */
    return ((float)(spray_rand(b) & 0xFFFFu) / 32768.0f) - 1.0f;
}

static inline void spray_clear(SprayPaintBuf *b) { b->count = 0; b->next = 0; }

static inline int spray_color_clamp(int c) { return (c < 0 || c >= SPRAY_COLOR_COUNT_ENGINE) ? 0 : c; }

/* Deposit amount for one ray that landed `dist` units away: quadratic falloff, so close-up
   spraying is several times as heavy as spraying across the room. */
static inline float spray_deposit_amount(float dist) {
    float f = 1.0f - dist / SPRAY_RANGE;
    if (f < 0.0f) f = 0.0f;
    return SPRAY_THICK_RATE * (0.25f + 3.0f * f * f);
}

/* Dab footprint: wider the farther away (a real spray cone spreads), tight when held close. */
static inline float spray_dab_radius(float dist) {
    return 0.18f + dist * 0.045f;
}

static inline void spray_deposit(SprayPaintBuf *b, int color, int scene_id,
                                 float hx, float hy, float hz, float nx, float ny, float nz, float dist) {
    float amt = spray_deposit_amount(dist);
    float rad = spray_dab_radius(dist);
    color = spray_color_clamp(color);
    SprayDab *best = NULL;
    float best_d = 1e9f;
    for (int i = 0; i < b->count; i++) {
        SprayDab *d = &b->dabs[i];
        if (d->scene_id != scene_id || d->color != color) continue;
        if (d->nx * nx + d->ny * ny + d->nz * nz < 0.9f) continue;
        float dx = hx - d->x, dy = hy - d->y, dz = hz - d->z;
        float plane = fabsf(dx * d->nx + dy * d->ny + dz * d->nz);
        if (plane > 0.06f) continue;
        float l2 = dx * dx + dy * dy + dz * dz;
        float reach = 0.55f * (d->radius > rad ? d->radius : rad);
        if (l2 > reach * reach || l2 >= best_d) continue;
        best = d; best_d = l2;
    }
    if (best) {
        best->thickness += amt;
        if (best->thickness > SPRAY_THICK_MAX) best->thickness = SPRAY_THICK_MAX;
        if (best->radius < rad) best->radius += (rad - best->radius) * 0.15f;
        return;
    }
    SprayDab *d = &b->dabs[b->next % SPRAY_DAB_MAX];
    memset(d, 0, sizeof(*d));
    d->x = hx; d->y = hy; d->z = hz; d->nx = nx; d->ny = ny; d->nz = nz;
    d->radius = rad; d->thickness = amt * 4.0f; /* a fresh dab starts a little translucent, builds up */
    d->drip_max = -1.0f; d->drip_w = 0.035f + 0.02f * (spray_randf(b) + 1.0f) * 0.5f;
    d->color = color; d->scene_id = scene_id;
    b->next++;
    if (b->count < SPRAY_DAB_MAX) b->count++;
}

/* One spray tick from an eye position along a unit forward vector. */
static inline int spray_fire(SprayPaintBuf *b, SprayTraceFn trace, int color, int scene_id,
                             float ex, float ey, float ez, float fx, float fy, float fz) {
    float ux = 0.0f, uy = 1.0f, uz = 0.0f;
    if (fabsf(fy) > 0.98f) { uy = 0.0f; uz = 1.0f; }
    float rx = fy * uz - fz * uy, ry = fz * ux - fx * uz, rz = fx * uy - fy * ux;
    float rl = sqrtf(rx * rx + ry * ry + rz * rz);
    if (rl < 0.0001f) return 0;
    rx /= rl; ry /= rl; rz /= rl;
    float vx = ry * fz - rz * fy, vy = rz * fx - rx * fz, vz = rx * fy - ry * fx;
    float tan_a = tanf(SPRAY_HALF_ANGLE_DEG * 0.0174533f);
    int hits = 0;
    for (int r = 0; r < SPRAY_RAYS; r++) {
        /* uniform point in the cone's unit disc */
        float ang = (spray_randf(b) + 1.0f) * 3.14159265f;
        float rr = sqrtf((spray_randf(b) + 1.0f) * 0.5f) * tan_a;
        float dx = fx + (rx * cosf(ang) + vx * sinf(ang)) * rr;
        float dy = fy + (ry * cosf(ang) + vy * sinf(ang)) * rr;
        float dz = fz + (rz * cosf(ang) + vz * sinf(ang)) * rr;
        float l = sqrtf(dx * dx + dy * dy + dz * dz);
        dx /= l; dy /= l; dz /= l;
        float hx, hy, hz, nx, ny, nz;
        if (!trace(ex, ey, ez, ex + dx * SPRAY_RANGE, ey + dy * SPRAY_RANGE, ez + dz * SPRAY_RANGE,
                   &hx, &hy, &hz, &nx, &ny, &nz)) continue;
        float qx = hx - ex, qy = hy - ey, qz = hz - ez;
        float dist = sqrtf(qx * qx + qy * qy + qz * qz);
        spray_deposit(b, color, scene_id, hx, hy, hz, nx, ny, nz, dist);
        hits++;
    }
    return hits;
}

/* Advance wet paint: drips on near-vertical surfaces. */
static inline void spray_step(SprayPaintBuf *b, SprayTraceFn trace, float dt) {
    if (dt <= 0.0f) return;
    if (dt > 0.1f) dt = 0.1f;
    for (int i = 0; i < b->count; i++) {
        SprayDab *d = &b->dabs[i];
        if (d->scene_id < 0) continue;
        if (fabsf(d->ny) > 0.5f) continue;                  /* floors / ceilings pool, they don't run */
        if (d->thickness <= SPRAY_DRIP_THRESHOLD && d->drip_len <= 0.0f) continue;
        if (d->thickness <= SPRAY_DRIP_THRESHOLD * 0.85f) continue; /* ran dry: the streak stays */
        if (d->drip_max < 0.0f) {
            float hx, hy, hz, nx, ny, nz;
            d->drip_max = SPRAY_DRIP_MAX_LEN;
            if (trace && trace(d->x + d->nx * 0.05f, d->y, d->z + d->nz * 0.05f,
                               d->x + d->nx * 0.05f, d->y - SPRAY_DRIP_MAX_LEN - 0.1f, d->z + d->nz * 0.05f,
                               &hx, &hy, &hz, &nx, &ny, &nz)) {
                float room = d->y - hy - 0.05f;
                if (room < 0.0f) room = 0.0f;
                if (room < d->drip_max) d->drip_max = room;
            }
        }
        if (d->drip_len >= d->drip_max) continue;
        float speed = SPRAY_DRIP_SPEED * (d->thickness - 0.15f);
        float adv = speed * dt;
        if (d->drip_len + adv > d->drip_max) adv = d->drip_max - d->drip_len;
        d->drip_len += adv;
        d->thickness -= adv * SPRAY_DRIP_COST;
    }
}

/* Drop every dab inside a destroyed brick cell (same contract as bullet_hole_remove_in_box). */
static inline int spray_remove_in_box(SprayPaintBuf *b, float cx, float cy, float cz,
                                      float hw, float hh, float hd) {
    const float margin = 0.15f;
    int removed = 0;
    for (int i = 0; i < b->count; i++) {
        SprayDab *d = &b->dabs[i];
        if (d->scene_id < 0) continue;
        if (d->x >= cx - hw - margin && d->x <= cx + hw + margin &&
            d->y >= cy - hh - margin && d->y <= cy + hh + margin &&
            d->z >= cz - hd - margin && d->z <= cz + hd + margin) {
            d->scene_id = -2;
            removed++;
        }
    }
    return removed;
}

#endif
