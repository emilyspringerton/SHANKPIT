#ifndef POLICE_PATROL_H
#define POLICE_PATROL_H

/* police_patrol.h -- the automated cruiser: a police rocket car with nobody in it, driving a loop.
 *
 * Founder (2026-11-05): "add automated police cars to the real zombies world ... the police ones when you
 * are boosting or hitting space for hover the siren is triggered by those things". Two halves:
 *
 *   1. A police car SOUNDS its siren when the driver boosts or hovers (PARENA's siren-trigger decides;
 *      physics.h records the driver's command in BuggyState.siren).
 *   2. A police car with no driver at all still drives: it circles the level on a ring, which is what
 *      makes a moving unmanned cruiser trigger the siren at all (the third arm of the same rule).
 *
 * This header is the second half only -- a STATELESS steering intent, so it is testable without physics,
 * a device or a clock and cannot drift into a second vehicle implementation. The host still calls the
 * ordinary simulate_buggy_state_ex with the intent, so the cruiser is a buggy with boosters and not a
 * special case the collision code has never seen. A ring rather than a stored route means no state to
 * desync: the target is a pure function of the clock, the car's id and the anchor the host placed it at.
 *
 * The ring is the LEVEL's, not the car's: an anchor + radius, chosen by whoever spawned the car (the
 * zombies level's own vehicle bay), so two cruisers on the same ring stay a patrol and not two cars
 * chasing each other's tails. */

#include <math.h>

#include "protocol.h"

#ifndef POLICE_PATROL_MAX
#define POLICE_PATROL_MAX 8
#endif
#define POLICE_PATROL_RADIUS_DEFAULT 60.0f
#define POLICE_PATROL_PERIOD_MS     48000u   /* one lap of the ring per 48 s: patrolling, not racing */

static float g_police_anchor_x[POLICE_PATROL_MAX];
static float g_police_anchor_z[POLICE_PATROL_MAX];
static float g_police_radius[POLICE_PATROL_MAX];

/* Anchor a cruiser's ring. Called once when the car is spawned; the ring lives with the slot, so a
 * respawned or recycled buggy does not inherit somebody else's patrol. */
static inline void police_patrol_set_anchor(int id, float cx, float cz, float radius) {
    if (id < 0 || id >= POLICE_PATROL_MAX) return;
    g_police_anchor_x[id] = cx;
    g_police_anchor_z[id] = cz;
    g_police_radius[id] = (radius > 1.0f) ? radius : POLICE_PATROL_RADIUS_DEFAULT;
}

static inline float police_patrol_anchor_x(int id) {
    return (id >= 0 && id < POLICE_PATROL_MAX) ? g_police_anchor_x[id] : 0.0f;
}
static inline float police_patrol_anchor_z(int id) {
    return (id >= 0 && id < POLICE_PATROL_MAX) ? g_police_anchor_z[id] : 0.0f;
}

/* Where on the ring this car should be heading right now. Exposed so a test (and the minimap, later) can
 * ask the same question the steering does, instead of re-deriving the ring. */
static inline void police_patrol_target(int id, unsigned int now_ms, float *out_x, float *out_z) {
    float r = (id >= 0 && id < POLICE_PATROL_MAX) ? g_police_radius[id] : POLICE_PATROL_RADIUS_DEFAULT;
    if (r <= 1.0f) r = POLICE_PATROL_RADIUS_DEFAULT;
    /* spread the cruisers around the ring so two cars on one anchor do not drive nose to tail */
    unsigned int slot_deg = (unsigned int)((id < 0 ? 0 : id) * 137) % 360u;
    unsigned int lap_deg = (unsigned int)(((unsigned long long)(now_ms % POLICE_PATROL_PERIOD_MS) * 360ull)
                                          / (unsigned long long)POLICE_PATROL_PERIOD_MS);
    unsigned int deg = (lap_deg + slot_deg) % 360u;
    float rad = (float)deg * 0.017453292f;
    *out_x = police_patrol_anchor_x(id) + sinf(rad) * r;
    *out_z = police_patrol_anchor_z(id) + cosf(rad) * r;
}

/* The intent the host hands to simulate_buggy_state_ex. throttle 0..1 (never reverse: a patrol that
 * backs up is a bug), steer -1..1. Close to the target it eases off rather than parking, because the ring
 * keeps turning and a stopped cruiser would stop sounding its siren. */
static inline void police_patrol_intent(const BuggyState *b, unsigned int now_ms,
                                        float *out_throttle, float *out_steer) {
    float throttle = 0.0f, steer = 0.0f;
    if (b && b->active) {
        float tx, tz;
        police_patrol_target(b->id, now_ms, &tx, &tz);
        float dx = tx - b->x, dz = tz - b->z;
        float dist = sqrtf(dx * dx + dz * dz);
        float bearing = atan2f(dx, dz) * 57.29577951f;   /* 0 = +Z, the engine's own yaw convention */
        if (bearing < 0.0f) bearing += 360.0f;
        float rel = bearing - b->yaw;
        while (rel > 180.0f) rel -= 360.0f;
        while (rel < -180.0f) rel += 360.0f;
        /* steer hard when the target is behind, straighten as it comes ahead */
        steer = rel / 60.0f;
        if (steer > 1.0f) steer = 1.0f;
        if (steer < -1.0f) steer = -1.0f;
        /* keep rolling while turning -- a car that stops to turn never catches a ring that keeps moving */
        throttle = (rel > 75.0f || rel < -75.0f) ? 0.45f : 1.0f;
        if (dist < 10.0f) throttle = 0.6f;
    }
    if (out_throttle) *out_throttle = throttle;
    if (out_steer) *out_steer = steer;
}

#endif /* POLICE_PATROL_H */
