/* gun_items_test.c -- card #487. Hand-derived.
 * gcc -Wall -Wextra -Werror -I. -o /tmp/gun_items_test packages/simulation/gun_items_test.c packages/simulation/gun_items.c -lm */
#include <stdio.h>
#include <math.h>
#include "gun_items.h"
#include "../common/protocol.h"

static int fails = 0;
#define CHECK(c, m) do { if (c) printf("PASS: %s\n", m); else { printf("FAIL: %s\n", m); fails++; } } while (0)

int main(void) {
    gun_items_reset();
    CHECK(gun_items_active_count() == 0 && gun_items_check(0, 0, 0) == -1, "empty world: nothing to pick up");

    /* loot table bands: 0-29 magnum, 30-54 AR, 55-74 shotgun, 75-84 katana, 85-94 sniper, 95-99 missile */
    CHECK(gun_loot_pick(0) == WPN_MAGNUM && gun_loot_pick(29) == WPN_MAGNUM && gun_loot_pick(30) == WPN_AR, "magnum/AR boundary at 30");
    CHECK(gun_loot_pick(54) == WPN_AR && gun_loot_pick(55) == WPN_SHOTGUN && gun_loot_pick(74) == WPN_SHOTGUN, "AR/shotgun boundary at 55");
    CHECK(gun_loot_pick(75) == WPN_KATANA && gun_loot_pick(84) == WPN_KATANA && gun_loot_pick(85) == WPN_SNIPER, "katana/sniper boundary at 85");
    CHECK(gun_loot_pick(94) == WPN_SNIPER && gun_loot_pick(95) == WPN_MISSILE && gun_loot_pick(99) == WPN_MISSILE, "sniper/missile boundary at 95");
    CHECK(gun_loot_pick(100) == WPN_MAGNUM && gun_loot_pick(-1) == WPN_MISSILE, "out-of-range rolls wrap");
    int counts[MAX_WEAPONS] = {0};
    for (int r = 0; r < 100; r++) counts[gun_loot_pick(r)]++;
    CHECK(counts[WPN_MAGNUM] == 30 && counts[WPN_AR] == 25 && counts[WPN_SHOTGUN] == 20 &&
          counts[WPN_KATANA] == 10 && counts[WPN_SNIPER] == 10 && counts[WPN_MISSILE] == 5, "table weights are 30/25/20/10/10/5 over 100 rolls");

    /* pickup reach: radius 2.2 horizontal, +-2.5 vertical */
    int s = gun_items_drop(WPN_SHOTGUN, 10.0f, 5.0f, 10.0f);
    CHECK(s == 0 && gun_items_active_count() == 1, "drop fills slot 0");
    CHECK(gun_items_check(13.0f, 5.0f, 10.0f) == -1, "3 units away: out of reach");
    CHECK(gun_items_check(10.0f, 9.0f, 10.0f) == -1, "4 units above: out of reach");
    CHECK(gun_items_check(11.5f, 6.0f, 10.0f) == WPN_SHOTGUN, "1.5 away, 1 up: picked up");
    CHECK(gun_items_active_count() == 0 && gun_items_check(10.0f, 5.0f, 10.0f) == -1, "a collected gun is gone");

    /* nearest wins */
    gun_items_drop(WPN_AR, 0.0f, 0.0f, 2.0f);
    gun_items_drop(WPN_SNIPER, 0.0f, 0.0f, 1.0f);
    CHECK(gun_items_check(0.0f, 0.0f, 0.0f) == WPN_SNIPER && gun_items_check(0.0f, 0.0f, 0.0f) == WPN_AR, "nearest first, then the other");

    /* pool exhaustion */
    gun_items_reset();
    int last = 0; for (int i = 0; i < GUN_ITEM_MAX; i++) last = gun_items_drop(WPN_MAGNUM, 100.0f + i, 0, 0);
    CHECK(last == GUN_ITEM_MAX - 1 && gun_items_drop(WPN_MAGNUM, 0, 0, 0) == -1, "full pool refuses a drop");

    /* ring seeding: every loot type appears once at count 6; all within the ring radii */
    gun_items_reset();
    CHECK(gun_items_seed_ring(0, 0, 0, 6) == 6, "ring places 6");
    int seen[MAX_WEAPONS] = {0};
    for (int i = 0; i < 6; i++) { const GunItem *g = gun_items_get(i); seen[g->weapon]++; }
    CHECK(seen[WPN_MAGNUM] == 1 && seen[WPN_AR] == 1 && seen[WPN_SHOTGUN] == 1 && seen[WPN_KATANA] == 1 && seen[WPN_SNIPER] == 1 && seen[WPN_MISSILE] == 1,
          "ring holds one of each of the six loot weapons");

    /* road seeding: 6 guns, all >= r0-pitch/2 from origin, each on a road centreline (284 pitch -> 142 + k*284) */
    gun_items_reset();
    CHECK(gun_items_seed_roads(0, 0, 0, 6, 140.0f, 120.0f, 284.0f) == 6, "road ring places 6");
    int on_road = 1, far_enough = 1;
    for (int i = 0; i < 6; i++) {
        const GunItem *g = gun_items_get(i);
        float fx = fmodf(fabsf(g->x), 284.0f), fz = fmodf(fabsf(g->z), 284.0f);
        if (fabsf(fx - 142.0f) > 0.01f && fabsf(fz - 142.0f) > 0.01f) on_road = 0;
        if (sqrtf(g->x * g->x + g->z * g->z) < 100.0f) far_enough = 0;
    }
    CHECK(on_road && far_enough, "every road-seeded gun sits on a road centreline, well out from the spawn");

    /* enemy death drops exactly once, re-arms on respawn */
    gun_items_reset();
    CHECK(gun_items_note_enemy(3, 1, 0, 0, 0, 0, 0) == -1, "alive enemy drops nothing");
    int d = gun_items_note_enemy(3, 0, 7.0f, 0.0f, 8.0f, 34, 60);
    CHECK(d >= 0 && gun_items_get(d)->weapon == WPN_SHOTGUN && gun_items_get(d)->x == 7.0f, "dead enemy, roll 34 < 35: drops loot_roll 60 = shotgun there");
    CHECK(gun_items_note_enemy(3, 0, 7.0f, 0.0f, 8.0f, 0, 0) == -1 && gun_items_active_count() == 1, "still dead next tick: no second drop");
    gun_items_note_enemy(3, 1, 0, 0, 0, 99, 0);
    CHECK(gun_items_note_enemy(3, 0, 1.0f, 0.0f, 1.0f, 35, 0) == -1 && gun_items_active_count() == 1, "re-armed; roll 35 is NOT under the 35% threshold");
    CHECK(gun_items_note_enemy(-1, 0, 0, 0, 0, 0, 0) == -1 && gun_items_note_enemy(GUN_ITEM_MAX, 0, 0, 0, 0, 0, 0) == -1, "out-of-range enemy slot ignored");

    printf(fails ? "\n%d FAILED\n" : "\nAll gun_items checks passed.\n", fails);
    return fails ? 1 : 0;
}
