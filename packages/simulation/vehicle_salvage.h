#ifndef SHANKPIT_VEHICLE_SALVAGE_H
#define SHANKPIT_VEHICLE_SALVAGE_H

/* vehicle_salvage.h -- which part of a car broke, what is left of it, and what falls out of it.
 *
 * Founder real-time (2026-11-03): a garage that accepts "vehicles of questionable origin", cars
 * "converted down to components and its a bit variable like wow disenchanting", then destructible
 * vehicles whose parts shear off mid-crash. Today a car is one `int health` (BuggyState.health) and
 * vehicle_burn.h turns that single number into smoke -> fire -> detonation. This header is the layer
 * BELOW that ladder: five components that add up to the same 300 hull, so "the engine is gone" can be
 * a fact rather than a story the damage number tells.
 *
 * TWO INVARIANTS, and everything else is consequence:
 *   1. CONVERVATION. sum(component hp) == the hull, always. Every hit takes exactly as much hull as it
 *      is given -- proportional to what each live part still is -- and the leftovers of a part that
 *      was already gone land on the next live part in authoring order. If this ever breaks, the
 *      authority and the parts it reports are describing two different cars.
 *   2. DETERMINISM. The salvage roll is a hash of (entity id, tick, component), never a clock and
 *      never the server's private randomness: two clients watching the same wreck must compute the
 *      same loot, or the ledger forks. It reuses vehicle_burn.h's hash for exactly that reason.
 *
 * The numbers are PARENA (PARENA/stdlib/shankpit/vehicle_salvage_rules.prn -> vehicle_salvage_rules.c,
 * installed by vehicle_salvage_rules_host.h). With nothing installed the built-in defaults below are
 * used, so a test that includes only this header needs no host -- the same contract vehicle_burn.h and
 * buggy_rules_host.h already use.
 *
 * Deliberately NOT here: spawning shards (the host does that through brick_bodies), the wire
 * (PACKET_VEHICLE_STATE is a later slice), loot entities (the host maps a tier onto an existing item),
 * and any bending/fatigue of the chassis. A part either holds or it is gone.
 */
#include <string.h>

#include "vehicle_burn.h"   /* vehicle_burn_hash -- one hash for the whole car, so fire and salvage agree */

#define SALVAGE_MAX_COMPONENTS 5
#define SALVAGE_HULL           300   /* == buggy_spawn_defaults' health and PARENA's salvage-hull */

/* Index -> meaning. Contiguous from 0 because the wire carries them as a bitmask; TANK is last so a
 * four-component vehicle still has a valid 0..3 mask. */
#define SALVAGE_COMP_ENGINE  0
#define SALVAGE_COMP_CHASSIS 1
#define SALVAGE_COMP_WHEELS  2
#define SALVAGE_COMP_LOGIC   3
#define SALVAGE_COMP_TANK    4

/* What losing a part multiplies: the host folds these into PhysBuggyRules, so a broken engine is a
 * smaller drive force and not a second physics path. */
#define SALVAGE_CH_DRIVE_FORCE 0
#define SALVAGE_CH_TOP_SPEED   1
#define SALVAGE_CH_TURN_RATE   2
#define SALVAGE_CH_GRIP        3
#define SALVAGE_CH_COUNT       4

/* Low to high. Tier 3 is the overclocked logic board: the only part worth tearing a car apart for. */
#define SALVAGE_TIER_SCRAP       0
#define SALVAGE_TIER_REBAR       1
#define SALVAGE_TIER_COMPOSITE   2
#define SALVAGE_TIER_LOGIC       3

typedef struct {
    int (*component_count)(int kind);
    int (*component_hp)(int kind, int comp);
    int (*component_material)(int comp);
    int (*component_offset_centi)(int kind, int comp, int axis);
    int (*handling_permille)(int comp, int channel);
    int (*tier)(int kind, int comp, int roll_permille);
    int (*quantity)(int hp_permille);
} VehicleSalvageRules;

/* ---- built-in defaults: these MUST equal the PARENA table (the test asserts it) ------------------- */
static inline int salvage_def_component_count(int kind) { return kind == 1 ? 5 : 4; }

