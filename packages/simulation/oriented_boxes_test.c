/* oriented_boxes_test.c -- headless end-to-end test of rotated cubes + ramps through the REAL
 * engine paths (physics.h resolve_collision / trace_map / phys_sample_ground_height) after loading a
 * NOCK-shaped level JSON through level_boxes.h.
 *
 *   gcc -std=gnu99 -Wall -fsanitize=address,undefined -Ipackages/common \
 *       -Ipackages/simulation -Ipackages/world packages/simulation/oriented_boxes_test.c \
 *       packages/world/terrain.c -lm
 */
#include "../common/physics.h"
#include "../world/level_boxes.h"
#include <stdio.h>
#include <string.h>

PlayerState *get_best_bot(void) { return NULL; }
void evolve_bot(PlayerState *a, PlayerState *b) { (void)a; (void)b; }

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

/* Box 0: ramp 20 wide x 6 tall x 30 deep at origin (rises toward +z, low edge z=-15, top z=+15).
 * Box 1: plain cube 10^3 at x=100. Box 2: same cube rotated 45 deg about Y at x=200. */
static const char *LEVEL =
 "{\"name\":\"t\",\"ground_plane_enabled\":true,\"ground_plane_squares\":40,\"walls\":["
 "{\"id\":1,\"x\":0,\"y\":3,\"z\":0,\"sx\":20,\"sy\":6,\"sz\":30,\"r\":1,\"g\":1,\"b\":1,\"friction\":0.3,\"ramp\":true},"
 "{\"id\":2,\"x\":100,\"y\":5,\"z\":0,\"sx\":10,\"sy\":10,\"sz\":10,\"r\":1,\"g\":1,\"b\":1,\"friction\":0.3},"
 "{\"id\":3,\"x\":200,\"y\":5,\"z\":0,\"sx\":10,\"sy\":10,\"sz\":10,\"r\":1,\"g\":1,\"b\":1,\"friction\":0.3,\"rot_y\":45},"
 "{\"id\":4,\"x\":-7,\"y\":3,\"z\":30,\"sx\":6,\"sy\":6,\"sz\":30,\"r\":1,\"g\":1,\"b\":1,\"friction\":0.3}"
 "]}";

static void load(void) {
    static CustomLevelData lvl;
    memset(&lvl, 0, sizeof lvl);
    CHECK(level_boxes_parse_json(LEVEL, &lvl));
    CHECK(lvl.count == 4);
    CHECK(lvl.boxes[0].ramp == 1 && lvl.boxes[1].ramp == 0);
    CHECK(lvl.boxes[2].rot_y == 45.0f);
    static float x[8], y[8], z[8], w[8], h[8], d[8], r[8], g[8], b[8], rx[8], ry[8], rz[8];
    static int mi[8];
    static unsigned char rp[8];
    for (int i = 0; i < lvl.count; i++) {
        x[i] = lvl.boxes[i].x; y[i] = lvl.boxes[i].y; z[i] = lvl.boxes[i].z;
        w[i] = lvl.boxes[i].w; h[i] = lvl.boxes[i].h; d[i] = lvl.boxes[i].d;
        r[i] = g[i] = b[i] = 1.0f; mi[i] = 0;
        rx[i] = lvl.boxes[i].rot_x; ry[i] = lvl.boxes[i].rot_y; rz[i] = lvl.boxes[i].rot_z;
        rp[i] = (unsigned char)lvl.boxes[i].ramp;
    }
    phys_set_custom_level(x, y, z, w, h, d, r, g, b, mi, lvl.count, 1, 40);
    phys_set_custom_level_orient(rx, ry, rz, rp, lvl.count);
    phys_set_scene(SCENE_CUSTOM_LEVEL);
}

static void step(PlayerState *p, float vx, float vz) {
    p->vx = vx; p->vz = vz; p->vy -= 0.05f;
    p->x += p->vx; p->y += p->vy; p->z += p->vz;
    resolve_collision(p);
}

