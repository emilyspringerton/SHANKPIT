#ifndef SHANKPIT_VEHICLE_SALVAGE_LOOT_H
#define SHANKPIT_VEHICLE_SALVAGE_LOOT_H

/* vehicle_salvage_loot.h -- what a sheared-off part is worth, lying on the ground where it fell.
 *
 * The disenchant shape the founder asked for has two halves. The TABLE is PARENA's
 * (vehicle_salvage_rules.prn: four tiers, quantity from the part's condition). This is the half the
 * rules cannot do, because it decides what actually appears in the world, and that is a host decision.
 *
 * NO NEW INVENTORY. `packages/common/kit.h` is explicit that this engine's items are inputs it already
 * acts on, not a parallel item universe, so the four tiers map onto four EXISTING consumables -- the
 * rare overclocked logic board is the KEY, the composite mesh is a ration pack, rebar an energy bar,
 * plain scrap a coffee. That mapping is deliberately a placeholder and is named as one in the spec: a
 * real salvage economy is a decision nobody has made. The DROP still carries the tier, so when that
 * decision lands the table changes in one function and nothing on the wire moves.
 *
 * Same contract `bioslurry.h` already states, for the same reason: this is the simulation half, NOT on
 * the wire. A networked client's drops are its own; making them authoritative entities is a separate,
 * larger step. Auto-collect on walk-over, matching food_pickup.h's own arcade convention.
 *
 * Header-only with its own state on purpose: the drop list belongs to whichever process ticked the
 * part off (a binary includes local_game.h exactly once), the same way brick_world.h owns g_brick.
 */

#include <string.h>

#include "vehicle_salvage.h"      /* SALVAGE_TIER_*, vehicle_salvage_* */
#include "../common/food_items.h" /* FOOD_* -- the existing consumables a tier maps onto */

#define VS_LOOT_MAX_DROPS     24
#define VS_LOOT_PICKUP_RADIUS 3.0f
#define VS_LOOT_ROT_MS        180000u   /* an uncollected part is gone after three minutes */

typedef struct {
    int active;
    int scene_id;
    float x, y, z;
    int comp;                 /* SALVAGE_COMP_* it came off, for the HUD's own text */
    int tier;                 /* SALVAGE_TIER_* */
    int quantity;
    unsigned int dropped_ms;
} VehicleLootDrop;

static VehicleLootDrop g_vs_loot[VS_LOOT_MAX_DROPS];
static int g_vs_loot_cursor = 0;

static inline void vehicle_salvage_loot_reset(void) {
    memset(g_vs_loot, 0, sizeof(g_vs_loot));
    g_vs_loot_cursor = 0;
}

/* The one place the tier -> existing-item decision lives. */
static inline int vehicle_salvage_loot_item(int tier) {
    switch (tier) {
    case SALVAGE_TIER_LOGIC:     return FOOD_KEY;          /* 5000 points: the board worth the teardown */
    case SALVAGE_TIER_COMPOSITE: return FOOD_RATION_PACK;
    case SALVAGE_TIER_REBAR:     return FOOD_ENERGY_BAR;
    default:                     return FOOD_COFFEE;       /* scrap */
    }
}

static inline int vehicle_salvage_loot_points(int tier) {
    int item = vehicle_salvage_loot_item(tier);
    return (item >= 0 && item < FOOD_ITEM_COUNT) ? FOOD_ITEM_POINTS[item] : 0;
}

static inline const char *vehicle_salvage_loot_name(int tier) {
    int item = vehicle_salvage_loot_item(tier);
    return (item >= 0 && item < FOOD_ITEM_COUNT) ? FOOD_ITEM_NAMES[item] : "--";
}

/* Places one drop. The pool reuses a free slot first and only then overwrites the oldest, so a busy
 * demolition cannot silently lose the good part it just produced. */
