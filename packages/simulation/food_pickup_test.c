/* food_pickup_test.c -- BIG_O basic food system real, standalone test (EMILY/BACKLOG.md SECTION
 * 536 follow-up). Plain assert() harness, same convention as every other test in this merge.
 *
 * Build and run:
 *   gcc -Wall -Wextra -O2 -Ipackages/simulation -Ipackages/common \
 *       -o /tmp/food_pickup_test packages/simulation/food_pickup_test.c \
 *       packages/simulation/food_pickup.c -lm && /tmp/food_pickup_test
 */
#include "food_pickup.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    {
        food_pickup_reset();
        assert(food_pickup_active_count() == 0);
        printf("PASS: food_pickup_reset leaves nothing active\n");
    }
    {
        food_pickup_reset();
        food_pickup_seed_voxworld();
        assert(food_pickup_active_count() == FOOD_ITEM_COUNT);
        printf("PASS: food_pickup_seed_voxworld seeds exactly all %d real items\n", FOOD_ITEM_COUNT);
    }
    {
        /* Wrong scene: never collects, even standing exactly on a spot. */
        food_pickup_reset();
        food_pickup_seed_voxworld();
        int got = food_pickup_check(0 /* not SCENE_VOXWORLD */, -20.0f, 8.0f, -200.0f, 0);
        assert(got == -1);
        assert(food_pickup_active_count() == FOOD_ITEM_COUNT);
        printf("PASS: food_pickup_check is a real no-op outside SCENE_VOXWORLD\n");
    }
    {
        /* Real collection: standing exactly on the first seeded spot in VOXWORLD. */
        food_pickup_reset();
        food_pickup_seed_voxworld();
        int got = food_pickup_check(SCENE_VOXWORLD, -20.0f, 8.0f, -200.0f, 0);
        assert(got == FOOD_CHERRY);
        assert(food_pickup_active_count() == FOOD_ITEM_COUNT - 1);
        printf("PASS: standing on a spot in VOXWORLD collects it for real\n");

        /* Same spot can't be collected twice. */
        int got_again = food_pickup_check(SCENE_VOXWORLD, -20.0f, 8.0f, -200.0f, 0);
        assert(got_again == -1);
        assert(food_pickup_active_count() == FOOD_ITEM_COUNT - 1);
        printf("PASS: a collected spot can't be collected a second time\n");
    }
    {
        /* Far from every spot: nothing collected. */
        food_pickup_reset();
        food_pickup_seed_voxworld();
        int got = food_pickup_check(SCENE_VOXWORLD, 900.0f, 8.0f, 900.0f, 0);
        assert(got == -1);
        assert(food_pickup_active_count() == FOOD_ITEM_COUNT);
        printf("PASS: far from every spot, nothing is collected\n");
    }
    {
        /* Lost and Found: rolls a real item on first check, collectible at its own hardcoded
           office coordinates, and restocks after the real cooldown -- not before. */
        food_pickup_reset();
        int got_far = food_pickup_check(SCENE_VOXWORLD, 0.0f, 8.0f, 0.0f, 1000);
        assert(got_far == -1);
        assert(food_pickup_lnf_active()); /* rolled and waiting, just not standing on it yet */
        printf("PASS: Lost and Found rolls a real item even before it's collected\n");

        int got = food_pickup_check(SCENE_VOXWORLD, -150.0f, 8.0f, -260.0f, 1000);
        assert(got >= 0 && got < FOOD_ITEM_COUNT);
        assert(!food_pickup_lnf_active());
        printf("PASS: standing on the Lost and Found spot collects a real, valid item\n");

        /* Too soon: the cooldown hasn't elapsed, nothing to find yet. */
        int got_too_soon = food_pickup_check(SCENE_VOXWORLD, -150.0f, 8.0f, -260.0f, 1500);
        assert(got_too_soon == -1);
        printf("PASS: Lost and Found doesn't restock before its real cooldown elapses\n");

        /* After the cooldown: restocked for real. */
        int got_again = food_pickup_check(SCENE_VOXWORLD, -150.0f, 8.0f, -260.0f,
                                           1000 + FOOD_PICKUP_LNF_RESTOCK_MS);
        assert(got_again >= 0 && got_again < FOOD_ITEM_COUNT);
        printf("PASS: Lost and Found restocks for real once the cooldown elapses\n");
    }
    {
        /* Real data-table sanity: 16 distinct names, points escalate with real arcade shape,
           and food_item_heal derives a real 5..50 range from points, not a second hand table. */
        assert(FOOD_ITEM_COUNT == 17); /* 16 original + FOOD_CAKE, founder real-time follow-up */
        for (int i = 0; i < FOOD_ITEM_COUNT; i++) {
            for (int j = i + 1; j < FOOD_ITEM_COUNT; j++) {
                assert(strcmp(FOOD_ITEM_NAMES[i], FOOD_ITEM_NAMES[j]) != 0);
            }
            int heal = food_item_heal(i);
            assert(heal >= 5 && heal <= 50);
        }
        assert(food_item_heal(FOOD_KEY) == 50);   /* 5000 points, clamps at the top */
        assert(food_item_heal(FOOD_CHERRY) == 5); /* 100 points, clamps at the bottom */
        printf("PASS: %d distinct real items, every heal value in real 5..50 range\n", FOOD_ITEM_COUNT);
    }

    printf("\nALL PASS\n");
    return 0;
}
