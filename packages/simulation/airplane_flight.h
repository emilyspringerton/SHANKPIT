/* airplane_flight.h -- the C-110 twin-prop transport (founder 2026-10-31: "how about a basic prop plane
 * c-110 or whatever the legendary ones are").
 *
 * The one vehicle in this engine that cannot hover, and that is the design: lift is a function of
 * AIRSPEED, not of a lever, so it has to be flown. It is deliberately not "the helicopter with wings":
 *
 *   - no lift at or below the stall speed, whatever the nose is doing, so a bad climb falls out of the sky
 *   - a real takeoff roll (throttle is a LEVER, it stays where you leave it) on a runway-length run
 *   - a bank turns it (rudder only matters while taxiing), and every rate is heavy compared to a quad
 *   - flaps lower the stall speed and add drag -- the short-field option
 *
 * Numbers are PARENA (PARENA/stdlib/shankpit/airplane_rules.prn, generated into airplane_rules.c,
 * installed by airplane_rules_host.h); this header is the integration. With nothing installed the
 * built-in defaults below are used, so a test that only includes this header still runs.
 *
 * Not modelled, named rather than faked: no cargo bay, no ramp, no airborne spawn, no cargo drops. This
 * is an airframe you can fly, land and roll, not a mobile base. */
#ifndef SHANKPIT_AIRPLANE_FLIGHT_H
#define SHANKPIT_AIRPLANE_FLIGHT_H

#include "protocol.h"
#include "net_sim.h"   /* heli_point_collides -- the same world query every other vehicle collides with */

typedef struct {
    int (*top_speed_milli)(void);
    int (*stall_milli)(int flaps);
    int (*liftoff_milli)(void);
    int (*thrust_milli)(int throttle);
    int (*drag_milli)(int speed_milli, int flaps);
    int (*lift_permille)(int speed_milli, int flaps, int aoa_permille);
    int (*hull)(void);
} AirplaneRules;
static const AirplaneRules *g_airplane_rules = NULL;
static inline void airplane_set_rules(const AirplaneRules *r) { g_airplane_rules = r; }

/* Built-in defaults, equal to the PARENA mod's own values. */
#define AF_TOP_SPEED 12.0f
#define AF_LIFTOFF 6.0f
#define AF_STALL 4.0f
#define AF_STALL_FLAPS 3.2f
#define AF_HULL 600
#define AF_GRAVITY 0.02f          /* units/tick^2, the same order as every other vehicle's */
#define AF_THROTTLE_RATE 350.0f   /* lever units per SECOND */
#define AF_TAXI_CLEARANCE 0.55f   /* how high the gear holds the fuselage off the ground */

static inline float af_top_speed(void) {
    if (g_airplane_rules && g_airplane_rules->top_speed_milli) {
        float v = (float)g_airplane_rules->top_speed_milli() * 0.001f;
        if (v > 0.1f) return v;
    }
    return AF_TOP_SPEED;
}
static inline float af_stall(int flaps) {
    if (g_airplane_rules && g_airplane_rules->stall_milli) {
        float v = (float)g_airplane_rules->stall_milli(flaps) * 0.001f;
        if (v > 0.05f) return v;
    }
    return flaps ? AF_STALL_FLAPS : AF_STALL;
}
static inline float af_liftoff(void) {
    if (g_airplane_rules && g_airplane_rules->liftoff_milli) {
        float v = (float)g_airplane_rules->liftoff_milli() * 0.001f;
        if (v > 0.05f) return v;
    }
    return AF_LIFTOFF;
}
static inline float af_thrust(int throttle) {
    if (g_airplane_rules && g_airplane_rules->thrust_milli)
        return (float)g_airplane_rules->thrust_milli(throttle) * 0.001f;
    if (throttle < 0) throttle = 0;
    if (throttle > 1000) throttle = 1000;
    return (float)(throttle * 60 / 1000) * 0.001f;
}
static inline float af_drag(int speed_milli, int flaps) {
    if (g_airplane_rules && g_airplane_rules->drag_milli)
        return (float)g_airplane_rules->drag_milli(speed_milli, flaps) * 0.001f;
    if (speed_milli < 0) speed_milli = 0;
    if (speed_milli > 20000) speed_milli = 20000;
    return (float)(speed_milli * 5 / 1000 + (flaps ? 4 : 0)) * 0.001f;
}
static inline int af_lift_permille(int speed_milli, int flaps, int aoa_permille) {
    if (g_airplane_rules && g_airplane_rules->lift_permille)
        return g_airplane_rules->lift_permille(speed_milli, flaps, aoa_permille);
    int stall = (int)(af_stall(flaps) * 1000.0f);
    if (speed_milli <= stall) return 0;
    if (aoa_permille < 0) aoa_permille = 0;
    if (aoa_permille > 2000) aoa_permille = 2000;
    int aoa = flaps ? (aoa_permille * 1150 / 1000) : aoa_permille;   /* flaps make the wing bigger */
    if (aoa > 2000) aoa = 2000;
    int d = speed_milli - stall;
    int lift = (d * d / 4000) * aoa / 1000;
    if (lift > 1800) lift = 1800;
    return lift;
}
static inline int af_hull(void) {
    if (g_airplane_rules && g_airplane_rules->hull) return g_airplane_rules->hull();
    return AF_HULL;
}