static inline int vehicle_salvage_loot_drop(int scene_id, float x, float y, float z, int comp, int tier,
                                            int quantity, unsigned int now_ms) {
    if (quantity < 1) quantity = 1;
    if (quantity > 9) quantity = 9;
    if (tier < SALVAGE_TIER_SCRAP) tier = SALVAGE_TIER_SCRAP;
    if (tier > SALVAGE_TIER_LOGIC) tier = SALVAGE_TIER_LOGIC;

    for (int i = 0; i < VS_LOOT_MAX_DROPS; i++) {
        int k = (g_vs_loot_cursor + i) % VS_LOOT_MAX_DROPS;
        if (g_vs_loot[k].active) continue;
        g_vs_loot[k].active = 1;
        g_vs_loot[k].scene_id = scene_id;
        g_vs_loot[k].x = x; g_vs_loot[k].y = y; g_vs_loot[k].z = z;
        g_vs_loot[k].comp = comp;
        g_vs_loot[k].tier = tier;
        g_vs_loot[k].quantity = quantity;
        g_vs_loot[k].dropped_ms = now_ms;
        g_vs_loot_cursor = (k + 1) % VS_LOOT_MAX_DROPS;
        return k;
    }
    int k = g_vs_loot_cursor;
    g_vs_loot[k].active = 1;
    g_vs_loot[k].scene_id = scene_id;
    g_vs_loot[k].x = x; g_vs_loot[k].y = y; g_vs_loot[k].z = z;
    g_vs_loot[k].comp = comp;
    g_vs_loot[k].tier = tier;
    g_vs_loot[k].quantity = quantity;
    g_vs_loot[k].dropped_ms = now_ms;
    g_vs_loot_cursor = (k + 1) % VS_LOOT_MAX_DROPS;
    return k;
}

static inline void vehicle_salvage_loot_tick(unsigned int now_ms) {
    for (int i = 0; i < VS_LOOT_MAX_DROPS; i++) {
        if (!g_vs_loot[i].active) continue;
        if (now_ms - g_vs_loot[i].dropped_ms > VS_LOOT_ROT_MS) g_vs_loot[i].active = 0;
    }
}

/* Walk-over collection, the arcade convention food_pickup.h already uses. Returns the tier collected
 * (>= 0) or -1. *out_quantity and *out_item describe what was picked up. */
static inline int vehicle_salvage_loot_collect(int scene_id, float px, float py, float pz,
                                               int *out_quantity, int *out_item) {
    float best_d2 = VS_LOOT_PICKUP_RADIUS * VS_LOOT_PICKUP_RADIUS;
    int best = -1;
    for (int i = 0; i < VS_LOOT_MAX_DROPS; i++) {
        const VehicleLootDrop *d = &g_vs_loot[i];
        if (!d->active || d->scene_id != scene_id) continue;
        float dx = d->x - px, dy = d->y - py, dz = d->z - pz;
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 <= best_d2) { best_d2 = d2; best = i; }
    }
    if (best < 0) return -1;
    if (out_quantity) *out_quantity = g_vs_loot[best].quantity;
    if (out_item) *out_item = vehicle_salvage_loot_item(g_vs_loot[best].tier);
    int tier = g_vs_loot[best].tier;
    g_vs_loot[best].active = 0;
    return tier;
}

static inline int vehicle_salvage_loot_count(void) {
    int n = 0;
    for (int i = 0; i < VS_LOOT_MAX_DROPS; i++) if (g_vs_loot[i].active) n++;
    return n;
}

static inline const VehicleLootDrop *vehicle_salvage_loot_at(int index) {
    if (index < 0 || index >= VS_LOOT_MAX_DROPS) return NULL;
    return g_vs_loot[index].active ? &g_vs_loot[index] : NULL;
}

/* The hook local_game.h calls (buggy_set_loot_spawn) the moment a part comes off. Signature matches
 * BuggyLootFn there; `comp` is kept so a HUD can say WHICH part is lying there. */
static inline void vehicle_salvage_loot_hook(int comp, int tier, int quantity, float x, float y, float z,
                                             int scene_id, unsigned int now_ms) {
    vehicle_salvage_loot_drop(scene_id, x, y, z, comp, tier, quantity, now_ms);
}

#endif /* SHANKPIT_VEHICLE_SALVAGE_LOOT_H */
