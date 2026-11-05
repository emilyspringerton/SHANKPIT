#ifndef POLICE_PATROL_H
#define POLICE_PATROL_H

/* police_patrol.h -- the automated cruiser: a police rocket car with nobody in it, driving a beat.
 *
 * Founder (2026-11-05): "add automated police cars to the real zombies world ... the police ones when you
 * are boosting or hitting space for hover the siren is triggered by those things", then "add patrol lanes",
 * then "two way lanes".
 * Four halves, all here or next door:
 *
 *   1. A police car SOUNDS its siren when the driver boosts or hovers (PARENA's siren-trigger decides;
 *      physics.h records the driver's command in BuggyState.siren).
 *   2. A police car with no driver still drives: it follows a LANE -- the level's own authored nav/waypoint
 *      graph, the same nodes the AI pathfinder uses -- and falls back to a ring around its anchor when the
 *      level authored none. Lanes matter because a ring is a circle in the middle of a city: it cuts corners
 *      through walls, it never uses the roads the level actually has, and every cruiser in the level drives
 *      the same shape.
 *   3. A LANE HAS A DIRECTION, and each cruiser is given one. A single undirected beat means every cruiser
 *      drives the centreline in whatever order the level happened to author it, so two of them meeting head
 *      on share a lane. Now slots alternate direction and each one keeps RIGHT of the centreline by
 *      POLICE_PATROL_KEEP_RIGHT while it travels, which is the one rule that turns a waypoint list into
 *      traffic: the same authored nodes carry both directions of a two-way road.
 *   4. The third arm of the siren rule (a moving unmanned cruiser) is what makes all of that audible.
 *
 * This header is steering only -- a stateless target plus a per-slot waypoint index and direction -- so it is
 * testable without physics, a device or a clock, and cannot drift into a second vehicle implementation. The
 * host still calls the ordinary simulate_buggy_state_ex with the intent, so a cruiser is a buggy with
 * boosters and not a special case the collision code has never seen. */

#include <math.h>

#include "protocol.h"

#ifndef POLICE_PATROL_MAX
#define POLICE_PATROL_MAX 8
#endif
#define POLICE_PATROL_LANE_MAX      12       /* waypoints per cruiser; a beat, not a route map */
#define POLICE_PATROL_RADIUS_DEFAULT 60.0f
#define POLICE_PATROL_PERIOD_MS     48000u   /* one lap of the fallback ring per 48 s */
#define POLICE_PATROL_WAYPOINT_HIT  12.0f    /* how close counts as "reached this waypoint" */
/* How far RIGHT of the authored centreline a cruiser drives, in world units. A lane is authored down the
 * middle of a road (that is what a nav node is), so keeping right is what puts a northbound cruiser on the
 * east half and a southbound one on the west half of the SAME nodes. 4 units is roughly one car width of
 * clearance either side -- enough to pass, small enough that the cruiser still counts as on the lane. */
#define POLICE_PATROL_KEEP_RIGHT    4.0f

static float g_police_anchor_x[POLICE_PATROL_MAX];
static float g_police_anchor_z[POLICE_PATROL_MAX];
static float g_police_radius[POLICE_PATROL_MAX];
static float g_police_lane_x[POLICE_PATROL_MAX][POLICE_PATROL_LANE_MAX];
static float g_police_lane_z[POLICE_PATROL_MAX][POLICE_PATROL_LANE_MAX];
static int   g_police_lane_n[POLICE_PATROL_MAX];
static int   g_police_lane_wp[POLICE_PATROL_MAX];
static int   g_police_lane_dir[POLICE_PATROL_MAX];   /* +1 forward along the authored order, -1 backward */

/* Anchor a cruiser's fallback ring. Called once when the car is spawned; the ring lives with the slot, so a
 * respawned or recycled buggy does not inherit somebody else's patrol. */
static inline void police_patrol_set_anchor(int id, float cx, float cz, float radius) {
    if (id < 0 || id >= POLICE_PATROL_MAX) return;
    g_police_anchor_x[id] = cx;
    g_police_anchor_z[id] = cz;
    g_police_radius[id] = (radius > 1.0f) ? radius : POLICE_PATROL_RADIUS_DEFAULT;
}

