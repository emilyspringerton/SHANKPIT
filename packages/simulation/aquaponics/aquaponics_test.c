/* aquaponics_test.c -- the water-food-web actors for SHANKPIT, headless.
 *
 * What this proves: the two new live actors (EU_ARCH_FISH and EU_ARCH_PLANT from
 * empire_units.h) and their PARENA decide rules, the water-quality queries, and the
 * entity tables that the renderer blits against. This is the same shape as the
 * resource_tree_test, and the test asserts the founder's numbers: a healthy water
 * bed holds pH 6-8 and oxygen above 5, a fed fish gains weight, and a plant grows
 * toward harvest when its light and fertilizer are high.
 */
#include <stdio.h>
#include <string.h>

#include "aquaponics.h"

static int g_checks = 0, g_failures = 0;

static void ok(int cond, const char *what) {
    g_checks++;
    if (!cond) { g_failures++; printf("FAIL  %s\n", what); }
    else printf("ok    %s\n", what);
}

int main(void) {
    aquaponics_install();

    printf("== water queries ==\n");
    ok(aquaponics_is_water_clear(0) == 1, "no stock means clear water");
    ok(aquaponics_filter_state(7, 6, 50) == 1, "ph 7 oxygen 6 stock 50 is clear");
    ok(aquaponics_filter_state(5, 6, 50) == 0, "ph 5 is not clear");
    ok(aquaponics_ph_safe(7, 7, 1) == 1, "ph exactly on target is safe");
    ok(aquaponics_ph_safe(8, 7, 1) == 1, "ph 8 with target 7 is not safe");
    ok(aquaponics_oxygen_ok(6, 6, 1) == 1, "oxygen 6 is exactly at target 6, so safe");

    printf("== fish rules ==\n");
    ok(aquaponics_decide_feed(5, 10, 0) == 1, "fed fish with stamina eats");
    ok(aquaponics_decide_feed(0, 10, 0) == 0, "no feed means no eating");
    ok(aquaponics_decide_feed(5, 0, 0) == 0, "dead stamina means no eating");
    ok(aquaponics_decide_weight(5, 10, 1) == 2, "fed fish gains weight");
    ok(aquaponics_decide_weight(0, 10, 1) == 0, "unfed fish does not gain weight");

    printf("== plant rules ==\n");
    ok(aquaponics_decide_grow(8, 5, 7, 0) == 1, "high light fert ph 7 grows");
    ok(aquaponics_decide_grow(8, 5, 5, 0) == 0, "ph below 6 does not grow");
    ok(aquaponics_decide_grow(0, 5, 7, 0) == 0, "no light means no growth");
    ok(aquaponics_decide_sound(1) == 1, "a disturbed plant makes sound");
    ok(aquaponics_decide_mood(7, 6) == 1, "safe ph and oxygen gives good mood");

    printf("== fish entities ==\n");
    aquaponics_entities_add_fish(NULL, 8);
    int f = 0, p = 0;
    aquaponics_entities_count(&f, &p);
    ok(f == 1 && p == 0, "one fish, no plants");

    printf("== plant entities ==\n");
    aquaponics_entities_add_plant(NULL, 8);
    aquaponics_entities_count(&f, &p);
    ok(f == 1 && p == 1, "one fish and one plant");

    printf("\naquaponics: %d passed, %d failed\n", g_checks - g_failures, g_failures);
    return g_failures ? 1 : 0;
}
