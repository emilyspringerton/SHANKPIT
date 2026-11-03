/* shield_packs_test.c -- card #541's world pool: the once-per-death latch, the 25% roll, the pool
 * cap, nearest-pickup within reach, and the untrusted client mirror. Pure data, no SDL/GL, so this
 * runs anywhere. The Makefile referenced this file since #541 but it was never committed, which made
 * `make test-shield-packs` fail on a missing source rather than a failing check.
 * make test-shield-packs */
#include "shield_packs.h"

#include <math.h>
#include <stdio.h>

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

int main(void) {
    unsigned v;

    /* an empty world */
    shield_packs_reset();
    CHECK(shield_packs_active_count() == 0, "a reset pool still holds packs");
    CHECK(shield_packs_get(-1) == NULL && shield_packs_get(SHIELD_PACK_MAX) == NULL,
          "get() past the pool should be NULL");

    /* a living player never drops */
    CHECK(shield_packs_note_player(0, 1, 1, 2, 3, 0) == -1, "an alive player dropped a pack");

    /* the first dead tick drops, at the body's position */
    int slot = shield_packs_note_player(0, 0, 10.0f, 2.0f, -5.0f, 0);
    CHECK(slot == 0, "the first dead tick returned %d, not a slot", slot);
    CHECK(shield_packs_active_count() == 1, "the drop did not land in the pool");
    {
        const ShieldPack *p = shield_packs_get(slot);
        CHECK(p && p->active && p->x == 10.0f && p->y == 2.0f && p->z == -5.0f,
              "the pack is not where the player died");
    }

    /* the latch: the same death does not keep dropping every tick */
    CHECK(shield_packs_note_player(0, 0, 10.0f, 2.0f, -5.0f, 0) == -1, "a second dead tick dropped again");
    CHECK(shield_packs_active_count() == 1, "the latch did not hold");

    /* re-armed only by being alive again */
    CHECK(shield_packs_note_player(0, 1, 10.0f, 2.0f, -5.0f, 0) == -1, "reviving dropped a pack");
    CHECK(shield_packs_note_player(0, 0, 10.0f, 2.0f, -5.0f, SHIELD_PACK_DROP_PERCENT - 1) >= 0,
          "a re-armed slot at roll 24 should drop");

    /* the roll boundary: 25% means 0..24 drop and 25 does not */
    int before = shield_packs_active_count();
    CHECK(shield_packs_note_player(1, 0, 0, 0, 0, SHIELD_PACK_DROP_PERCENT) == -1,
          "roll 25 dropped a pack, and the chance is 25%%");
    CHECK(shield_packs_active_count() == before, "a refused roll changed the pool");
    CHECK(shield_packs_note_player(1, 0, 0, 0, 0, 0) == -1,
          "the latch should stay set after a refused roll");

    /* slots outside the tracked range are refused, not written */
    CHECK(shield_packs_note_player(-1, 0, 0, 0, 0, 0) == -1, "slot -1 dropped a pack");
    CHECK(shield_packs_note_player(SHIELD_PLAYER_SLOTS, 0, 0, 0, 0, 0) == -1, "slot past the last dropped a pack");

    /* the pool cap: 16 packs, and the 17th death is refused cleanly */
    shield_packs_reset();
    for (int i = 0; i < SHIELD_PACK_MAX; i++)
        CHECK(shield_packs_note_player(i, 0, (float)i, 0, 0, 0) == i, "pack %d did not take slot %d", i, i);
    CHECK(shield_packs_active_count() == SHIELD_PACK_MAX, "the pool did not fill");
    CHECK(shield_packs_note_player(SHIELD_PACK_MAX, 0, 0, 0, 0, 0) == -1, "a full pool still dropped a pack");
    CHECK(shield_packs_active_count() == SHIELD_PACK_MAX, "a refused drop changed a full pool");

    /* pickup: nearest within reach, and the pack leaves the pool */
    shield_packs_reset();
    shield_packs_note_player(0, 0, 0, 1, 0, 0);
    shield_packs_note_player(1, 0, 0, 1, 10, 0);
    CHECK(shield_packs_take(0, 1, 0.5f) == 1, "a pack underfoot was not picked up");
    CHECK(shield_packs_get(0)->active == 0, "the near pack stayed in the pool");
    CHECK(shield_packs_get(1)->active == 1, "the far pack was taken instead of the near one");
    CHECK(shield_packs_take(0, 1, 0.5f) == 0, "the same pack was taken twice");

    /* out of reach horizontally and vertically */
    shield_packs_reset();
    shield_packs_note_player(0, 0, 5, 5, 5, 0);
    CHECK(shield_packs_take(5 + SHIELD_PACK_RADIUS + 0.2f, 5, 5) == 0, "picked up past the radius");
    CHECK(shield_packs_take(5, 5 + SHIELD_PACK_HEIGHT + 0.2f, 5) == 0, "picked up past the height window");
    CHECK(shield_packs_take(5, 5, 5) == 1, "could not pick up a pack within reach");

    /* version: every change is observable to a client that only sees it */
    shield_packs_reset();
    v = shield_packs_version();
    shield_packs_note_player(0, 0, 1, 1, 1, 0);
    CHECK(shield_packs_version() != v, "a drop did not bump the version");
    v = shield_packs_version();
    shield_packs_take(1, 1, 1);
    CHECK(shield_packs_version() != v, "a pickup did not bump the version");

    /* the client mirror replaces the whole set and treats the packet as untrusted input */
    shield_packs_reset();
    shield_packs_note_player(0, 0, 7, 7, 7, 0);
    {
        float three[9] = { 1, 2, 3, 4, 5, 6, NAN, 8, 9 };
        CHECK(shield_packs_set_all(three, 3) == 2, "a non-finite coordinate was not skipped");
        CHECK(shield_packs_active_count() == 2, "the mirror did not replace the old set");
        CHECK(shield_packs_get(0)->x == 1.0f && shield_packs_get(1)->y == 5.0f, "mirrored coordinates are wrong");
    }
    {
        float many[SHIELD_PACK_MAX * 3 + 9];
        for (int i = 0; i < SHIELD_PACK_MAX + 3; i++) { many[i * 3] = (float)i; many[i * 3 + 1] = 0; many[i * 3 + 2] = 0; }
        CHECK(shield_packs_set_all(many, SHIELD_PACK_MAX + 3) == SHIELD_PACK_MAX, "the mirror did not clamp to the pool");
        CHECK(shield_packs_active_count() == SHIELD_PACK_MAX, "the clamped mirror holds the wrong count");
    }
    CHECK(shield_packs_set_all(NULL, 5) == 0, "a null mirror should empty the set");
    CHECK(shield_packs_active_count() == 0, "a null mirror left packs behind");

    printf("shield_packs: %d checks, %d failed\n", checks, fails);
    if (fails) return 1;
    printf("shield_packs: all checks passed\n");
    return 0;
}