/* The lane: points the LEVEL authored (its nav graph), copied in, so the patrol cannot outlive the level
 * data. Fewer than two points is not a lane -- clear it and the ring takes over. A fresh lane runs FORWARD
 * along the authored order; police_patrol_set_direction is what makes it two-way. */
static inline void police_patrol_set_lane(int id, const float *xs, const float *zs, int count) {
    if (id < 0 || id >= POLICE_PATROL_MAX) return;
    if (!xs || !zs || count < 2) { g_police_lane_n[id] = 0; g_police_lane_wp[id] = 0; return; }
    if (count > POLICE_PATROL_LANE_MAX) count = POLICE_PATROL_LANE_MAX;
    for (int i = 0; i < count; i++) {
        g_police_lane_x[id][i] = xs[i];
        g_police_lane_z[id][i] = zs[i];
    }
    g_police_lane_n[id] = count;
    g_police_lane_wp[id] = 0;
    g_police_lane_dir[id] = 1;
}

static inline void police_patrol_clear_lane(int id) {
    if (id < 0 || id >= POLICE_PATROL_MAX) return;
    g_police_lane_n[id] = 0;
    g_police_lane_wp[id] = 0;
    g_police_lane_dir[id] = 1;
}

/* Which way round the beat this cruiser runs. dir < 0 travels the authored points in reverse; anything else
 * is forward. Only the SIGN matters, so a caller can hand it a velocity or a slot parity without caring. */
static inline void police_patrol_set_direction(int id, int dir) {
    if (id < 0 || id >= POLICE_PATROL_MAX) return;
    g_police_lane_dir[id] = (dir < 0) ? -1 : 1;
}

/* Give every cruiser the same authored lane, each starting at a different waypoint AND alternating
 * direction, so a patrol spreads along the beat and the same nodes carry both directions of the road
 * instead of a convoy driving nose to tail down the centreline. */
static inline void police_patrol_set_lane_from_graph(const float *xs, const float *zs, int count) {
    for (int id = 0; id < POLICE_PATROL_MAX; id++) {
        police_patrol_set_lane(id, xs, zs, count);
        int dir = (id & 1) ? -1 : 1;
        g_police_lane_dir[id] = dir;
        if (count >= 2 && g_police_lane_n[id] > 0) {
            int n = g_police_lane_n[id];
            int start = (id / 2) % n;                 /* pairs share a start, one at each end of the road */
            g_police_lane_wp[id] = (dir > 0) ? start : ((n - start) % n);
        }
    }
}

static inline int police_patrol_has_lane(int id) {
    return (id >= 0 && id < POLICE_PATROL_MAX) ? g_police_lane_n[id] : 0;
}
static inline int police_patrol_waypoint(int id) {
    return (id >= 0 && id < POLICE_PATROL_MAX) ? g_police_lane_wp[id] : 0;
}
static inline int police_patrol_direction(int id) {
    return (id >= 0 && id < POLICE_PATROL_MAX) ? g_police_lane_dir[id] : 0;
}
static inline float police_patrol_anchor_x(int id) {
    return (id >= 0 && id < POLICE_PATROL_MAX) ? g_police_anchor_x[id] : 0.0f;
}
static inline float police_patrol_anchor_z(int id) {
    return (id >= 0 && id < POLICE_PATROL_MAX) ? g_police_anchor_z[id] : 0.0f;
}

/* The centreline point this cruiser is heading for, before the keep-right offset: the lane's current
 * waypoint when the level authored one, otherwise a point on the fallback ring (pure in the clock, so the
 * ring never desyncs). */
