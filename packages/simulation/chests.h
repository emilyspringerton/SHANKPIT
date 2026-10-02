#ifndef CHESTS_H
#define CHESTS_H

/* chests.h -- card #528: destructible loot chests. A pool of chests lying in the world; shooting, slashing or blasting one
 * wears it down, and when it breaks it spills a weapon onto the ground (gun_items.h) for the player to walk over.
 * What a chest IS -- HP per tier, tier rarity, the drop table, how much each weapon hurts it -- is decided by the PARENA mod
 * PARENA/stdlib/shankpit/chest_rules.prn (generated into chest_rules.c); this module is the pool, the geometry and the
 * plumbing. Pure data + math, no SDL/GL/PlayerState, so it links anywhere and is headless-testable.
 * Client-local like gun_items.h (survival mode); server-authoritative chests are the follow-up. */

#define CHEST_MAX 32
#define CHEST_HALF_X 0.9f
#define CHEST_HALF_Y 0.6f   /* a chest sits on the ground: it spans y .. y + 2*HALF_Y */
#define CHEST_HALF_Z 0.6f

typedef struct {
    int active;
    int tier;       /* 0 wooden, 1 reinforced, 2 rare */
    int hp, max_hp;
    float x, y, z;  /* base centre on the ground */
    int hurt_ms;    /* host-set timestamp of the last hit, for a hit flash (0 = never) */
} Chest;

void chests_reset(void);
/* Places a chest of `tier` (clamped 0..2) with its tier's full HP; returns its slot or -1 if the pool is full. */
int chests_place(int tier, float x, float y, float z);
int chests_active_count(void);
const Chest *chests_get(int slot);   /* NULL past the pool */

/* Tracing a ray (unit dx,dy,dz) of length <= range against the chests: the nearest chest it enters takes
 * dmg * chest_damage_permille(weapon) / 1000. If that breaks it, a weapon (chest_loot_weapon(tier, loot_roll)) is dropped
 * onto the ground where it stood. Returns the slot hit (even if it survived), or -1. *broke is set to 1 if it broke. */
int chests_hit_ray(float ox, float oy, float oz, float dx, float dy, float dz, float range,
                   int weapon, int dmg, int loot_roll, int *broke);
/* Splash: every chest whose box is within `radius` of (x,y,z) takes dmg (scaled by weapon); returns how many broke.
 * Each broken chest uses loot_roll + its slot so a blast does not drop N copies of one weapon. */
int chests_hit_blast(float x, float y, float z, float radius, int weapon, int dmg, int loot_roll);

/* Seeds `count` chests on the road ring like gun_items_seed_roads; tier from chest_tier_for_roll over spread rolls.
 * Returns how many were placed. */
int chests_seed_roads(float cx, float y, float cz, int count, float r0, float r_step, float pitch);

/* PARENA-generated (chest_rules.c) */
int chest_max_hp(int tier);
int chest_tier_for_roll(int roll);
int chest_loot_weapon(int tier, int roll);
int chest_damage_permille(int weapon);

#endif
