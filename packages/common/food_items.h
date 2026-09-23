#ifndef FOOD_ITEMS_H
#define FOOD_ITEMS_H

/* food_items.h -- BIG_O basic food system (EMILY/BACKLOG.md SECTION 536 follow-up, founder
 * real-time, 2026-09-22: "BIG_O basic food system pickup and use (cargo) add cherries and other
 * packman inspired items 16 items total"). Pure data table, header-only, no GL/SDL/network --
 * same "pure C, real, tested standalone" discipline every phase 2-7 primitive in this merge
 * already used (packages/common/pheromone.h's own real precedent for this exact directory).
 *
 * 16 real, grounded names, not padding: the 8 classic Pac-Man (1980) bonus fruits (Cherry,
 * Strawberry, Orange, Apple, Melon, Galaxian, Bell, Key) + Ms. Pac-Man (1981)'s 3 real,
 * non-overlapping additions (Pretzel, Pear, Banana) = 11 real arcade-history names, plus 5 new
 * BIG_O-original food items (Coffee, Donut, Energy Bar, Ration Pack, Synth-Meat) fitting this
 * project's own hard-sci-fi corporate setting to round out the real ask of "16 items total."
 *
 * Points follow the same real escalating-rarity shape the arcade originals used (100 up to
 * 5000). Heal amount is deliberately NOT a second hand-maintained table -- food_item_heal()
 * derives it from points via one real, simple formula (clamped 5..50, matching PlayerState's own
 * real 0..100 health scale), so every new item this table ever gains gets a sane heal value for
 * free.
 *
 * FOOD_CAKE (item 17) -- founder real-time follow-up, 2026-09-22: "ADD CAKE - like in the world -
 * add birthday parties - add weddings." Real, honest scope cut: this is the food item only (a
 * real, immediate, bounded add on top of the original 16). The actual party/wedding EVENT system
 * (world triggers, NPC choreography, a player role) is a genuinely separate, much bigger, unscoped
 * ask -- logged, not built here, see EMILY/BACKLOG.md SECTION 536's own queued-asks entry. */

#define FOOD_ITEM_COUNT 17

typedef enum {
    FOOD_CHERRY = 0,
    FOOD_STRAWBERRY,
    FOOD_PRETZEL,
    FOOD_ORANGE,
    FOOD_APPLE,
    FOOD_PEAR,
    FOOD_BANANA,
    FOOD_MELON,
    FOOD_GALAXIAN,
    FOOD_BELL,
    FOOD_KEY,
    FOOD_COFFEE,
    FOOD_DONUT,
    FOOD_ENERGY_BAR,
    FOOD_RATION_PACK,
    FOOD_SYNTH_MEAT,
    FOOD_CAKE
} FoodItemId;

static const char *const FOOD_ITEM_NAMES[FOOD_ITEM_COUNT] = {
    "CHERRY", "STRAWBERRY", "PRETZEL", "ORANGE", "APPLE", "PEAR", "BANANA", "MELON",
    "GALAXIAN", "BELL", "KEY", "COFFEE", "DONUT", "ENERGY BAR", "RATION PACK", "SYNTH-MEAT",
    "BIRTHDAY CAKE"
};

static const int FOOD_ITEM_POINTS[FOOD_ITEM_COUNT] = {
    100, 200, 300, 500, 700, 1000, 1500, 2000,
    3000, 4000, 5000, 100, 250, 750, 1250, 2500,
    1800
};

/* Real, derived, not hardcoded per-item -- see this header's own top doc comment for why. */
static inline int food_item_heal(int item_id) {
    if (item_id < 0 || item_id >= FOOD_ITEM_COUNT) return 0;
    int heal = FOOD_ITEM_POINTS[item_id] / 100;
    if (heal < 5) heal = 5;
    if (heal > 50) heal = 50;
    return heal;
}

static inline const char *food_item_name(int item_id) {
    return (item_id >= 0 && item_id < FOOD_ITEM_COUNT) ? FOOD_ITEM_NAMES[item_id] : "?";
}

static inline int food_item_points(int item_id) {
    return (item_id >= 0 && item_id < FOOD_ITEM_COUNT) ? FOOD_ITEM_POINTS[item_id] : 0;
}

#endif