static inline void police_patrol_waypoint_target(int id, unsigned int now_ms, float *out_x, float *out_z) {
    if (id < 0 || id >= POLICE_PATROL_MAX) {
        if (out_x) *out_x = 0.0f;
        if (out_z) *out_z = 0.0f;
        return;
    }
    if (g_police_lane_n[id] > 0) {
        int wp = g_police_lane_wp[id];
        if (wp < 0 || wp >= g_police_lane_n[id]) wp = 0;
        if (out_x) *out_x = g_police_lane_x[id][wp];
        if (out_z) *out_z = g_police_lane_z[id][wp];
        return;
    }
    float r = g_police_radius[id];
    if (r <= 1.0f) r = POLICE_PATROL_RADIUS_DEFAULT;
    unsigned int slot_deg = (unsigned int)(id * 137) % 360u;
    unsigned int lap_deg = (unsigned int)(((unsigned long long)(now_ms % POLICE_PATROL_PERIOD_MS) * 360ull)
                                          / (unsigned long long)POLICE_PATROL_PERIOD_MS);
    unsigned int deg = (lap_deg + slot_deg) % 360u;
    float rad = (float)deg * 0.017453292f;
    if (out_x) *out_x = g_police_anchor_x[id] + sinf(rad) * r;
    if (out_z) *out_z = g_police_anchor_z[id] + cosf(rad) * r;
}

/* Where this cruiser should actually put its wheels: the centreline target taken KEEP_RIGHT to the right of
 * the direction of travel. On the ring fallback there is no lane direction to keep right of, so the offset is
 * skipped and the ring's own tangent does that job. */
static inline void police_patrol_target(int id, unsigned int now_ms, float *out_x, float *out_z) {
    float tx = 0.0f, tz = 0.0f;
    police_patrol_waypoint_target(id, now_ms, &tx, &tz);
    if (id >= 0 && id < POLICE_PATROL_MAX && g_police_lane_n[id] > 1) {
        int n = g_police_lane_n[id];
        int dir = (g_police_lane_dir[id] < 0) ? -1 : 1;
        int wp = g_police_lane_wp[id];
        if (wp < 0 || wp >= n) wp = 0;
        int prev = ((wp - dir) % n + n) % n;
        float fx = g_police_lane_x[id][wp] - g_police_lane_x[id][prev];
        float fz = g_police_lane_z[id][wp] - g_police_lane_z[id][prev];
        float len = sqrtf(fx * fx + fz * fz);
        if (len > 0.001f) {
            fx /= len; fz /= len;
            /* right of travel: for forward (0,+Z) that is (+X,0), the engine's own yaw convention */
            tx += fz * POLICE_PATROL_KEEP_RIGHT;
            tz += -fx * POLICE_PATROL_KEEP_RIGHT;
        }
    }
    if (out_x) *out_x = tx;
    if (out_z) *out_z = tz;
}

/* The intent the host hands to simulate_buggy_state_ex. throttle 0..1 (never reverse: a patrol that backs
 * up is a bug), steer -1..1. Reaching a lane waypoint advances it -- forward or backward along the beat,
 * according to this slot's direction -- which is the only state this adds. */
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
        steer = rel / 60.0f;
        if (steer > 1.0f) steer = 1.0f;
        if (steer < -1.0f) steer = -1.0f;
        /* keep rolling while turning -- a car that stops to turn never catches a beat that keeps moving */
        throttle = (rel > 75.0f || rel < -75.0f) ? 0.45f : 1.0f;
        if (dist < 10.0f) throttle = 0.6f;
        /* arrived: take the next waypoint on the beat, in THIS cruiser's direction. Only a LANE advances; the
         * ring keeps turning on its own clock, so there is nothing to advance there. The test is against the
         * centreline, not the offset target, so a car that drifts to the wrong side of the road still passes
         * its waypoint instead of circling it. */
        if (b->id >= 0 && b->id < POLICE_PATROL_MAX && g_police_lane_n[b->id] > 0) {
            float cx, cz;
            police_patrol_waypoint_target(b->id, now_ms, &cx, &cz);
            float cdx = cx - b->x, cdz = cz - b->z;
            if (sqrtf(cdx * cdx + cdz * cdz) < POLICE_PATROL_WAYPOINT_HIT) {
                int n = g_police_lane_n[b->id];
                int dir = (g_police_lane_dir[b->id] < 0) ? -1 : 1;
                g_police_lane_wp[b->id] = ((g_police_lane_wp[b->id] + dir) % n + n) % n;
            }
        }
    }
    if (out_throttle) *out_throttle = throttle;
    if (out_steer) *out_steer = steer;
}

#endif /* POLICE_PATROL_H */