int main(void) {
    load();
    CHECK(phys_box_orient(1) && phys_box_orient(1)->ramp);
    CHECK(phys_box_orient(2) == NULL); /* the plain cube stays on the AABB path */
    CHECK(phys_box_orient(3) && !phys_box_orient(3)->ramp);

    /* ground height over the ramp: 0 at the low edge+, 3 at the middle, ~6 at the top */
    CHECK(fabsf(phys_sample_ground_height(0, 0, NULL) - 3.0f) < 0.05f);
    CHECK(fabsf(phys_sample_ground_height(0, 10, NULL) - 5.0f) < 0.05f);
    CHECK(phys_sample_ground_height(0, -14.9f, NULL) < 0.1f);
    /* rotated cube covers its diamond: (205,0) inside (reach 7.07), (203.6,3.6) outside (|x|+|z|=7.2>7.07) */
    CHECK(fabsf(phys_sample_ground_height(205, 0, NULL) - 10.0f) < 0.05f);
    CHECK(phys_sample_ground_height(203.6f, 3.6f, NULL) < 0.1f);

    /* walk up the ramp from the low side: the player must end up standing on the top (y ~ 6) */
    PlayerState pl; memset(&pl, 0, sizeof pl);
    pl.active = 1; pl.x = 0; pl.y = 0; pl.z = -25;
    for (int i = 0; i < 400 && pl.z < 12; i++) step(&pl, 0, 0.25f);
    CHECK(pl.z >= 12.0f);
    CHECK(fabsf(pl.y - phys_sample_ground_height(pl.x, pl.z, NULL)) < 0.35f);
    CHECK(pl.y > 4.5f);
    CHECK(pl.on_ground);

    /* the ramp's top edge meets a plain cube (box 4, x -10..-4, top y=6, starts z=15): walking over the seam
       must step onto it, not stall at its face one player-width short */
    memset(&pl, 0, sizeof pl); pl.active = 1; pl.x = -7; pl.y = 0; pl.z = -25;
    for (int i = 0; i < 500 && pl.z < 25; i++) step(&pl, 0, 0.25f);
    CHECK(pl.z >= 25.0f && fabsf(pl.y - 6.0f) < 0.1f);

    /* walking into the ramp's tall back wall from +z is blocked (no tunnelling through) */
    memset(&pl, 0, sizeof pl); pl.active = 1; pl.x = 0; pl.y = 0; pl.z = 25;
    for (int i = 0; i < 400; i++) step(&pl, 0, -0.25f);
    CHECK(pl.z > 15.0f);

    /* rotated cube is a diamond (reach 7.07 along x and z): a player running straight at its tip
       must never get inside it (it slides around the faces), and one running at z=4.6 -- which an
       unrotated 10-wide cube's face at x=195 would stop dead -- is NOT stopped at x=195. */
    int inside = 0;
    memset(&pl, 0, sizeof pl); pl.active = 1; pl.x = 180; pl.y = 0; pl.z = 0;
    for (int i = 0; i < 300; i++) {
        step(&pl, 0.25f, 0);
        if (fabsf(pl.x - 200.0f) + fabsf(pl.z) < 7.07f - 0.3f && pl.y < 9.0f) inside++;
    }
    CHECK(inside == 0);
    CHECK(fabsf(pl.z) > 1.0f || pl.x < 193.0f); /* deflected sideways (or held at the tip), not tunnelled straight through */
    memset(&pl, 0, sizeof pl); pl.active = 1; pl.x = 180; pl.y = 0; pl.z = 8.0f;
    float max_x = 0;
    for (int i = 0; i < 300; i++) { step(&pl, 0.25f, 0); if (pl.x > max_x) max_x = pl.x; }
    CHECK(max_x > 215.0f); /* z=8 is clear of the diamond entirely: runs straight past */

    /* trace: a ray down onto the ramp's slope hits at the slope height with an up-tilted normal;
       a ray along the low edge just above the ground passes over the wedge's thin end */
    float hx, hy, hz, nx, ny, nz;
    CHECK(trace_map(0, 20, 0, 0, -20, 0, &hx, &hy, &hz, &nx, &ny, &nz));
    CHECK(fabsf(hy - 3.0f) < 0.05f && ny > 0.9f && nz < -0.1f);
    CHECK(!trace_map(-30, 2.0f, -14.0f, 30, 2.0f, -14.0f, &hx, &hy, &hz, &nx, &ny, &nz) ||
          (hy < 2.5f)); /* at z=-14 the slope is at y=0.2: a ray at y=2 passes over it */

    printf(g_fail ? "oriented_boxes: %d FAILURES\n" : "oriented_boxes: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
