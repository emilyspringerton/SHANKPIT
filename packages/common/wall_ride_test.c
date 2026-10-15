/* wall_ride_test.c -- crouch-slide wall ride (airborne + ctrl held + against a wall magnets you to it,
   keeps momentum, lets go when ctrl is released / speed bleeds off). make test-wall-ride */
#include "physics.h"
#include <stdio.h>

PlayerState *get_best_bot(void) { return NULL; }
void evolve_bot(PlayerState *a, PlayerState *b) { (void)a; (void)b; }
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static PlayerState mk(float x, float y, float z, float vz, int crouch) {
    PlayerState p; memset(&p, 0, sizeof p);
    p.active = 1; p.x = x; p.y = y; p.z = z; p.vz = vz; p.crouching = crouch;
    return p;
}

int main(void) {
    phys_set_scene(SCENE_CITY);
    static Box geo[3];
    geo[1] = (Box){5000, 10, 0, 1, 20, 400};   /* long wall, x face at 5000.5, running along z */
    map_geo = geo; map_count = 2;

    float x_touch = 5000.5f + PLAYER_WIDTH + 0.05f;  /* just off the +x face */
    float nx, nz;

    PlayerState p = mk(x_touch, 8, 0, -0.9f, 1);
    CHECK(phys_wall_ride_probe(&p, &nx, &nz) && nx > 0.9f && fabsf(nz) < 0.01f);
    CHECK(phys_wall_ride_tick(&p) == 1);
    CHECK(p.vy == 0.0f);                              /* gravity cancelled */
    CHECK(p.vz < -0.85f);                             /* momentum kept (only the 1% slide drag) */

    /* controls: each of these must NOT ride, i.e. existing physics is untouched */
    p = mk(x_touch, 8, 0, -0.9f, 0); CHECK(phys_wall_ride_tick(&p) == 0);  /* ctrl not held */
    p = mk(x_touch, 8, 0, -0.9f, 1); p.on_ground = 1; CHECK(phys_wall_ride_tick(&p) == 0); /* grounded */
    p = mk(x_touch, 8, 0, -0.1f, 1); CHECK(phys_wall_ride_tick(&p) == 0);  /* no momentum */
    p = mk(5010, 8, 0, -0.9f, 1);    CHECK(phys_wall_ride_tick(&p) == 0);  /* no wall near */
    p = mk(x_touch, 30, 0, -0.9f, 1);CHECK(phys_wall_ride_tick(&p) == 0);  /* above the wall */

    /* full multi-tick ride incl. real collision: stays attached ~60 ticks, then bleeds off and falls */
    p = mk(x_touch, 8, 0, -0.9f, 1);
    p.vx = 0.4f;                                      /* flying into the wall: velocity into it is cancelled */
    int ridden = 0, first_fall = -1;
    float x_min = 1e9f;
    for (int t = 0; t < 300; t++) {
        int r = phys_wall_ride_tick(&p);
        if (r) ridden++; else { if (first_fall < 0) first_fall = t; p.vy -= GRAVITY_DROP; }
        p.y += p.vy;
        resolve_collision(&p);
        p.x += p.vx; p.z += p.vz;
        if (p.x < x_min) x_min = p.x;
        if (p.y < 0.5f) break;
    }
    printf("ridden %d ticks, first fall tick %d, z %.1f, y %.2f\n", ridden, first_fall, p.z, p.y);
    CHECK(ridden >= 60);                              /* real, sustained ride */
    CHECK(first_fall > 60);                           /* ...that only ends once momentum bleeds off */
    CHECK(x_min > 5000.5f + PLAYER_WIDTH - 0.1f);     /* never clipped into the wall (<= one pull step of overshoot, same as any wall contact) */
    CHECK(p.z < -50.0f);                              /* actually travelled along it */
    return g_fail;
}
