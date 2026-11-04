/* aquaponics.h -- the water-food-web actors for SHANKPIT.
 *
 * Adds two live actors to the empire model so the world can grow food instead of just gather it:
 *   - EU_ARCH_FISH: a live water creature. A PARENA decide/feed rule. A fish has stamina and
 *     feed spread, and moves toward the nearest grow bed on its own.
 *   - EU_ARCH_PLANT: a PARENA mod with its own light/fert/nutrient dynamics, a decision layer
 *     like the resource tree, so the player can put crops in the water and watch them grow.
 *
 * Class breakdown (a parena mod is the rule, a game actor is the character):
 *   - fish: a water creature. Rule is the aquaponics decide/feed rules. Stamina, feed spread,
 *     growth, and a full tank.
 *   - plant: a PARENA mod with its own light/fert/nutrient dynamics. A decision layer.
 *   - water bed: a tank with ph, oxygen, and stock. The unit the fish and plants live in.
 *
 * Actor archetypes (the same EU_ARCH_* system the empire model uses):
 *   - EU_ARCH_FISH (6) replaces EU_ARCH_ZOMBIE in a water world. It is rule, by PARENA decide/feed.
 *   - EU_ARCH_PLANT (7) is a new actor, a PARENA mod with its own light/fert/nutrient dynamics.
 *
 * Actor entity: choices in a menu
 *   - fish: a water creature. Rule is PARENA decide/feed. Stamina, feed spread, growth, and a full
 *     tank. A fish is a decision layer, so it runs on the server tick and is rendered by the
 *     parena mod that hosts it (the same way game_info / sim walls work).
 *   - plant: a PARENA mod with its own light/fert/nutrient dynamics. A decision layer like the
 *     resource tree, so the player can put crops in the water and watch them grow.
 *
 * Class breakdown (all by PARENA):
 *   - WaterBed: a tank with stock, ph, oxygen, and filter.
 *   - AquaponicsWorld: tanks, grow beds, and the nutrient cycle.
 *   - FishState: health, weight, feed load, time since last fed.
 *   - PlantState: height, health, days-to-harvest, nutrients spent.
 *   - AquaponicsEntities: fish states, plant states.
 */
#ifndef SHANKPIT_AQUAPONICS_H
#define SHANKPIT_AQUAPONICS_H

#include <stddef.h>

/* Archetypes added to the empire model. */#define EU_ARCH_FISH 6
#define EU_ARCH_PLANT 7
#define EU_ARCH_COUNT 8

/* A water bed is a tank with stock, ph, oxygen, and the filter. */typedef struct {
    float x, z;
    int amount;           /* stock (fish, plants, nutrients) */
    int capacity;
    int ph;
    int oxygen;
    int filter;           /* how well the filter is running */
} WaterBed;

/* The aquaponics world: tanks, grow beds, and the nutrient cycle. */typedef struct {
    WaterBed beds[32];
    int bed_count;
    int ph_target;
    int oxygen_target;
    int ph_tolerance;
    int oxygen_tolerance;
} AquaponicsWorld;

/* The fish's state: health, weight, feed load, time since last fed. */typedef struct {
    int stamina;
    int feed;
    int time_since_fed;
    int weight;
} FishState;

/* A plant's state: height, health, days-to-harvest, nutrients spent. */typedef struct {
    int height;
    int health;
    int days_to_harvest;
    int nutrients;
} PlantState;

/* The aquaponics world's fish states. */typedef struct {
    FishState fish[16];
    int fish_count;
    PlantState plants[16];
    int plant_count;
} AquaponicsEntities;

/* Install the built-in water rules. Call once at startup from the host that owns the water. */void aquaponics_install(void);

/* Water world queries. */int aquaponics_filter_state(int ph, int oxygen, int stock);
int aquaponics_is_water_clear(int amount);
int aquaponics_ph_safe(int ph, int ph_target, int ph_tolerance);
int aquaponics_oxygen_ok(int oxygen, int oxygen_target, int oxygen_tolerance);

/* Fish rules. */int aquaponics_decide_feed(int feed, int stamina, int time_since_fed);
int aquaponics_decide_move(int stamina, float x, float z, float *nx, float *nz);
int aquaponics_decide_weight(int feed, int stamina, int amount);

/* Plant rules. */int aquaponics_decide_grow(int light, int fert, int ph, int disturbed);
int aquaponics_decide_sound(int disturb);
int aquaponics_decide_mood(int ph, int oxygen);

/* Aquaponics entities. */int aquaponics_entities_count(int *fish, int *plant);
int aquaponics_entities_add_fish(int *fish, int max);
int aquaponics_entities_add_plant(int *plant, int max);

#endif /* SHANKPIT_AQUAPONICS_H */
