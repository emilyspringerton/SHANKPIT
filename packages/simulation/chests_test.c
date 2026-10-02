/* chests_test.c -- card #528. Hand-derived.
 * Built by `make test-chests` (links chest_rules.c and gun_items.c). */
#include <stdio.h>
#include <math.h>
#include "chests.h"
#include "gun_items.h"
#include "../common/protocol.h"

static int fails = 0;
#define CHECK(c, m) do { if (c) printf("PASS: %s\n", m); else { printf("FAIL: %s\n", m); fails++; } } while (0)

int main(void) {
    chests_reset(); gun_items_reset();
    CHECK(chests_active_count() == 0, "empty pool");
    int a = chests_place(0, 10.0f, 0.0f, 0.0f);   /* wooden: 100 hp, box x 9.1..10.9, y 0..1.2, z -0.6..0.6 */
    CHECK(a == 0 && chests_get(a)->hp == 100 && chests_get(a)->max_hp == 100, "wooden chest has 100 hp");
    CHECK(chests_get(chests_place(1, 30, 0, 0))->hp == 240 && chests_get(chests_place(2, 50, 0, 0))->hp == 420, "reinforced 240, rare 420");
    CHECK(chests_get(chests_place(9, 70, 0, 0))->tier == 2, "tier clamps to 2");

    int broke = 7;
    CHECK(chests_hit_ray(0, 0.6f, 0, 0, 0, -1, 50.0f, WPN_AR, 20, 0, &broke) == -1 && broke == 0, "ray the other way misses");
    CHECK(chests_hit_ray(0, 2.0f, 0, 1, 0, 0, 50.0f, WPN_AR, 20, 0, &broke) == -1, "ray passing over the top (y=2 > 1.2) misses");
    CHECK(chests_hit_ray(0, 0.6f, 0, 1, 0, 0, 5.0f, WPN_AR, 20, 0, &broke) == -1, "range 5 stops short of x=9.1");
    int h = chests_hit_ray(0, 0.6f, 0, 1, 0, 0, 50.0f, WPN_AR, 20, 0, &broke);
    CHECK(h == a && broke == 0 && chests_get(a)->hp == 80, "AR round (20 x1.0) leaves 80 hp");
    /* the ray also passes the chests behind it, but only the nearest takes the hit */
    CHECK(chests_get(1)->hp == 240, "the chest behind the first is untouched");
    for (int i = 0; i < 4; i++) chests_hit_ray(0, 0.6f, 0, 1, 0, 0, 50.0f, WPN_AR, 20, 0, &broke);
    CHECK(chests_get(a)->hp == 0 && broke == 1 && !chests_get(a)->active, "five AR rounds (100 hp) break it");
    CHECK(gun_items_active_count() == 1, "the broken chest dropped a gun");
    /* wooden roll 0 -> magnum (1) */
    CHECK(gun_items_check(10.0f, 0.0f, 0.0f) == WPN_MAGNUM, "wooden roll 0 drops a magnum where the chest stood");

    /* the next ray now reaches the reinforced chest at x=30 (box 29.1..30.9) */
    h = chests_hit_ray(0, 0.6f, 0, 1, 0, 0, 50.0f, WPN_KATANA, 40, 55, &broke);
    CHECK(h == 1 && chests_get(1)->hp == 240 - 60, "katana 40 x1.5 = 60 off the reinforced chest");
    h = chests_hit_ray(0, 0.6f, 0, 1, 0, 0, 50.0f, WPN_MISSILE, 130, 0, &broke);
    CHECK(broke == 1 && gun_items_check(30.0f, 0.0f, 0.0f) == WPN_SHOTGUN, "missile 130 x2.0 = 260 >= 180 hp left: breaks; tier 1 roll 0 -> shotgun");

    /* blast: rare chest at x=50 (box 49.1..50.9). Centre 3 away from the box face with radius 2: out; radius 4: in. */
    CHECK(chests_hit_blast(46.1f, 0.5f, 0, 2.0f, WPN_MISSILE, 100, 0) == 0 && chests_get(2)->hp == 420, "blast radius 2, 3 units from the face: no damage");
    CHECK(chests_hit_blast(46.1f, 0.5f, 0, 4.0f, WPN_MISSILE, 100, 0) == 0 && chests_get(2)->hp == 220, "blast radius 4 reaches: 100 x2.0 = 200 off");
    CHECK(chests_hit_blast(46.1f, 0.5f, 0, 4.0f, WPN_MISSILE, 100, 0) == 0 && chests_get(2)->hp == 20, "second blast leaves 20 hp");
    CHECK(chests_hit_blast(46.1f, 0.5f, 0, 4.0f, WPN_MISSILE, 100, 40) == 1 && !chests_get(2)->active, "third blast breaks it");
    CHECK(gun_items_check(50.0f, 0.0f, 0.0f) == WPN_SNIPER, "rare chest: roll 40 + slot 2*37 = 114 wraps to 14 (<40) -> sniper");
    printf("%s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
