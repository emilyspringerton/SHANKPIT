/* aquaponics_rules.h -- the water-food-web decision layer.
 *
 * A PARENA-native ecosys framework: the fish, the plants, and the water column form a small
 * closed loop, and every actor is a decision layer that runs on the server's own tick exactly
 * like empire influence and resource collection. The renderer is a separate parena mod (the
 * same way zone_rules / empire_rules are rendered as game_info/sim walls) so this layer stays
 * pure and testable without a display.
 *
 * Actors introduced:
 *   - EU_ARCH_FISH: a live water creature. Rule is PARENA decide/feed rules. A fish has
 *     stamina and a feed spread, and moves toward the nearest grow bed on its own.
 *   - EU_ARCH_PLANT: a PARENA mod with its own light/fert/nutrient dynamics, a decision layer
 *     like the resource tree, so the player can put crops in the water and watch them grow.
 *
 * Naming follows the empire model: EU_ARCH_FISH replaces EU_ARCH_ZOMBIE in a water world;
 * EU_ARCH_PLANT is a new actor. The owner/faction/species model becomes the aquaponics
 * bio-region and its ph. Every number lives in the rules layer so the PARENA host can decide
 * the same thing on server and client.
 */
#ifndef SHANKPIT_AQUAPONICS_RULES_H
#define SHANKPIT_AQUAPONICS_RULES_H

#include <stddef.h>

/* Archetypes added to the empire model. */#define EU_ARCH_FISH 6
#define EU_ARCH_PLANT 7
#define EU_ARCH_COUNT 8

/* The water world is a level: a grow bed of tanks and beds. This is the place a fish lives,
 * and the place a plant grows. A water bed is a tank with stock, ph, oxygen and the filter. */typedef struct {
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

/* The fish's state: its health, its weight, its feed load and the time since it last fed.
 * Stability and growth are what the fish rules actually decide. */typedef struct {
    int stamina;
    int feed;
    int time_since_fed;
    int weight;
} FishState;

/* A plant's state: its height, its health, its days-to-harvest and the amount of nutrients
 * it still has to spend to finish the cycle. */typedef struct {
    int height;
    int health;
    int days_to_harvest;
} PlantState;

/* The aquaponics world's fish states. */typedef struct {
    FishState fish[16];
    int fish_count;
    PlantState plants[16];
    int plant_count;
} AquaponicsEntities;

/* What the fish rules do. decide_feed: a fish decides whether to feed or rest, based on feed
 * and stamina. decide_move: a fish moves to the nearest grow bed on its own. decide_weight:
 * a fish gains weight when it is fed and rested. */int aquaponics_decide_feed(int feed, int stamina, int time_since_fed);
int aquaponics_decide_move(int stamina, float x, float z, float *nx, float *nz);
int aquaponics_decide_weight(int feed, int stamina, int amount);

/* Plant rules. decide_grow: a plant decides to grow, given light, fert and ph. decide_harvest:
 * a plant decides to harvest when it has reached full height. decide_sound: a plant makes noise
 * when it is disturbed (a bird or a hand). decide_mood: a plant decides its mood based on how
 * safely it is growing. */int aquaponics_decide_grow(int light, int fert, int ph, int disturbed);
int aquaponics_decide_sound(int disturb);
int aquaponics_decide_mood(int ph, int oxygen);

/* Aquaponics world queries. filter_state: the filter is at the bed's ph, oxygen and stock. */
int aquaponics_filter_state(int ph, int oxygen, int stock);

/* Aquaponics water queries. is_water_clear: what's in the water. ph_safe: is the ph in range.
 * oxygen_ok: is oxygen enough. */int aquaponics_is_water_clear(int amount);
int aquaponics_ph_safe(int ph, int ph_target, int ph_tolerance);
int aquaponics_oxygen_ok(int oxygen, int oxygen_target, int oxygen_tolerance);

#endif /* SHANKPIT_AQUAPONICS_RULES_H */
