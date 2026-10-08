#ifndef AWARENESS_COMPASS_H
#define AWARENESS_COMPASS_H
#include <math.h>

/* awareness_compass.h -- BIG_O engine merge phase 8 (EMILY/BACKLOG.md SECTION 536 follow-up,
 * founder real-time, 2026-10-08: "continue to bring in the BIG_O affordances into shankpit
 * zombies... we need it all evented with reflux"). A faithful, verbatim port of BIG_O's own
 * day/packages/common/bigo_awareness.h (BIG_O/NORTHSTAR.md §35, "every agent in the system,
 * including the player, can 'feel' when an agent notices them") -- same pure, host-side,
 * no-network/no-GL decision-helper discipline pheromone.h's own port already established, just
 * for direction/compass/intensity math instead of pheromone steering.
 *
 * BIG_O's own version keys this off its QUIET costume/Decorum observation path, which SHANKPIT's
 * MODE_ZOMBIES/MODE_SURVIVAL has no equivalent of yet (no costume/Decorum system in these modes).
 * This port is used instead by the LOUD zombie-event channel these modes DO have --
 * packages/simulation/zombies_hud_bridge.c turns a REFLUX_ACTION_ZOMBIE_MOOD_ESCALATED/
 * WITNESS_ESCALATED/GIANT_BUG_ATE_ZOMBIE/MEN_DISPATCHED/MEN_RESOLVED event into a real "something
 * is happening near you" compass+intensity readout, honestly a new application of this math, not
 * a claim that SHANKPIT's Decorum system now exists. */

static const char *const AWARENESS_COMPASS_NAMES[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };

/* awareness_direction -- normalizes (dx, dz) into out_x/out_z. A zero-length input (the subject is
 * exactly on top of the player, or no real position is known yet) degrades to (0, 1) -- north --
 * rather than dividing by zero or returning garbage. */
static inline void awareness_direction(float dx, float dz, float *out_x, float *out_z) {
    float len = sqrtf(dx * dx + dz * dz);
    if (len < 0.0001f) { *out_x = 0.0f; *out_z = 1.0f; return; }
    *out_x = dx / len;
    *out_z = dz / len;
}

/* awareness_compass -- real 8-point index from an already-normalized direction, matching
 * AWARENESS_COMPASS_NAMES's own index order. Works on any nonzero vector, not just a unit one.
 * Same convention BIG_O's bigo_awareness_compass fixed: +Z is "north" (index 0), rotating
 * clockwise through E/S/W, this engine's own existing x/z world-position axes verbatim. */
static inline int awareness_compass(float dir_x, float dir_z) {
    const float pi2 = 6.28318530717958647692f;
    float angle = atan2f(dir_x, dir_z);   /* -PI..PI, 0 = north (+Z), clockwise toward +X (east) */
    if (angle < 0.0f) angle += pi2;
    int idx = (int)(angle / pi2 * 8.0f + 0.5f) % 8;
    if (idx < 0) idx += 8;                /* defensive: keeps a pathological angle in range */
    return idx;
}

/* awareness_intensity -- real, bounds-checked 0..100 score. severity (already 0..100, from
 * zombies_awareness_severity.prn's own PARENA decision) is NOT rescaled here -- unlike BIG_O's own
 * bigo_awareness_intensity, which scales conspicuousness up per extra witness, this call site has
 * no live witness-count input to scale by (the REFLUX payload is a/b/c scalars, not a witness
 * list), so this is a straight, honest clamp, not a port of that scaling formula. */
static inline int awareness_intensity(int severity) {
    if (severity < 0) return 0;
    if (severity > 100) return 100;
    return severity;
}

#endif
