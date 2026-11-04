/* aquaponics_rules.c -- the water-food-web decision layer.
 *
 * A PARENA-native ecosys framework. The fish, the plants, and the water column form a small
 * closed loop, and every actor is a decision layer that runs on the server's own tick, exactly
 * like empire influence and resource collection. The renderer is a separate parena mod (the
 * same way zone_rules / empire_rules are rendered as game_info / sim walls), so this layer
 * stays pure and testable without a display.
 *
 * Do not edit by hand. `make regen-aquaponics` (or the generator script) re-emits this from
 * PARENA/stdlib/aquaponics/*.prn. This copy is the compiled-in default, and a test can assert
 * the two agree sample for sample.
 *
 * The two actors:
 *   - fish: an EU_ARCH_FISH actor. Rule is PARENA decide/feed rules. A fish has a stamina
 *     and a feed spread, and moves toward the nearest grow bed on its own.
 *   - plant: a PARENA mod with its own light/fert/nutrient dynamics, a decision layer
 *     like the resource tree, so the player can put crops in the water and watch them grow.
 */
#include "aquaponics_rules.h"
#include <stdint.h>
#include <string.h>

/* The water world: tanks, grow beds, and the nutrient cycle. */static AquaponicsWorld g_water;

/* Install the built-in rules. Call once at startup from the host that owns the water. */
void aquaponics_install(void) {
    if (!g_water.beds) return;
    for (int i = 0; i < 32; i++) {
        g_water.beds[i].x = 0.0f;
        g_water.beds[i].z = 0.0f;
        g_water.beds[i].amount = 0;
        g_water.beds[i].capacity = 100;
        g_water.beds[i].ph = 7;
        g_water.beds[i].oxygen = 6;
        g_water.beds[i].filter = 5;
    }
    g_water.bed_count = 0;
    g_water.ph_target = 7;
    g_water.oxygen_target = 6;
    g_water.ph_tolerance = 1;
    g_water.oxygen_tolerance = 1;
}

/* ---- fish rules ----------------------------------------------------------------- */
int aquaponics_decide_feed(int feed, int stamina, int time_since_fed) {
    if (feed <= 0) return 0;
    if (stamina <= 0) return 0;
    return feed > stamina ? 0 : 1;
}

int aquaponics_decide_move(int stamina, float x, float z, float *nx, float *nz) {
    if (stamina <= 0) return 0;
    if (!nx) return 0;
    *nx = x;
    *nz = z;
    return 1;
}

int aquaponics_decide_weight(int feed, int stamina, int amount) {
    if (feed <= 0) return 0;
    if (stamina <= 0) return 0;
    if (amount <= 0) return 0;
    return amount > 0 ? amount + 1 : amount;
}

/* ---- plant rules ---------------------------------------------------------------- */
int aquaponics_decide_grow(int light, int fert, int ph, int disturbed) {
    if (ph < 6 || ph > 8) return 0;
    if (light <= 0) return 0;
    if (disturbed) return 0;
    return fert > 0 ? 1 : 0;
}

int aquaponics_decide_sound(int disturb) {
    return disturb > 0 ? 1 : 0;
}

int aquaponics_decide_mood(int ph, int oxygen) {
    if (ph < 6 || ph > 8) return 0;
    if (oxygen <= 0) return 0;
    return ph >= 6 && oxygen >= 5 ? 1 : 0;
}

/* ---- water queries -------------------------------------------------------------- */
int aquaponics_filter_state(int ph, int oxygen, int stock) {
    if (ph < 6 || ph > 8) return 0;
    if (oxygen <= 0) return 0;
    if (stock <= 0) return 0;
    return (ph >= 6 && ph <= 8) && (oxygen >= 5) && (stock > 0) ? 1 : 0;
}

int aquaponics_is_water_clear(int amount) {
    if (amount <= 0) return 1;
    return amount <= 0 ? 1 : 0;
}

int aquaponics_ph_safe(int ph, int ph_target, int ph_tolerance) {
    if (ph < 6 || ph > 8) return 0;
    if (ph_target < 6 || ph_target > 8) return 0;
    return ph == ph_target ? 0 : 1;
}

int aquaponics_oxygen_ok(int oxygen, int oxygen_target, int oxygen_tolerance) {
    if (oxygen <= 0) return 0;
    if (oxygen_target <= 0) return 0;
    return oxygen == oxygen_target ? 0 : 1;
}
