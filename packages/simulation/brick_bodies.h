#ifndef SHANKPIT_BRICK_BODIES_H
#define SHANKPIT_BRICK_BODIES_H

/* brick_bodies.h -- physical broken brick: pieces of destructible wall become real rigid bodies on
 * GOLDEN BAND's XPBD solver (packages/goldenband/grb.{c,h}), with mass, orientation and angular
 * velocity, instead of the old massless cosmetic flakes (brick_debris.h).
 *
 * Two kinds of body feed this pool (both come from packages/world/brick_fracture.h):
 *   - SHARDS: a cell that was shot / blasted out spawns a small box (0.2 m) of that cell's material.
 *     Mass = material density x volume: a brick shard is 1900 kg/m^3 x 0.008 m^3 ~= 15 kg, concrete
 *     19, wood 5, glass a thin 0.24x0.24x0.06 plate ~8 kg. It inherits a fraction of the impact
 *     velocity (a bullet's direction / a blast's radial outward) -- see brick_world.h.
 *   - CHUNKS: cells cut off from everything (bf_detect_detached) fall as one body shaped like the
 *     chunk's bounding box, mass = sum of its cells' density x volume (a 3 m wall cell really is
 *     tonnes; the glass disc shot out of a pane really is heavier than a brick).
 *
 * Collision. grb solves body vs the ground plane (with friction, restitution and full torque). This
 * module adds, once per fixed 64 Hz tick after the grb step: the 8 corners of each body against the
 * level's static geometry (a host callback, so oriented boxes and RAMPS work -- brick_world.h backs
 * it with physics.h's obb_sphere_push) resolved as real impulses at the contact point (so bodies
 * tumble and slide down a ramp, not just stop), and shard-vs-shard sphere contacts so shards pile
 * up instead of interpenetrating on the floor.
 *
 * Named v0 limits: grb has no body-vs-body (shards use spheres here; chunks do not collide with
 * other bodies), chunk shape is its bounding box, only body-corner-into-geometry is detected (a
 * static edge poking into a face is not), no sleeping in grb (a calm body is simply held still).
 * Bodies never feed back into gameplay or the network: the server only decides which cells are
 * gone. Deterministic (fixed step, no rand, no wall clock): same inputs -> same bodies.
 */

#include "../goldenband/grb.h"

#define BB_MAX            120          /* <= GRB_MAX_BODIES */
#define BB_HZ             64
#define BB_GRAVITY        19.62        /* 2 g: reads right at SHANKPIT's scale; tunable */
#define BB_SHARD_HALF     0.10         /* shard box half extent (m) */
#define BB_MAX_AGE        120.0        /* seconds before a body is retired */

/* kinds mirror BF_KIND_* (brick, concrete, wood, glass) */
typedef struct {
    int active;
    int kind;
    int big;                           /* a chunk: no shard-vs-shard sphere contacts */
    double half[3];
    double calm, age;
} BbSlot;

/* Static-geometry query: is the sphere (c, r) penetrating the level? 1 = yes, with the exit normal
 * and depth of the deepest overlap. */
typedef int (*BbPushFn)(void *user, const double c[3], double r, double nrm[3], double *pen);

typedef struct {
    GrbWorld world;
    BbSlot slot[BB_MAX];
    int cursor;
    double acc;
    BbPushFn push;
    void *user;
    double ground_y;
    unsigned int ticks;
} BrickBodies;

void bb_init(BrickBodies *bb, BbPushFn push, void *user);
void bb_clear(BrickBodies *bb);
/* Spawns a body (box of the given half extents) with `mass` kg, velocity and spin. Returns the slot or -1. */
int bb_spawn(BrickBodies *bb, int kind, int big, double mass, const double pos[3], const double half[3],
             const double vel[3], const double omega[3]);
/* Advances by dt seconds in fixed 1/64 s ticks (catch-up capped at 4 ticks). */
void bb_step(BrickBodies *bb, double dt);
int bb_active_count(const BrickBodies *bb);
double bb_slot_mass(const BrickBodies *bb, int slot);

#endif
