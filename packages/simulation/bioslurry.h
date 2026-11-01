#ifndef BIOSLURRY_H
#define BIOSLURRY_H

/* bioslurry.h -- what a dead zombie leaves, and what the lab can read off it.
 *
 * Founder real-time, 2026-11-01: "as per big_o when you kill zombies they may drop bioslury which
 * can be taken to the lab to possibly harvest dna; dna should depend on the stats of the zombie
 * slain".
 *
 * A killed zombie MAY leave a canister where it fell (the chance is the body's own
 * slurry_permille, zombie_taxonomy.h -- an ALPHA almost always does, a SHAMBLER sometimes). You
 * walk over it to pick it up, you carry it, and you take it to the lab; delivering does not
 * promise a result, it rolls one: a poor strand from a level 1 shambler can come back "no usable
 * DNA", which is exactly what makes a level 9 Alpha worth killing.
 *
 * Deliberately NOT on the wire and NOT in the phone's own state yet: this is the simulation half
 * (drops, carrying, delivery, the DNA itself). Handing a strand to `Phone.lab_trait`/`samples[]`
 * is a host call, and the host's lab UI is in apps/lobby/src/main.c, which this sandbox cannot
 * compile -- so the seam is an accessor, not an edit nobody could verify. Same honest boundary
 * projectiles and towers already hold.
 *
 * "BIOSLURRY" is the founder's spelling; the code uses the corrected one in identifiers only.
 */

#include "../common/protocol.h"

#define BIOSLURRY_MAX_DROPS 10
#define BIOSLURRY_PICKUP_RADIUS 3.0f
#define BIOSLURRY_ROT_MS 120000u     /* an uncollected canister is gone after two minutes */
#define BIOSLURRY_MAX_CARRIED 6
#define BIOSLURRY_MAX_DNA 16
#define BIOSLURRY_HARVEST_MIN 1      /* a delivery is one canister at a time */
#define BIOSLURRY_DNA_FAIL_BELOW 25  /* a strand graded under this yields nothing usable */

typedef struct {
    int active;
    int scene_id;
    float x, y, z;
    int cls, level;
    int dna_quality, dna_trait;
    unsigned int dropped_ms;
    int collected_by;    /* -1 while it lies on the ground */
} BioslurryDrop;

/* A usable strand, i.e. the product of a delivery that actually read. */
typedef struct {
    int quality;         /* 0..100, the source body's dna_quality */
    int trait;           /* ZTRAIT_* */
    int cls, level;      /* what it came off, for the lab's own text */
} BioslurryStrand;

void bioslurry_reset(void);

/* Places a canister for a body that just died. Returns the pool index or -1 (pool full). */
int bioslurry_drop(int scene_id, float x, float y, float z, int cls, int level, unsigned int now_ms);

void bioslurry_tick(ServerState *s, unsigned int now_ms);

/* Pickups: walking over a canister collects it (per player, capped at BIOSLURRY_MAX_CARRIED).
 * Returns how many were collected this tick. */
int bioslurry_collect(ServerState *s, unsigned int now_ms);
int bioslurry_carried(int player_id);

/* Deliver one carried canister for this player. Returns 1 when a usable strand was produced (and
 * appended, see bioslurry_dna_at), 0 when it was spent and yielded nothing -- a real outcome, not
 * an error. -1 when the player is carrying nothing. */
int bioslurry_deliver(int player_id, unsigned int now_ms);

int bioslurry_drop_count(void);
const BioslurryDrop *bioslurry_drop_at(int index);
int bioslurry_dna_count(void);
const BioslurryStrand *bioslurry_dna_at(int index);

/* Total canisters delivered this session and how many of those read (a real success rate the lab
 * UI can show instead of pretending every sample works). */
int bioslurry_delivered_total(void);
int bioslurry_harvested_total(void);

#endif
