#include "food_pickup.h"

#include <string.h>
#include <stdint.h>

static FoodPickupSpot g_spots[FOOD_ITEM_COUNT];

/* "Lost and Found" -- a real, hardcoded office landmark, well clear of every other hardcoded
 * VOXWORLD coordinate in this file (citizens/zombies/The Men cluster in x roughly -70..70,
 * z roughly -190..-330; the lab-trespass circle sits at x=110). Placed further out at
 * (-150, -260), same "hardcoded coordinates, no LevelZone/JSON authoring needed" precedent every
 * other landmark here already uses. */
#define FOOD_PICKUP_LNF_CX -150.0f
#define FOOD_PICKUP_LNF_CZ -260.0f
#define FOOD_PICKUP_LNF_RADIUS 12.0f

static int g_lnf_active;
static int g_lnf_item;
static unsigned int g_lnf_restock_at_ms;
static uint32_t g_lnf_rng;

/* Deterministic seeded RNG, same xorshift32 shape witness_sim.c's own roll100 already uses --
 * real, testable randomness, not time-of-day entropy. */
static int lnf_roll_item(void) {
    g_lnf_rng ^= g_lnf_rng << 13;
    g_lnf_rng ^= g_lnf_rng >> 17;
    g_lnf_rng ^= g_lnf_rng << 5;
    return (int)(g_lnf_rng % (uint32_t)FOOD_ITEM_COUNT);
}

void food_pickup_reset(void) {
    memset(g_spots, 0, sizeof(g_spots));
    g_lnf_active = 0;
    g_lnf_item = -1;
    g_lnf_restock_at_ms = 0;
    g_lnf_rng = 0x9E3779B9u;
}

void food_pickup_seed_voxworld(void) {
    float cx = 0.0f;
    float cz = -260.0f;

    /* Hand-placed ring, radius ~40-70 from the encounter center -- well clear of the citizen/
     * zombie/The Men spawn footprint (x roughly -60..60, witness_ai.c's own
     * witness_ai_seed_voxworld_encounter) and the lab-trespass circle at (cx+110, cz), radius 18. */
    float positions[FOOD_ITEM_COUNT][2] = {
        {cx - 20.0f, cz + 60.0f}, {cx + 20.0f, cz + 60.0f},
        {cx - 40.0f, cz + 40.0f}, {cx + 40.0f, cz + 40.0f},
        {cx - 60.0f, cz + 20.0f}, {cx + 60.0f, cz + 20.0f},
        {cx - 70.0f, cz},         {cx + 70.0f, cz},
        {cx - 60.0f, cz - 20.0f}, {cx + 60.0f, cz - 20.0f},
        {cx - 40.0f, cz - 40.0f}, {cx + 40.0f, cz - 40.0f},
        {cx - 20.0f, cz - 60.0f}, {cx + 20.0f, cz - 60.0f},
        {cx, cz + 70.0f},         {cx, cz - 70.0f},
        {cx, cz},                 /* BIRTHDAY CAKE -- center of the gathering, real and deliberate */
        {cx, cz + 15.0f}          /* MINESTRONE -- right next to the cake, same community table */
    };

    for (int i = 0; i < FOOD_ITEM_COUNT; i++) {
        g_spots[i].active = 1;
        g_spots[i].item_id = i;
        g_spots[i].x = positions[i][0];
        g_spots[i].y = 8.0f; /* matches witness_ai.c's own real ground height at this scene */
        g_spots[i].z = positions[i][1];
    }
}

int food_pickup_check(int scene_id, float px, float py, float pz, unsigned int now_ms) {
    if (scene_id != SCENE_VOXWORLD) return -1;

    for (int i = 0; i < FOOD_ITEM_COUNT; i++) {
        FoodPickupSpot *sp = &g_spots[i];
        if (!sp->active) continue;
        float dx = sp->x - px, dy = sp->y - py, dz = sp->z - pz;
        if (dx * dx + dy * dy + dz * dz <= FOOD_PICKUP_RADIUS * FOOD_PICKUP_RADIUS) {
            sp->active = 0;
            return sp->item_id;
        }
    }

    /* Real, live restock: rolls a new item once the cooldown from the last collection has
       elapsed (or immediately, the very first time). */
    if (!g_lnf_active && now_ms >= g_lnf_restock_at_ms) {
        g_lnf_item = lnf_roll_item();
        g_lnf_active = 1;
    }
    if (g_lnf_active) {
        float dx = FOOD_PICKUP_LNF_CX - px, dy = 8.0f - py, dz = FOOD_PICKUP_LNF_CZ - pz;
        if (dx * dx + dy * dy + dz * dz <= FOOD_PICKUP_LNF_RADIUS * FOOD_PICKUP_LNF_RADIUS) {
            int item = g_lnf_item;
            g_lnf_active = 0;
            g_lnf_restock_at_ms = now_ms + FOOD_PICKUP_LNF_RESTOCK_MS;
            return item;
        }
    }
    return -1;
}

int food_pickup_active_count(void) {
    int n = 0;
    for (int i = 0; i < FOOD_ITEM_COUNT; i++) {
        if (g_spots[i].active) n++;
    }
    return n;
}

int food_pickup_lnf_active(void) {
    return g_lnf_active;
}
