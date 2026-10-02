/* city_geo_test.c -- headless checks for the restored SCENE_CITY geometry (card T62892945).
 *   make test-city */
#include "physics.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

PlayerState *get_best_bot(void) { return NULL; }
void evolve_bot(PlayerState *a, PlayerState *b) { (void)a; (void)b; }
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

int main(void) {
    phys_set_scene(SCENE_CITY);
    CHECK(phys_scene_id == SCENE_CITY);
    CHECK(map_geo == map_geo_city && map_count == map_geo_city_count);
    CHECK(map_count > 300 && map_count < CITY_MAX_BOXES);       /* a real city, inside the cap */
    CHECK(map_geo[0].h == 4.0f && map_geo[0].y == -2.0f);         /* the ground slab is box 0 */

    /* idempotent: a second set_scene does not regenerate */
    int n = map_count;
    phys_set_scene(SCENE_CITY);
    CHECK(map_count == n);

    /* every spawn slot lands clear of every box's solid volume (horizontally inside a box whose
       top is above the spawn height would be buried) */
    for (int slot = 0; slot < 12; slot++) {
        float x, y, z;
        scene_spawn_point(SCENE_CITY, slot, &x, &y, &z);
        CHECK(y == 6.0f);
        int buried = 0;
        for (int i = 1; i < map_count; i++) {
            Box b = map_geo[i];
            if (fabsf(x - b.x) < b.w * 0.5f && fabsf(z - b.z) < b.d * 0.5f &&
                y > b.y - b.h * 0.5f && y < b.y + b.h * 0.5f) { buried = 1; break; }
        }
        CHECK(!buried);
    }

    /* bounds: below the kill plane respawns; beyond the soft edge pushes back toward the middle */
    PlayerState p; memset(&p, 0, sizeof(p));
    p.scene_id = SCENE_CITY; p.id = 0;
    p.x = 100.0f; p.y = CITY_KILL_Y - 1.0f; p.z = 100.0f;
    scene_safety_check(&p);
    CHECK(p.y > CITY_KILL_Y);
    memset(&p, 0, sizeof(p)); p.scene_id = SCENE_CITY; p.x = CITY_SOFT_X + 100.0f; p.y = 5.0f;
    scene_safety_check(&p);
    CHECK(p.vx < 0.0f);

    if (g_fail) { printf("city_geo_test: %d FAILED\n", g_fail); return 1; }
    printf("city_geo_test: all checks passed (%d boxes)\n", n);
    return 0;
}
