/* aquaponics.c -- the water-food-web actors for SHANKPIT.
 *
 * Adds two live actors to the empire model so the world can grow food instead of just gather it:
 *   - fish: a live water creature. PARENA decide/feed rules. A fish has a stamina and a
 *     feed spread, and moves toward the nearest grow bed on its own.
 *   - plant: a PARENA mod with its own light/fert/nutrient dynamics. A decision layer like
 *     the resource tree, so the player can put crops in the water and watch them grow.
 *
 * The rules live in parana rules (resource_tree_rules.c), so this file is the host glue:
 * the world, the entity tables, and the queries that the apiece blings bind to.
 */
#include "aquaponics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The water world: tanks, grow beds, and the nutrient cycle. */static AquaponicsWorld g_water;
static AquaponicsEntities g_entities;

/* Install the built-in water rules. Call once at startup from the host that owns the water. */
void aquaponics_install(void) {
    (void)g_water.beds;
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

/* ---- water queries -------------------------------------------------------------- */
int aquaponics_filter_state(int ph, int oxygen, int stock) {
    if (ph < 6 || ph > 8) return 0;
    if (oxygen <= 0) return 0;
    if (stock <= 0) return 0;
    return (ph >= 6 && ph <= 8) && (oxygen >= 5) && (stock > 0) ? 1 : 0;
}

int aquaponics_is_water_clear(int amount) {
    return amount <= 0 ? 1 : 0;
}

int aquaponics_ph_safe(int ph, int ph_target, int ph_tolerance) {
    (void)ph_tolerance;
    (void)ph_target;
    if (ph < 6 || ph > 8) return 0;
    return 1;
}

int aquaponics_oxygen_ok(int oxygen, int oxygen_target, int oxygen_tolerance) {
    (void)oxygen_tolerance;
    if (oxygen <= 0) return 0;
    if (oxygen_target <= 0) return 0;
    return oxygen >= oxygen_target ? 1 : 0;
}

/* ---- fish rules ----------------------------------------------------------------- */
int aquaponics_decide_feed(int feed, int stamina, int time_since_fed) {
    (void)time_since_fed;
    if (stamina <= 0) return 0;
    if (feed <= 0) return 0;
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
    return amount + 1;
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

/* ---- aquaponics entities -------------------------------------------------------- */
int aquaponics_entities_count(int *fish, int *plant) {
    if (fish) *fish = g_entities.fish_count;
    if (plant) *plant = g_entities.plant_count;
    return g_entities.fish_count + g_entities.plant_count;
}

int aquaponics_entities_add_fish(int *fish, int max) {
    (void)fish;
    if (g_entities.fish_count >= max) return -1;
    g_entities.fish[g_entities.fish_count].stamina = 10;
    g_entities.fish[g_entities.fish_count].feed = 0;
    g_entities.fish[g_entities.fish_count].time_since_fed = 0;
    g_entities.fish[g_entities.fish_count].weight = 1;
    return g_entities.fish_count++;
}

int aquaponics_entities_add_plant(int *plant, int max) {
    (void)plant;
    if (g_entities.plant_count >= max) return -1;
    g_entities.plants[g_entities.plant_count].height = 0;
    g_entities.plants[g_entities.plant_count].health = 5;
    g_entities.plants[g_entities.plant_count].days_to_harvest = 7;
    g_entities.plants[g_entities.plant_count].nutrients = 0;
    return g_entities.plant_count++;
}
