#ifndef SHANKPIT_BRICK_BODIES_H
#define SHANKPIT_BRICK_BODIES_H

/* brick_bodies.h -- falling cut-off geometry, FAKED cheaply. When a shot or blast leaves a piece of wall or
 * pane connected to nothing (packages/world/brick_fracture.h detach), it must not hang in the air: it
 * becomes one "chunk" that falls under gravity with a seeded tumble, bounces once, and lies flat on
 * whatever it lands on (the floor plane or a level box, found with one probe per chunk per tick), then
 * lingers a few seconds. No solver, no body-vs-body, no piling -- it only has to look right and cost almost
 * nothing (16 chunks max, one level scan each). Mass (material density x volume) is still carried so the
 * NOCK `density` setting means something to tooling and tests; it does not drive the motion.
 * Shot-out cells are NOT bodies any more: they shed the old cheap flakes (brick_debris.h).
 * Deterministic: fixed step, no rand, no wall clock. */

#include "../goldenband/grb.h"

#define BB_MAX            32           /* <= GRB_MAX_BODIES */
#define BB_HZ             64
#define BB_GRAVITY        30.0         /* level units/s^2 (a cell is 3 units): snappy, tunable */
#define BB_SHARD_HALF     0.10         /* shard box half extent (m) */
#define BB_MAX_AGE        20.0         /* seconds before a body is retired, however it is doing */
#define BB_LINGER         5.0          /* seconds a landed chunk stays before it goes */

/* kinds mirror BF_KIND_* (brick, concrete, wood, glass) */
typedef struct {
    int active;
    int kind;
    int big;                           /* a chunk: no shard-vs-shard sphere contacts */
    double half[3];
    double calm, age;                  /* calm > 0: landed (seconds since) */
    int bounced;
    double settle_q[4], rest_y;
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
/* Advances by dt seconds in fixed 1/64 s ticks (catch-up capped at 2 ticks). */
void bb_step(BrickBodies *bb, double dt);
int bb_active_count(const BrickBodies *bb);
double bb_slot_mass(const BrickBodies *bb, int slot);

#endif
