/* hammer_test.c -- WPN_HAMMER (#447): a swing hurts a player in reach, strikes the wall in front through
 * the map-damage surface hook (only within HAMMER_REACH), has no ammo, and has a slow swing. make test-hammer */
#include "physics.h"
#include <stdio.h>
#include <string.h>

PlayerState *get_best_bot(void) { return NULL; }
void evolve_bot(PlayerState *a, PlayerState *b) { (void)a; (void)b; }
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static int g_hook_calls, g_hook_weapon, g_hook_damage;
static float g_hook_z;
static void surface_hook(int scene, float hx, float hy, float hz, float nx, float ny, float nz, int damage, int weapon) {
    (void)scene; (void)hx; (void)hy; (void)nx; (void)ny; (void)nz;
    g_hook_calls++; g_hook_weapon = weapon; g_hook_damage = damage; g_hook_z = hz;
}

static void setup(PlayerState *ps) {
    memset(ps, 0, sizeof(PlayerState) * 2);
    for (int i = 0; i < 2; i++) {
        ps[i].active = 1; ps[i].state = STATE_ALIVE; ps[i].health = 100; ps[i].shield = 0; ps[i].id = i;
        ps[i].scene_id = SCENE_CITY; ps[i].team_id = -1;
    }
    ps[0].current_weapon = WPN_HAMMER;
}

int main(void) {
    CHECK(MAX_WEAPONS > WPN_HAMMER);
    CHECK(WPN_STATS[WPN_HAMMER].id == WPN_HAMMER);
    CHECK(WPN_STATS[WPN_HAMMER].rof > WPN_STATS[WPN_KNIFE].rof);      /* a slow swing */
    CHECK(WPN_STATS[WPN_HAMMER].ammo_max == 0);

    phys_set_scene(SCENE_CITY);
    static Box floor_only[1];
    map_geo = floor_only; map_count = 1;

    /* 1. player in reach gets hurt, swing sets the slow cooldown, no ammo is consumed or needed */
    static PlayerState ps[MAX_CLIENTS];
    setup(ps);
    ps[1].x = 0; ps[1].y = 0; ps[1].z = -3.0f;                       /* attacker at origin, yaw 0 faces -Z */
    update_weapons(&ps[0], ps, NULL, 1, 0, 0, 1000, 3000);
    CHECK(ps[1].health == 100 - WPN_STATS[WPN_HAMMER].dmg);
    CHECK(ps[0].attack_cooldown == WPN_STATS[WPN_HAMMER].rof);
    CHECK(ps[0].ammo[WPN_HAMMER] == 0 && ps[0].reload_timer == 0);

    /* 2. cooldown: an immediate second swing does nothing */
    int hp = ps[1].health;
    update_weapons(&ps[0], ps, NULL, 1, 0, 0, 1016, 3000);
    CHECK(ps[1].health == hp);

    /* 3. the wall: a box 5 units ahead is struck through the surface hook with the hammer's weapon id */
    setup(ps);
    static Box wall_near[2];
    wall_near[1] = (Box){0, 2, -5.0f - 0.5f, 20, 10, 1.0f};         /* face at z = -5 */
    map_geo = wall_near; map_count = 2;
    g_hook_calls = 0;
    phys_set_map_damage_hooks(NULL, NULL, surface_hook);
    update_weapons(&ps[0], ps, NULL, 1, 0, 0, 2000, 3000);
    CHECK(g_hook_calls == 1);
    CHECK(g_hook_weapon == WPN_HAMMER);
    CHECK(g_hook_damage == WPN_STATS[WPN_HAMMER].dmg);
    CHECK(g_hook_z < -4.9f && g_hook_z > -5.1f);                     /* struck the near face */

    /* 4. out of reach: a wall 20 units away is not touched */
    setup(ps);
    static Box wall_far[2];
    wall_far[1] = (Box){0, 2, -20.0f - 0.5f, 20, 10, 1.0f};
    map_geo = wall_far; map_count = 2;
    g_hook_calls = 0;
    update_weapons(&ps[0], ps, NULL, 1, 0, 0, 3000, 3000);
    CHECK(g_hook_calls == 0);

    /* 5. no hook installed (a client): swinging at a wall is just a swing, not a crash */
    phys_set_map_damage_hooks(NULL, NULL, NULL);
    setup(ps);
    map_geo = wall_near; map_count = 2;
    update_weapons(&ps[0], ps, NULL, 1, 0, 0, 4000, 3000);
    CHECK(ps[0].attack_cooldown == WPN_STATS[WPN_HAMMER].rof);

    if (g_fail) { printf("hammer_test: %d FAILED\n", g_fail); return 1; }
    printf("hammer_test OK\n");
    return 0;
}
