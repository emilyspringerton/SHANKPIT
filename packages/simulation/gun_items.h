#ifndef GUN_ITEMS_H
#define GUN_ITEMS_H

/* gun_items.h -- card #487: "survival mode the guns will be items entities widgets that drop on the ground and
 * you pick them up when you oof a competitor" (Fortnite-style loot). First slice, client-local like
 * food_pickup.h: a pool of gun entities lying in the world, walk-over pickup, and loot dropped by a killed
 * enemy. Survival players start with melee/tools only and arm themselves from this pool.
 *
 * Pure data + rules, no SDL/GL and no PlayerState dependency, so it links anywhere (host plumbing applies the
 * result: owned-mask bit, current weapon, ammo refill). NOT yet server-authoritative -- the competitive
 * multiplayer survival map is the follow-up this module is the first piece of. */

#define GUN_ITEM_MAX 64
#define GUN_PICKUP_RADIUS 2.2f   /* horizontal, world units */
#define GUN_PICKUP_HEIGHT 2.5f   /* vertical window around the item */
#define GUN_DROP_PERCENT 35      /* chance a killed enemy leaves a gun */

typedef struct {
    int active;
    int weapon;   /* WPN_* from protocol.h -- stored as a plain int so this header needs nothing */
    float x, y, z;
} GunItem;

void gun_items_reset(void);
/* Lays a gun on the ground; returns its slot, or -1 if the pool is full. */
int gun_items_drop(int weapon, float x, float y, float z);
/* If (px,py,pz) is within reach of an active gun, collects it (deactivates the slot) and returns the weapon
 * id; -1 if nothing was in reach. The nearest gun wins when several overlap. */
int gun_items_check(float px, float py, float pz);
int gun_items_active_count(void);
/* Read-only view for the renderer; NULL past the pool. */
const GunItem *gun_items_get(int slot);

/* Weighted loot table: magnum 30, AR 25, shotgun 20, katana 10, sniper 10, missile 5 (sums to 100).
 * roll is 0..99 (anything else is wrapped); returns a WPN_* id. Deterministic, so it is testable. */
int gun_loot_pick(int roll);

/* Seeds the map: `count` guns on a ring (radius ~14..26) around (cx,cz) at height y, weapons cycled through the
 * loot table so every type is present. Returns how many were placed. */
int gun_items_seed_ring(float cx, float y, float cz, int count);

/* Enemy-death hook: call once per tick per enemy slot. `alive` 0 on the tick(s) it is dead. Drops a gun at (x,y,z)
 * the FIRST tick it is seen dead if `roll100` (0..99) < GUN_DROP_PERCENT; re-arms when alive again. Returns the
 * dropped slot or -1. `slot` is an enemy index 0..GUN_ITEM_MAX-1 (out of range is ignored). */
int gun_items_note_enemy(int slot, int alive, float x, float y, float z, int roll100, int loot_roll);

#endif