static inline float af_ground_y(float x, float z) {
    float gy = terrain_sample_height(&g_scene_terrain, x, z);
    if (gy < 0.0f) gy = 0.0f;
    return gy + AF_TAXI_CLEARANCE;
}

/* af_level_floor -- the top of the highest level box under (x,z) at or below `ceiling`, or a value far
 * below the world when there is none. A bridge deck is a FLOOR: terrain was the only surface this model
 * knew, so a C-110 authored on a bridge spawned at terrain height underneath it and fell away from the
 * map. map_geo/map_count are the same active geometry heli_point_collides already tests against, so this
 * needs no host hook, no new dependency, and is exercised by the headless flight test with a real Box. */
static inline float af_level_floor(float x, float z, float ceiling) {
    float best = -1.0e30f;
    for (int i = 1; i < map_count; i++) {
        /* A destroyed brick is still in map_geo but not in the world -- do not land on it. */
        if (map_geo == g_custom_level_geo && i < CUSTOM_LEVEL_SLOT_CAP && g_custom_level_hidden[i]) continue;
        Box b = map_geo[i];
        if (x <= b.x - b.w * 0.5f || x >= b.x + b.w * 0.5f) continue;
        if (z <= b.z - b.d * 0.5f || z >= b.z + b.d * 0.5f) continue;
        float top = b.y + b.h * 0.5f;
        if (top <= ceiling && top > best) best = top;
    }
    return best;
}

/* af_world_y -- the surface under the gear: terrain, or a level box if there is one at or below the
 * ceiling the caller cares about. Spawning passes a huge ceiling ("the highest thing under the wheels"),
 * the per-tick clamp passes the hull's own height (so a plane flying OVER a building, or UNDER a bridge,
 * does not get snapped onto it). */
static inline float af_world_y(float x, float z, float ceiling) {
    float gy = terrain_sample_height(&g_scene_terrain, x, z);
    if (gy < 0.0f) gy = 0.0f;
    float deck = af_level_floor(x, z, ceiling);
    if (deck > gy) gy = deck;
    return gy + AF_TAXI_CLEARANCE;
}

static inline void airplane_spawn_defaults(AirplaneState *a, int id, int scene_id,
                                          float x, float z, float yaw) {
    memset(a, 0, sizeof(*a));
    a->active = 1;
    a->id = id;
    a->scene_id = scene_id;
    a->x = x;
    a->z = z;
    a->yaw = yaw;
    a->y = af_world_y(x, z, 1.0e30f);   /* the highest surface under the gear: a bridge deck counts */
    a->health = (float)af_hull();
    a->occupant_player_id = -1;
    a->grounded = 1;
}

static inline AirplaneState *airplane_find_nearby(ServerState *st, int scene_id, float x, float y, float z, float radius) {
    if (!st) return NULL;
    float rr = radius * radius;
    for (int i = 0; i < MAX_AIRPLANES; i++) {
        AirplaneState *a = &st->airplanes[i];
        if (!a->active || a->scene_id != scene_id) continue;
        float dx = a->x - x, dy = a->y - y, dz = a->z - z;
        if ((dx * dx + dy * dy + dz * dz) <= rr) return a;
    }
    return NULL;
}

static inline int airplane_try_enter(PlayerState *p, AirplaneState *a) {
    if (!p || !a || !a->active || a->occupant_player_id >= 0) return 0;
    a->occupant_player_id = p->id;
    p->in_vehicle = 1;
    p->vehicle_type = VEH_AIRPLANE;
    p->x = a->x; p->y = a->y; p->z = a->z;
    p->vx = p->vy = p->vz = 0.0f;
    p->on_ground = a->grounded;
    return 1;
}

static inline int airplane_try_exit(PlayerState *p, AirplaneState *a) {
    if (!p || !a || a->occupant_player_id != p->id) return 0;
    float r = -a->yaw * 0.0174532925f;
    float ox = cosf(r) * 3.0f, oz = sinf(r) * 3.0f;
    float px = a->x + ox, pz = a->z + oz;
    float py = terrain_sample_height(&g_scene_terrain, px, pz);
    if (py < 0.0f) py = 0.0f;
    p->x = px; p->y = py; p->z = pz;
    p->in_vehicle = 0;
    p->vehicle_type = VEH_NONE;
    p->on_ground = py <= a->y + 0.5f;
    a->occupant_player_id = -1;
    return 1;
}