static inline int salvage_def_component_hp(int kind, int comp) {
    switch (comp) {
    case SALVAGE_COMP_ENGINE:  return kind == 1 ? 80 : 90;
    case SALVAGE_COMP_CHASSIS: return kind == 1 ? 100 : 120;
    case SALVAGE_COMP_WHEELS:  return kind == 1 ? 50 : 60;
    case SALVAGE_COMP_LOGIC:   return 30;
    case SALVAGE_COMP_TANK:    return kind == 1 ? 40 : 0;
    default:                   return 0;
    }
}

static inline int salvage_def_component_material(int comp) {
    if (comp == SALVAGE_COMP_WHEELS) return 2;   /* BF_KIND_WOOD */
    if (comp == SALVAGE_COMP_LOGIC)  return 3;   /* BF_KIND_GLASS */
    return 1;                                    /* BF_KIND_CONCRETE */
}

static inline int salvage_def_component_offset_centi(int kind, int comp, int axis) {
    if (comp == SALVAGE_COMP_ENGINE)  return axis == 1 ? 10 : (axis == 2 ? -70 : 0);
    if (comp == SALVAGE_COMP_WHEELS)  return axis == 1 ? -30 : 0;
    if (comp == SALVAGE_COMP_LOGIC)   return axis == 2 ? 45 : 0;
    if (comp == SALVAGE_COMP_TANK)
        return kind == 1 ? (axis == 1 ? 20 : (axis == 2 ? -20 : 0)) : 0;
    return 0;
}

static inline int salvage_def_handling_permille(int comp, int channel) {
    if (comp == SALVAGE_COMP_ENGINE)
        return channel == SALVAGE_CH_DRIVE_FORCE ? 300 : (channel == SALVAGE_CH_TOP_SPEED ? 600 : 1000);
    if (comp == SALVAGE_COMP_CHASSIS) {
        if (channel == SALVAGE_CH_DRIVE_FORCE) return 700;
        if (channel == SALVAGE_CH_TOP_SPEED)   return 700;
        if (channel == SALVAGE_CH_TURN_RATE)   return 800;
        return 900;
    }
    if (comp == SALVAGE_COMP_WHEELS) {
        if (channel == SALVAGE_CH_DRIVE_FORCE) return 800;
        if (channel == SALVAGE_CH_TOP_SPEED)   return 900;
        if (channel == SALVAGE_CH_TURN_RATE)   return 500;
        return 400;
    }
    return 1000;   /* LOGIC / TANK cost you the salvage, not the drive */
}

static inline int salvage_def_tier(int kind, int comp, int roll) {
    (void)kind;
    if (comp == SALVAGE_COMP_LOGIC)   return roll < 180 ? 3 : (roll < 500 ? 2 : 1);
    if (comp == SALVAGE_COMP_ENGINE)  return roll < 60 ? 3 : (roll < 300 ? 2 : 1);
    if (comp == SALVAGE_COMP_TANK)    return roll < 90 ? 2 : 0;
    if (comp == SALVAGE_COMP_WHEELS)  return roll < 120 ? 2 : 0;
    return roll < 30 ? 2 : 0;   /* CHASSIS */
}

static inline int salvage_def_quantity(int hp_permille) {
    return hp_permille >= 900 ? 3 : (hp_permille >= 400 ? 2 : 1);
}

static const VehicleSalvageRules g_vehicle_salvage_defaults = {
    salvage_def_component_count,
    salvage_def_component_hp,
    salvage_def_component_material,
    salvage_def_component_offset_centi,
    salvage_def_handling_permille,
    salvage_def_tier,
    salvage_def_quantity,
};
static const VehicleSalvageRules *g_vehicle_salvage_rules = &g_vehicle_salvage_defaults;

static inline void vehicle_salvage_set_rules(const VehicleSalvageRules *r) {
    g_vehicle_salvage_rules = r ? r : &g_vehicle_salvage_defaults;
}
static inline const VehicleSalvageRules *vehicle_salvage_rules(void) { return g_vehicle_salvage_rules; }

