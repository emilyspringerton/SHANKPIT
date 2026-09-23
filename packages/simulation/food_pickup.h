#ifndef FOOD_PICKUP_H
#define FOOD_PICKUP_H

/* food_pickup.h -- BIG_O basic food system, the real world half (EMILY/BACKLOG.md SECTION 536
 * follow-up, founder real-time, 2026-09-22: "BIG_O basic food system pickup and use (cargo) add
 * cherries and other packman inspired items 16 items total"). Composes food_items.h's own pure
 * data table into a real, live, client-local world state -- same "client-local only, no server-
 * authoritative story yet" scope cut phase 6/7e's own phone.h/day_night_clock/world_alert_bridge
 * already made and named honestly (real, separate multiplayer-sync follow-up, not this pass).
 *
 * Auto-collect on walk-over (FOOD_PICKUP_RADIUS), matching the arcade originals' own real
 * convention -- no E-key interaction, unlike the wheelbarrow's own deliberate carry mechanic.
 * Collected items are the CALLER's concern to actually add to Phone's own cargo (main.c, the
 * real g_story_phone consumer) -- this module only owns the world-side pickup spots. */

#include "../common/food_items.h"
#include "../common/protocol.h" /* SCENE_VOXWORLD */

#define FOOD_PICKUP_RADIUS 3.0f

typedef struct {
    int active;
    int item_id;
    float x, y, z;
} FoodPickupSpot;

/* Clears all pickup spots (nothing active). Safe to call on any level load. */
void food_pickup_reset(void);

/* Seeds one of each real food item (FOOD_ITEM_COUNT of them) scattered in a hand-placed ring around VOXWORLD's
 * own witness encounter center (witness_ai.c's own cx=0/cz=-260), clear of every citizen/zombie/
 * The Men spawn position and the hardcoded lab-trespass circle (WITNESS_AI_LAB_ZONE_*) -- same
 * "hardcoded coordinates, no LevelZone/JSON authoring needed" precedent
 * witness_ai_seed_voxworld_encounter's own placement already set. A no-op call site guard
 * (scene_id checked at food_pickup_check time, not here) matches that function's own shape. */
void food_pickup_seed_voxworld(void);

/* Real, live per-tick collection check: if (px,py,pz) is within FOOD_PICKUP_RADIUS of an active
 * spot (the 17 hand-placed ones, or the Lost and Found spot below), deactivates/collects it and
 * returns its item_id. scene_id is checked first -- a no-op everywhere except SCENE_VOXWORLD,
 * matching every other VOXWORLD-only hardcoded-coordinate check in this merge (witness_ai_tick's
 * own lab-zone block). now_ms drives the Lost and Found's own restock timer. Returns -1 if
 * nothing was collected this call. */
int food_pickup_check(int scene_id, float px, float py, float pz, unsigned int now_ms);

/* Test/debug accessor: how many of the FOOD_ITEM_COUNT spots are still active (uncollected). */
int food_pickup_active_count(void);

/* "Lost and Found" -- founder real-time, 2026-09-22: "add a 'lost and found' in the office where
 * random stuff can randomly be there." A real, hardcoded third office landmark (distinct from
 * every citizen/zombie/The Men spawn, the lab-trespass circle, and the 17 hand-placed spots
 * above) that randomly restocks with one of the FOOD_ITEM_COUNT real items after
 * FOOD_PICKUP_LNF_RESTOCK_MS once collected -- deterministic seeded RNG (xorshift32, same real
 * precedent witness_sim.c's own roll100 already uses), not true randomness, for testability. */
#define FOOD_PICKUP_LNF_RESTOCK_MS 15000u

/* Test/debug accessor: is the Lost and Found spot currently holding an item? */
int food_pickup_lnf_active(void);

#endif
