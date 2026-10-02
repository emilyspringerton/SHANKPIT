#ifndef SHIELD_PACKS_H
#define SHIELD_PACKS_H

/* shield_packs.h -- card #541 (founder real-time: "add a 25% chance for players to drop a shield pack when they
 * die in queue it refills your shield to full (shields currently dont regenerate leave it like that)").
 *
 * A small pool of shield packs lying in the world. The SERVER owns it in MODE_QUEUE (roll on death, pickup by
 * proximity, shield set to full) and mirrors the whole set to clients with PACKET_SHIELD_PACKS; clients only
 * render it, they never grant a pack themselves. Pure data + rules, no SDL/GL and no PlayerState dependency,
 * like gun_items.h, so it links into both the server and the lobby and is unit-testable. */

#define SHIELD_PACK_MAX 16
#define SHIELD_PACK_DROP_PERCENT 25   /* chance a dying player leaves a pack */
#define SHIELD_PACK_RADIUS 2.5f       /* horizontal pickup reach, world units */
#define SHIELD_PACK_HEIGHT 3.0f       /* vertical window around the pack */
#define SHIELD_PLAYER_SLOTS 64        /* upper bound on player slots tracked for the once-per-death latch */

typedef struct {
    int active;
    float x, y, z;
} ShieldPack;

void shield_packs_reset(void);

/* Death hook, call once per tick per player slot. `alive` is 0 while dead. On the FIRST tick the slot is seen
 * dead a pack is dropped at (x,y,z) if roll100 (0..99) < SHIELD_PACK_DROP_PERCENT; the latch re-arms when the
 * slot is alive again. Returns the pack slot, or -1 (no drop, already handled, pool full, slot out of range). */
int shield_packs_note_player(int slot, int alive, float x, float y, float z, int roll100);

/* Collects the nearest active pack within reach of (px,py,pz); returns 1 if one was taken, else 0. The caller
 * decides who may pick up (the server skips players whose shield is already full) and applies the refill. */
int shield_packs_take(float px, float py, float pz);

int shield_packs_active_count(void);
const ShieldPack *shield_packs_get(int slot);   /* NULL past the pool */
unsigned shield_packs_version(void);            /* bumps every time the set changes */

/* Client mirror: replace the whole set with the server's. Entries beyond SHIELD_PACK_MAX are ignored and
 * non-finite coordinates are skipped (a packet is untrusted input). Returns how many packs are now active. */
int shield_packs_set_all(const float *xyz, int n);

#endif