/* ---- state ---------------------------------------------------------------------------------------- */
typedef struct {
    int active;
    int kind;                 /* 0 buggy, 1 rocket car; anything else is treated as a buggy */
    int entity_id;            /* the identity the salvage roll is seeded from */
    int hp[SALVAGE_MAX_COMPONENTS];      /* absolute hull points, not permille */
    int max_hp[SALVAGE_MAX_COMPONENTS];
    unsigned int alive_mask;             /* bit per component; a detached part's bit is clear */
    unsigned int detached_mask;          /* cumulative -- what has EVER come off this car */
} SalvageVehicle;

static inline int vehicle_salvage_count(const SalvageVehicle *v) {
    const VehicleSalvageRules *r = g_vehicle_salvage_rules;
    int n = r->component_count(v ? v->kind : 0);
    return n > SALVAGE_MAX_COMPONENTS ? SALVAGE_MAX_COMPONENTS : (n < 0 ? 0 : n);
}

static inline int vehicle_salvage_total(const SalvageVehicle *v) {
    int total = 0;
    if (!v) return 0;
    int n = vehicle_salvage_count(v);
    for (int i = 0; i < n; i++) if (v->alive_mask & (1u << i)) total += v->hp[i];
    return total;
}

/* vehicle_salvage_reset -- a whole car. Entity id 0 is refused as an identity (it would make every
 * car's roll identical); 1 is substituted, matching vehicle_burn_reset's own seed guard. */
static inline void vehicle_salvage_reset(SalvageVehicle *v, int kind, int entity_id) {
    if (!v) return;
    const VehicleSalvageRules *r = g_vehicle_salvage_rules;
    memset(v, 0, sizeof *v);
    v->active = 1;
    v->kind = kind;
    v->entity_id = entity_id ? entity_id : 1;
    int n = vehicle_salvage_count(v);
    for (int i = 0; i < n; i++) {
        v->max_hp[i] = r->component_hp(kind, i);
        v->hp[i] = v->max_hp[i];
        if (v->hp[i] > 0) v->alive_mask |= (1u << i);
    }
}

/* vehicle_salvage_apply -- split `damage` hull points across the live parts.
 * Returns the bitmask of components that came off on THIS call (0 when nothing did). The hull total
 * drops by exactly min(damage, hull) and no other amount: the proportional pass is integer, so the
 * remainder is placed explicitly rather than silently dropped. */
static inline unsigned int vehicle_salvage_apply(SalvageVehicle *v, int damage) {
    if (!v || !v->active || damage <= 0) return 0;
    int n = vehicle_salvage_count(v);
    int live_total = vehicle_salvage_total(v);
    if (live_total <= 0) return 0;

    int remaining = damage;
    unsigned int fresh = 0;

    /* pass 1: proportional to what each part still is */
    for (int i = 0; i < n && remaining > 0; i++) {
        if (!(v->alive_mask & (1u << i)) || v->hp[i] <= 0) continue;
        int share = (int)(((long long)damage * v->hp[i]) / live_total);
        if (share <= 0) share = 1;          /* every live part wears something, even from a big hit */
        if (share > remaining) share = remaining;
        if (share > v->hp[i]) share = v->hp[i];
        v->hp[i] -= share;
        remaining -= share;
        if (v->hp[i] == 0) {
            v->alive_mask &= ~(1u << i);
            v->detached_mask |= (1u << i);
            fresh |= (1u << i);
        }
    }

    /* pass 2: damage that belonged to a part which was already gone lands on the next live part in
     * authoring order -- deterministic, and never duplicated or lost */
    for (int i = 0; i < n && remaining > 0; i++) {
        if (!(v->alive_mask & (1u << i)) || v->hp[i] <= 0) continue;
        int take = remaining < v->hp[i] ? remaining : v->hp[i];
        v->hp[i] -= take;
        remaining -= take;
        if (v->hp[i] == 0) {
            v->alive_mask &= ~(1u << i);
            v->detached_mask |= (1u << i);
            fresh |= (1u << i);
        }
    }
    return fresh;
}

