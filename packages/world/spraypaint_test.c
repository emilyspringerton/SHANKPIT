/* Headless test for spraypaint.h: cone coverage, thickness vs distance, drips, tombstones.
   cc -O1 -Wall -Wextra -o /tmp/sp_test packages/world/spraypaint_test.c -lm */
#include <stdio.h>
#include <stdlib.h>
#include "spraypaint.h"

static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); fails++; } else printf("ok:   %s\n", m); } while (0)

/* A single wall at z = -W_Z facing +z (normal (0,0,1)), a floor at y = 0. */
static float W_Z = 3.0f;
static int trace_wall(float ox, float oy, float oz, float tx, float ty, float tz,
                      float *hx, float *hy, float *hz, float *nx, float *ny, float *nz) {
    float best = 2.0f; int kind = 0;
    float dz = tz - oz, dy = ty - oy;
    if (dz < 0.0f) { float t = (-W_Z - oz) / dz; if (t >= 0.0f && t <= 1.0f && t < best) { best = t; kind = 1; } }
    if (dy < 0.0f) { float t = (0.0f - oy) / dy; if (t >= 0.0f && t <= 1.0f && t < best) { best = t; kind = 2; } }
    if (!kind) return 0;
    *hx = ox + (tx - ox) * best; *hy = oy + dy * best; *hz = oz + dz * best;
    if (kind == 1) { *nx = 0; *ny = 0; *nz = 1; } else { *nx = 0; *ny = 1; *nz = 0; }
    return 1;
}

static float total_thick(SprayPaintBuf *b) { float s = 0; for (int i = 0; i < b->count; i++) s += b->dabs[i].thickness; return s; }
static float max_drip(SprayPaintBuf *b) { float m = 0; for (int i = 0; i < b->count; i++) if (b->dabs[i].drip_len > m) m = b->dabs[i].drip_len; return m; }

int main(void) {
    static SprayPaintBuf b;
    /* 1. cone paints the wall, dabs stay inside the cone footprint */
    spray_clear(&b);
    for (int i = 0; i < 30; i++) spray_fire(&b, trace_wall, 0, 1, 0.0f, 5.0f, 0.0f, 0, 0, -1);
    CHECK(b.count > 0, "spraying a wall leaves dabs");
    float maxr = 0;
    for (int i = 0; i < b.count; i++) { float r = sqrtf(b.dabs[i].x * b.dabs[i].x + (b.dabs[i].y - 5) * (b.dabs[i].y - 5)); if (r > maxr) maxr = r; }
    float cone_r = (3.0f) * tanf(SPRAY_HALF_ANGLE_DEG * 0.0174533f);
    CHECK(maxr <= cone_r + 0.01f, "all hits lie inside the cone footprint");
    CHECK(b.dabs[0].nz > 0.99f, "dabs carry the wall normal");

    /* 2. closer + longer = thicker: 1 second at 1.5 units vs 1 second at 10 units */
    SprayPaintBuf *n = (SprayPaintBuf *)calloc(1, sizeof(*n));
    SprayPaintBuf *f = (SprayPaintBuf *)calloc(1, sizeof(*f));
    W_Z = 1.5f; for (int i = 0; i < 30; i++) spray_fire(n, trace_wall, 0, 1, 0, 5, 0, 0, 0, -1);
    W_Z = 10.0f; for (int i = 0; i < 30; i++) spray_fire(f, trace_wall, 0, 1, 0, 5, 0, 0, 0, -1);
    float pn = total_thick(n) / (float)n->count, pf = total_thick(f) / (float)f->count;
    printf("      per-dab thickness near=%.3f far=%.3f (dabs %d / %d)\n", pn, pf, n->count, f->count);
    CHECK(pn > 3.0f * pf, "close-range spray is much thicker per spot than far spray");
    CHECK(n->count < f->count, "close spray concentrates on fewer, thicker dabs");

    /* 3. holding close on a spot drips; a brief or distant spray does not */
    W_Z = 1.5f;
    spray_clear(n);
    for (int i = 0; i < 90; i++) { spray_fire(n, trace_wall, 0, 1, 0, 5, 0, 0, 0, -1); spray_step(n, trace_wall, 0.033f); }
    float drip_hold = max_drip(n);
    printf("      held 3s close: max drip %.2f, dabs %d\n", drip_hold, n->count);
    CHECK(drip_hold > 0.2f, "holding spray close on a spot makes it drip");
    spray_clear(f);
    W_Z = 10.0f;
    for (int i = 0; i < 90; i++) { spray_fire(f, trace_wall, 0, 1, 0, 5, 0, 0, 0, -1); spray_step(f, trace_wall, 0.033f); }
    CHECK(max_drip(f) == 0.0f, "far spray does not drip");
    W_Z = 1.5f; spray_clear(n);
    for (int i = 0; i < 6; i++) { spray_fire(n, trace_wall, 0, 1, 0, 5, 0, 0, 0, -1); spray_step(n, trace_wall, 0.033f); }
    for (int i = 0; i < 90; i++) spray_step(n, trace_wall, 0.033f);
    CHECK(max_drip(n) == 0.0f, "a quick tap does not drip");

    /* 4. drips run dry and stop above the floor */
    spray_clear(n);
    for (int i = 0; i < 150; i++) { spray_fire(n, trace_wall, 0, 1, 0, 2.0f, 0, 0, 0, -1); spray_step(n, trace_wall, 0.033f); }
    for (int i = 0; i < 600; i++) spray_step(n, trace_wall, 0.033f);
    float dr = max_drip(n);
    int floor_ok = 1;
    for (int i = 0; i < n->count; i++) if (n->dabs[i].y - n->dabs[i].drip_len < -0.001f) floor_ok = 0;
    CHECK(dr > 0.5f && floor_ok, "drips run but are stopped by the floor below them");
    float before = dr; for (int i = 0; i < 100; i++) spray_step(n, trace_wall, 0.033f);
    CHECK(max_drip(n) == before, "drip settles (paint ran dry), does not run forever");

    /* 5. floors don't drip */
    spray_clear(n);
    for (int i = 0; i < 90; i++) { spray_fire(n, trace_wall, 0, 1, 0, 1.0f, 0, 0, -1, 0); spray_step(n, trace_wall, 0.033f); }
    CHECK(n->count > 0 && max_drip(n) == 0.0f, "paint on the floor pools, no drip");

    /* 6. tombstone */
    spray_clear(n); W_Z = 3.0f;
    for (int i = 0; i < 10; i++) spray_fire(n, trace_wall, 0, 1, 0, 5, 0, 0, 0, -1);
    int rm = spray_remove_in_box(n, 0, 5, -3, 10, 10, 1);
    CHECK(rm == n->count && rm > 0, "destroyed-brick removal tombstones dabs");
    free(n); free(f);
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
