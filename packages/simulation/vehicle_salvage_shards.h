#ifndef SHANKPIT_VEHICLE_SALVAGE_SHARDS_H
#define SHANKPIT_VEHICLE_SALVAGE_SHARDS_H

/* vehicle_salvage_shards.h -- a part that comes off a car, as a body in the brick-body world.
 *
 * vehicle_salvage.h decides WHEN a part is gone and local_game.h's buggy_shard_throw decides WHERE it
 * was; this is the last hop, and it deliberately adds no physics: a sheared part goes through the
 * existing packages/simulation/brick_bodies.{h,c} on GOLDEN BAND's grb, the same path a shot-out wall
 * already takes. That is the whole reason the component table carries BF_KIND_* materials -- a broken
 * logic board falls as glass because the brick world already knows how glass behaves.
 *
 * Per-client cosmetic, exactly like the brick bodies it rides on: bb bodies are not synced and the
 * dedicated server keeps none (brick_world_discard_bodies). So a host BINDS a world here; an unbound
 * host spawns nothing and costs nothing, which is what the server wants.
 *
 * Deterministic: mass and extents are a function of the component, and the tumble is hashed from the
 * seed local_game passes (itself hashed from car identity and component). No rand(), no clock.
 */
#include "brick_bodies.h"
#include "vehicle_burn.h"      /* vehicle_burn_hash -- the same hash the rest of the car uses */
#include "vehicle_salvage.h"   /* SALVAGE_COMP_* */

static BrickBodies *g_salvage_shard_bodies = NULL;

static inline void vehicle_salvage_shards_bind(BrickBodies *bb) { g_salvage_shard_bodies = bb; }

/* A part is a lump, not a brick shard: the chassis is a heavy slab, the logic board a small plate.
 * Kilos are nominal but carried on the body (bb_slot_mass), so tooling and tests can read them back. */
static inline double vehicle_salvage_shard_mass(int comp) {
    switch (comp) {
    case SALVAGE_COMP_CHASSIS: return 320.0;
    case SALVAGE_COMP_ENGINE:  return 180.0;
    case SALVAGE_COMP_TANK:    return 90.0;
    case SALVAGE_COMP_WHEELS:  return 70.0;
    default:                   return 4.0;      /* the logic board */
    }
}

static inline void vehicle_salvage_shard_extents(int comp, double h[3]) {
    if (comp == SALVAGE_COMP_CHASSIS) { h[0] = 1.10; h[1] = 0.35; h[2] = 1.60; return; }
    if (comp == SALVAGE_COMP_ENGINE)  { h[0] = 0.45; h[1] = 0.35; h[2] = 0.50; return; }
    if (comp == SALVAGE_COMP_WHEELS)  { h[0] = 0.50; h[1] = 0.35; h[2] = 0.50; return; }
    if (comp == SALVAGE_COMP_TANK)    { h[0] = 0.35; h[1] = 0.35; h[2] = 0.60; return; }
    h[0] = 0.22; h[1] = 0.04; h[2] = 0.18;
}

/* The hook local_game.h calls (buggy_set_shard_spawn). Spawns one body, or nothing when no world is
 * bound. Velocity is the car's own plus local_game's seeded kick, so a part lands where a piece of
 * that car would; the spin is derived from the same seed. */
static inline void vehicle_salvage_shards_hook(int comp, int material, float x, float y, float z,
                                               float vx, float vy, float vz, unsigned int seed) {
    BrickBodies *bb = g_salvage_shard_bodies;
    if (!bb) return;
    double pos[3] = { (double)x, (double)y, (double)z };
    double half[3];
    double vel[3] = { (double)vx, (double)vy, (double)vz };
    double om[3];
    vehicle_salvage_shard_extents(comp, half);
    unsigned int s = seed ? seed : 1u;
    double wob = comp == SALVAGE_COMP_LOGIC ? 1.0 : 0.6;   /* thin glass tumbles hardest */
    for (int a = 0; a < 3; a++) {
        s = vehicle_burn_hash(s + 0x9E3779B9u + (unsigned int)a);
        om[a] = ((double)(s & 0xFFFFu) / 65535.0 - 0.5) * 3.2 * wob;
    }
    bb_spawn(bb, material, 1, vehicle_salvage_shard_mass(comp), pos, half, vel, om);
}

#endif /* SHANKPIT_VEHICLE_SALVAGE_SHARDS_H */