/* vehicle_salvage_sync -- reconcile the parts with the authority's own hull number.
 *
 * The hull is NOT this layer's private property: a shot, a repair and vehicle_burn.h's own 0.25/tick
 * drain all move it without asking. So once per tick the host hands the real hull in and this makes the
 * parts agree with it, in both directions:
 *
 *   down   exactly vehicle_salvage_apply -- the proportional split, the leftovers, the detach bits.
 *   up     a repair refills what is still ATTACHED, proportional to the room each part has. A part that
 *          came off stays off: a repaired wreck is still a wreck until the car is replaced. Without
 *          this, a car welded back from 0 would silently grow a second engine, and sum(parts) == hull
 *          would only be true while the number fell.
 *
 * Returns the hull this car can actually represent; the caller writes THAT back (it is below the
 * request only when the request exceeds what is still attached). *fresh is the mask of parts that came
 * off on this call -- the host's cue to throw shards -- and is set to 0 when nothing did. */
static inline int vehicle_salvage_sync(SalvageVehicle *v, int hull, unsigned int *fresh) {
    if (fresh) *fresh = 0;
    if (!v || !v->active) return hull;
    int n = vehicle_salvage_count(v);
    int total = vehicle_salvage_total(v);

    if (hull < total) {
        unsigned int f = vehicle_salvage_apply(v, total - hull);
        if (fresh) *fresh = f;
        return vehicle_salvage_total(v);
    }
    if (hull == total) return total;

    int need = hull - total;
    int room_total = 0;
    for (int i = 0; i < n; i++) {
        if (!(v->alive_mask & (1u << i))) continue;
        int room = v->max_hp[i] - v->hp[i];
        if (room > 0) room_total += room;
    }
    if (room_total <= 0) return total;   /* nothing attached left to refill: the car cannot hold the number */

    int remaining = need;
    /* pass 1: proportional to the room each attached part has left */
    for (int i = 0; i < n && remaining > 0; i++) {
        if (!(v->alive_mask & (1u << i))) continue;
        int room = v->max_hp[i] - v->hp[i];
        if (room <= 0) continue;
        int share = (int)(((long long)need * room) / room_total);
        if (share > room) share = room;
        if (share > remaining) share = remaining;
        v->hp[i] += share;
        remaining -= share;
    }
    /* pass 2: the integer remainder, in authoring order */
    for (int i = 0; i < n && remaining > 0; i++) {
        if (!(v->alive_mask & (1u << i))) continue;
        int room = v->max_hp[i] - v->hp[i];
        int take = remaining < room ? remaining : room;
        if (take <= 0) continue;
        v->hp[i] += take;
        remaining -= take;
    }
    return vehicle_salvage_total(v);
}

/* vehicle_salvage_set_state -- adopt an ABSOLUTE component state, the form the wire carries
 * (PACKET_VEHICLE_STATE). Masks arrive from the network and are therefore untrusted: they are clipped
 * to the components this car kind actually has, and each hp is clamped into [0, its own max]. Returns
 * the hull the state represents, so the caller can mirror it into BuggyState.health and the two never
 * disagree. */
static inline int vehicle_salvage_set_state(SalvageVehicle *v, int kind, int entity_id,
                                            unsigned int alive_mask, unsigned int detached_mask,
                                            const int hp[SALVAGE_MAX_COMPONENTS]) {
    if (!v || !hp) return 0;
    const VehicleSalvageRules *r = g_vehicle_salvage_rules;
    memset(v, 0, sizeof *v);
    v->active = 1;
    v->kind = kind;
    v->entity_id = entity_id ? entity_id : 1;
    int n = vehicle_salvage_count(v);
    unsigned int valid = n >= 32 ? 0xFFFFFFFFu : ((n <= 0) ? 0u : ((1u << n) - 1u));
    v->alive_mask = alive_mask & valid;
    v->detached_mask = detached_mask & valid;
    for (int i = 0; i < n; i++) {
        v->max_hp[i] = r->component_hp(kind, i);
        int h = hp[i];
        if (h < 0) h = 0;
        if (h > v->max_hp[i]) h = v->max_hp[i];
        /* a part that is not alive holds nothing, and vice versa: the two masks and hp must agree, or
           the sum the authority reads would include a part the wire says is gone */
        if (!(v->alive_mask & (1u << i))) h = 0;
        v->hp[i] = h;
        if (v->hp[i] <= 0) {
            v->alive_mask &= ~(1u << i);
            if (v->max_hp[i] > 0) v->detached_mask |= (1u << i);
        }
    }
    return vehicle_salvage_total(v);
}