/* airplane_simulate_step -- one tick.
 *   elevator  -1..1  W/S   (nose down / nose up; no authority below the stall speed)
 *   aileron   -1..1  A/D   (roll; a bank is what actually turns a fixed wing)
 *   cam_yaw          the pilot's heading -- used as a tiller, ONLY while taxiing
 *   throttle_up/down      throttle is a LEVER (space raises, ctrl lowers), it holds where you leave it
 */
static inline void airplane_simulate_step(AirplaneState *a, float elevator, float aileron,
                                          float cam_yaw, int throttle_up, int throttle_down, float dt) {
    if (!a || !a->active) return;
    if (elevator > 1.0f) elevator = 1.0f;
    if (elevator < -1.0f) elevator = -1.0f;
    if (aileron > 1.0f) aileron = 1.0f;
    if (aileron < -1.0f) aileron = -1.0f;
    float dts = dt / 0.016f;
    if (dts < 0.0f) dts = 0.0f;
    if (dts > 4.0f) dts = 4.0f;

    /* the lever */
    if (throttle_up) a->throttle += AF_THROTTLE_RATE * dts / 60.0f;
    if (throttle_down) a->throttle -= AF_THROTTLE_RATE * dts / 60.0f;
    if (a->throttle < 0.0f) a->throttle = 0.0f;
    if (a->throttle > 1000.0f) a->throttle = 1000.0f;

    float airspeed = sqrtf(a->vx * a->vx + a->vy * a->vy + a->vz * a->vz);
    int speed_milli = (int)(airspeed * 1000.0f);
    int stall_milli = (int)(af_stall(a->flaps) * 1000.0f);

    /* attitude. Above the stall the stick works; below it the wing is not flying and the nose falls --
     * that is the whole failure mode, and it is not recoverable by pulling harder. */
    if (speed_milli > stall_milli) {
        a->pitch += elevator * 3.2f * dts;
        a->roll += aileron * 5.0f * dts;
    } else {
        a->pitch -= 1.8f * dts;
        a->roll *= 0.985f;
    }
    if (a->pitch > 32.0f) a->pitch = 32.0f;
    if (a->pitch < -32.0f) a->pitch = -32.0f;
    if (a->roll > 60.0f) a->roll = 60.0f;
    if (a->roll < -60.0f) a->roll = -60.0f;

    /* turning: a bank turns it; the tiller only works with the wheels down and slow */
    a->yaw = norm_yaw_deg(a->yaw + a->roll * 0.05f * dts);
    if (a->grounded && speed_milli < stall_milli * 2) {
        float err = norm_yaw_deg(cam_yaw - a->yaw);
        if (err > 180.0f) err -= 360.0f;
        if (err < -180.0f) err += 360.0f;
        if (err > 1.0f) err = 1.0f;
        if (err < -1.0f) err = -1.0f;
        a->yaw = norm_yaw_deg(a->yaw + err * 1.4f * dts);
    }

    float r = -a->yaw * 0.0174532925f;
    float fx = sinf(r), fz = -cosf(r);

    /* thrust along the nose, drag against the air it is moving through */
    float along = af_thrust((int)a->throttle) - af_drag(speed_milli, a->flaps);
    a->vx += fx * along * dts;
    a->vz += fz * along * dts;

    /* lift versus weight: 1000 permille holds altitude, so anything above it climbs and a stall falls */
    int aoa = (int)(1000.0f + a->pitch * 10.0f);
    int lift = af_lift_permille(speed_milli, a->flaps, aoa);
    float lift_accel = (float)lift * 0.001f * AF_GRAVITY;
    a->vy += (lift_accel - AF_GRAVITY) * dts;

    /* propellers and the cached airspeed the HUD/tests read */
    a->prop_angle += (35.0f + a->throttle * 0.045f) * dts;
    if (a->prop_angle > 360.0f) a->prop_angle -= 360.0f;
    a->airspeed = sqrtf(a->vx * a->vx + a->vy * a->vy + a->vz * a->vz);

    /* the world */
    float nx = a->x + a->vx * dts;
    float nz = a->z + a->vz * dts;
    float nose_x = nx + fx * 7.0f, nose_z = nz + fz * 7.0f;
    if (heli_point_collides(nx, a->y, nz) || heli_point_collides(nose_x, a->y, nose_z)) {
        /* a transport does not bounce off a wall, it stops and needs maintenance */
        a->vx *= 0.08f;
        a->vz *= 0.08f;
        a->health -= 25.0f;
        if (a->health < 0.0f) a->health = 0.0f;
    } else {
        a->x = nx;
        a->z = nz;
    }
    a->y += a->vy * dts;
    float ground = af_world_y(a->x, a->z, a->y + 1.0f);   /* only surfaces at or below the hull */
    if (a->y <= ground) {
        a->y = ground;
        if (a->vy < 0.0f) a->vy = 0.0f;
        a->grounded = 1;
        a->pitch *= 0.85f;      /* sitting on the gear */
        a->roll *= 0.85f;
    } else {
        a->grounded = 0;
    }
}

#endif /* SHANKPIT_AIRPLANE_FLIGHT_H */