/* vehicle_salvage_offset_centi -- where a part sits in the car's own frame, integer centi-units: the
 * host rotates this by the car's yaw to throw the shard from where the part actually was. */
static inline int vehicle_salvage_offset_centi(const SalvageVehicle *v, int comp, int axis) {
    if (!v || comp < 0 || comp >= SALVAGE_MAX_COMPONENTS || axis < 0 || axis > 2) return 0;
    return g_vehicle_salvage_rules->component_offset_centi(v->kind, comp, axis);
}

/* vehicle_salvage_handling_permille -- the live multiplier for one channel, 0..1000. Every detached
 * part multiplies in, in authoring order, so the stack is deterministic (integer division truncates
 * the same way everywhere). */
static inline int vehicle_salvage_handling_permille(const SalvageVehicle *v, int channel) {
    if (!v) return 1000;
    const VehicleSalvageRules *r = g_vehicle_salvage_rules;
    int p = 1000;
    int n = vehicle_salvage_count(v);
    if (channel < 0 || channel >= SALVAGE_CH_COUNT) return 1000;
    for (int i = 0; i < n; i++) {
        if (v->alive_mask & (1u << i)) continue;
        if (!(v->detached_mask & (1u << i))) continue;
        p = (p * r->handling_permille(i, channel)) / 1000;
    }
    return p;
}

/* The roll: identity + tick + part, hashed with the same function the fire flicker uses. Same car,
 * same tick, same part -> same number on every machine, forever. */
static inline int vehicle_salvage_roll_permille(const SalvageVehicle *v, int comp, unsigned int tick) {
    unsigned int entity = (unsigned int)(v ? v->entity_id : 1);
    unsigned int x = entity * 2654435761u;
    x ^= tick * 2246822519u;
    x ^= (unsigned int)comp * 3266489917u;
    return (int)(vehicle_burn_hash(x) % 1000u);
}

/* vehicle_salvage_tier -- what a detached part yields (SALVAGE_TIER_*), 0 if it has no value. Only
 * meaningful once the part is off; the host calls it at the moment it spawns the loot. */
static inline int vehicle_salvage_tier(const SalvageVehicle *v, int comp, unsigned int tick) {
    if (!v || comp < 0 || comp >= SALVAGE_MAX_COMPONENTS) return SALVAGE_TIER_SCRAP;
    return g_vehicle_salvage_rules->tier(v->kind, comp, vehicle_salvage_roll_permille(v, comp, tick));
}

/* Quantity is condition, not luck: a part at 92% is worth three of itself, a wrecked one is worth
 * one. hp_permille of the part's ORIGINAL value, so a detached part remembers what it used to be. */
static inline int vehicle_salvage_quantity(const SalvageVehicle *v, int comp) {
    if (!v || comp < 0 || comp >= SALVAGE_MAX_COMPONENTS) return 1;
    if (v->max_hp[comp] <= 0) return 1;
    int hp = v->hp[comp];
    if (hp < 0) hp = 0;
    int permille = (hp * 1000) / v->max_hp[comp];
    return g_vehicle_salvage_rules->quantity(permille);
}

/* vehicle_salvage_material -- the BF_KIND_* a detached part's shards should be made of. */
static inline int vehicle_salvage_material(int comp) {
    return g_vehicle_salvage_rules->component_material(comp);
}

#endif /* SHANKPIT_VEHICLE_SALVAGE_H */
