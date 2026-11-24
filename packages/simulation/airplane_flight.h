/* airplane_flight.h -- the C-110 twin-prop transport (founder 2026-10-31: "how about a basic prop plane
 * c-110 or whatever the legendary ones are").
 *
 * The one vehicle in this engine that cannot hover, and that is the design: lift is a function of
 * AIRSPEED, not of a lever, so it has to be flown. It is deliberately not "the helicopter with wings":
 *
 *   - no lift at or below the stall speed, whatever the nose is doing, so a bad climb falls out of the sky
 *   - a real takeoff roll, driven with W on the wheels the same way the rocket car is (the SPACE/CTRL
 *     lever still works, and in the air it is the only throttle: it stays where you leave it)
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

/* Handling, tuned to the rocket car's arcade model rather than to a real aeroplane (founder 2026-10-31:
 * "it should handle similar to the rocket car"). Three things that car has and this one did not:
 *
 *   - a throttle you DRIVE with W, not a lever you have to wind up with SPACE. On the wheels W/S now
 *     spools the engine (S brakes back to idle, then reverses), so a taxi is just holding W.
 *   - a bounded climb. The old model integrated lift-minus-weight forever, so full throttle ballooned
 *     from y=6 to y=2200 in ten seconds and the airframe felt like it was on a string. Vertical speed
 *     now chases a target set by the lift SURPLUS, and the surplus itself is capped, so it climbs at a
 *     sane rate and settles instead of accelerating.
 *   - trim. Release the stick and the aeroplane settles back to level, like the car's suspension does,
 *     instead of holding the last input forever.
 */
#define AF_MAX_LIFT_RATIO 1.25f   /* hard cap on lift/weight -- the anti-balloon clamp */
#define AF_CLIMB_RATE 0.8f        /* units/tick of climb per 1.0 of lift surplus */
#define AF_VSPEED_RESPONSE 0.15f  /* how fast vy chases that target (per tick) */
/* Attitude control, straight from Bedrock Racers (PARENA/stdlib/bedrockracers/web/br_phys.prn
 * fly-sub!): a stick commands a RATE, the rate chases it through a first-order lag, and the moment the
 * stick is let go the rate decays to zero. That is the difference between this and the old model, which
 * added degrees to an angle every tick, so a tap banked the aeroplane and it kept turning -- the "floppy"
 * report. Bank is no longer integrated at all: it follows the yaw rate (a coordinated turn), so there is
 * no bank -> yaw -> bank feedback loop left to oscillate. */
#define AF_PITCH_RATE 2.2f        /* deg/tick at full stick (~132 deg/s) */
#define AF_YAW_RATE 1.4f          /* deg/tick at full rudder (~84 deg/s) */
#define AF_ATTITUDE_RESPONSE 0.45f  /* per tick; Bedrock Racers' own 0.27 per 8 ms substep, ~0.54/tick */
#define AF_BANK_PER_YAW 45.0f     /* degrees of bank at full yaw rate */
#define AF_PITCH_TRIM 0.02f       /* per-tick weathervane back to level with the stick centred */
/* Lateral grip: the airframe flies where the nose points instead of keeping the sideways velocity of
 * its previous heading. This is the aeroplane's version of the rocket car's tyre grip, and its absence
 * is why a turn used to end with the aeroplane sliding sideways. */
#define AF_AIR_GRIP 0.06f         /* per tick, in the air */
#define AF_GROUND_GRIP 0.30f      /* per tick, on the wheels */
#define AF_GROUND_SPOOL_UP 900.0f     /* throttle units per SECOND while W is held on the wheels */
#define AF_GROUND_SPOOL_DOWN 1400.0f  /* S: brake back to idle */
#define AF_GROUND_IDLE_DECAY 500.0f   /* no key held: the engine falls back to idle, like lifting off */

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

/* ---- FLIGHT-0/1 (founder 2026-11-24: "the PROP plane is super jank ... behave more like a real flight
 * simulator, its super floppy") ----------------------------------------------------------------------
 * What was jank, concretely (diagnosed from the model above and the lobby glue):
 *   1. The rudder was the ONLY turn control, and positive rudder INCREASED yaw -- but this engine's yaw
 *      grows to the LEFT (mouse-right does cam_yaw -= xrel), so D turned the aeroplane left.
 *   2. Bank was a visual that FOLLOWED yaw; there was no aileron axis and the roll never turned the
 *      aeroplane. A real wing turns because it is banked (the lift vector tilts).
 *   3. Pitch was only a number feeding a lift term: the flight path did not follow the nose, so pulling
 *      up changed the picture but not where it went, and climbing cost no speed.
 *   4. Keyboard W was nose-UP (inverted against every sim) and doubled as the throttle on the wheels.
 *   5. The camera heading dragged the nose on the ground (a tiller fighting the pedals) and the wheels
 *      let go on speed alone, so a takeoff was a hop with no rotation.
 *   6. No wheel brakes, no stall warning, no recoverable stall, no trim/assist, no analog stick path.
 * The laws that fix it live in PARENA/stdlib/shankpit/airplane_rules.prn (permille / milli-degree ints);
 * this function is the integration. */

/* The pilot's controls, already shaped. Axes are -1..1: pitch +1 = pull back (nose up), roll +1 = bank
 * right, rudder +1 = right pedal. The keyboard, the gamepad and (later) rebinding all produce this one
 * struct, so the flight model never sees which device is flying it. */
typedef struct {
    float pitch, roll, rudder;
    float throttle_set;      /* 0..1 = drive the persistent lever to this value; < 0 = leave it alone */
    int throttle_up, throttle_down;   /* lever keys: ramp the lever, it stays where you leave it */
    float ground_throttle;   /* keyboard taxi: W/S spools the engine on the wheels (-1..1) */
    int kb_ground_spool;     /* 1 = keyboard owns the lever on the wheels (idle decay when no key is down) */
    float brake;             /* 0..1 wheel brakes (ground only) */
    int barrel;              /* -1/0/1 commanded barrel roll (J/L) */
    int assist;              /* 1 = attitude-hold assist, 0 = sim (rate command, attitude holds) */
} AirplaneControls;

static inline void airplane_controls_neutral(AirplaneControls *c) {
    memset(c, 0, sizeof(*c));
    c->throttle_set = -1.0f;
    c->assist = 1;
}

/* The local pilot's shaped controls, handed to the simulation by the host (apps/lobby builds them with
 * airplane_input.h each frame). local_game.h's airplane tick uses these for the aeroplane whose occupant
 * is g_airplane_pilot_pid, and falls back to the legacy PlayerState key mapping for anyone else. */
static AirplaneControls g_airplane_pilot_controls;
static int g_airplane_pilot_pid = -1;
static inline void airplane_pilot_set(int pid, const AirplaneControls *c) {
    g_airplane_pilot_pid = pid;
    if (c) g_airplane_pilot_controls = *c;
}
static inline const AirplaneControls *airplane_pilot_for(int occupant_pid) {
    return (occupant_pid >= 0 && occupant_pid == g_airplane_pilot_pid) ? &g_airplane_pilot_controls : NULL;
}

/* PARENA-generated FLIGHT-0/1 laws (airplane_rules.c). Called directly: anything that calls
 * airplane_step links airplane_rules.c, exactly as every host already does. */
int airplane_pitch_cmd_milli(int stick, int speed_milli, int stall_milli, int pitch_mdeg, int assist);
int airplane_roll_cmd_milli(int stick, int speed_milli, int roll_mdeg, int assist);
int airplane_turn_rate_milli(int roll_mdeg, int speed_milli);
int airplane_rudder_yaw_milli(int rudder);
int airplane_ground_steer_milli(int steer, int speed_milli);
int airplane_vspeed_target_milli(int speed_milli, int pitch_mdeg, int flaps);
int airplane_pitch_accel_milli(int pitch_mdeg);
int airplane_stall_warn_permille(int speed_milli, int stall_milli);
int airplane_brake_permille(int brake);
int airplane_unstick(int speed_milli, int pitch_mdeg, int flaps);
int airplane_rotate_speed_milli(int flaps);
int airplane_ground_pitch_target_milli(int stick, int speed_milli, int flaps, int assist);

static inline float af_clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int af_permille(float axis) { return (int)(af_clampf(axis, -1.0f, 1.0f) * 1000.0f); }

static inline void airplane_toggle_flaps(AirplaneState *a) { if (a) a->flaps = a->flaps ? 0 : 1; }

/* airplane_step -- one tick. */
static inline void airplane_step(AirplaneState *a, const AirplaneControls *c, float dt) {
    if (!a || !a->active || !c) return;
    float dts = dt / 0.016f;
    if (dts < 0.0f) dts = 0.0f;
    if (dts > 4.0f) dts = 4.0f;
    float pitch_in = af_clampf(c->pitch, -1.0f, 1.0f);
    float roll_in = af_clampf(c->roll, -1.0f, 1.0f);
    float rudder_in = af_clampf(c->rudder, -1.0f, 1.0f);
    int barrel = c->barrel > 0 ? 1 : (c->barrel < 0 ? -1 : 0);
    int assist = c->assist ? 1 : 0;

    /* the lever: persistent. Keys ramp it; an analog target drives it there at a bounded rate. */
    if (c->throttle_set >= 0.0f) {
        float want = af_clampf(c->throttle_set, 0.0f, 1.0f) * 1000.0f;
        float step = 600.0f * dts / 60.0f;
        a->throttle += af_clampf(want - a->throttle, -step, step);
    }
    if (c->throttle_up) a->throttle += AF_THROTTLE_RATE * dts / 60.0f;
    if (c->throttle_down) a->throttle -= AF_THROTTLE_RATE * dts / 60.0f;
    if (a->grounded && c->kb_ground_spool) {
        float g = af_clampf(c->ground_throttle, -1.0f, 1.0f);
        if (g > 0.05f) a->throttle += AF_GROUND_SPOOL_UP * g * dts / 60.0f;
        else if (g < -0.05f) a->throttle += AF_GROUND_SPOOL_DOWN * g * dts / 60.0f;
        else if (!c->throttle_up && !c->throttle_down)
            a->throttle -= AF_GROUND_IDLE_DECAY * dts / 60.0f;
    }
    a->throttle = af_clampf(a->throttle, 0.0f, 1000.0f);

    float airspeed = sqrtf(a->vx * a->vx + a->vy * a->vy + a->vz * a->vz);
    int speed_milli = (int)(airspeed * 1000.0f);
    int stall_milli = (int)(af_stall(a->flaps) * 1000.0f);
    a->stall_warn = a->grounded ? 0 : airplane_stall_warn_permille(speed_milli, stall_milli);
    int flying = speed_milli > stall_milli;
    int rolling_barrel = 0;
    float turn_rate = 0.0f;   /* deg/tick, + = turn right */

    if (a->grounded) {
        /* On the gear the wings carry no authority: the nose holds level until rotation speed, then the
         * stick (sim) or the automatic rotation (assist) lifts it. Roll stays flat. */
        float tgt = (float)airplane_ground_pitch_target_milli(af_permille(pitch_in), speed_milli, a->flaps, assist) * 0.001f;
        a->pitch += (tgt - a->pitch) * 0.08f * dts;
        a->roll *= 0.80f;
        a->roll_rate = 0.0f;
        a->pitch_rate = 0.0f;
        float steer = af_clampf(rudder_in + roll_in, -1.0f, 1.0f);   /* pedals or stick: nosewheel */
        float want = -(float)airplane_ground_steer_milli(af_permille(steer), speed_milli) * 0.001f;
        a->yaw_rate += (want - a->yaw_rate) * AF_ATTITUDE_RESPONSE * dts;
    } else {
        /* pitch: PARENA rate command, first-order lag (no overshoot of the rate by construction). */
        float pcmd = (float)airplane_pitch_cmd_milli(af_permille(pitch_in), speed_milli, stall_milli,
                                                     (int)(a->pitch * 1000.0f), assist) * 0.001f;
        a->pitch_rate += (pcmd - a->pitch_rate) * 0.25f * dts;
        a->pitch += a->pitch_rate * dts;

        if (barrel != 0 && flying) {
            rolling_barrel = 1;
            a->roll += (float)barrel * 3.6f * dts;     /* a COMMANDED corkscrew: 360 in ~1.7 s */
            a->roll_rate = 0.0f;
        } else {
            float rcmd = (float)airplane_roll_cmd_milli(af_permille(roll_in), speed_milli,
                                                        (int)(a->roll * 1000.0f), assist) * 0.001f;
            a->roll_rate += (rcmd - a->roll_rate) * 0.30f * dts;
            a->roll += a->roll_rate * dts;
            if (!flying) a->roll *= 0.985f;
            if (a->roll > 75.0f) a->roll = 75.0f;
            if (a->roll < -75.0f) a->roll = -75.0f;
            /* bank-to-turn: the lift vector is tilted, so the heading changes. No bank, no turn. */
            turn_rate = (float)airplane_turn_rate_milli((int)(a->roll * 1000.0f), speed_milli) * 0.001f;
        }
        float want = -turn_rate + (float)airplane_rudder_yaw_milli(af_permille(rudder_in)) * -0.001f;
        a->yaw_rate += (want - a->yaw_rate) * 0.35f * dts;
    }
    if (a->pitch > 32.0f) a->pitch = 32.0f;
    if (a->pitch < -32.0f) a->pitch = -32.0f;
    if (rolling_barrel) {
        if (a->roll > 180.0f) a->roll -= 360.0f;
        if (a->roll < -180.0f) a->roll += 360.0f;
        a->yaw_rate = 0.0f;
    } else {
        a->yaw = norm_yaw_deg(a->yaw + a->yaw_rate * dts);
    }

    float r = -a->yaw * 0.0174532925f;
    float fx = sinf(r), fz = -cosf(r);

    /* thrust along the nose, drag against the air, and the pitch's trade of height for speed */
    float along = af_thrust((int)a->throttle) - af_drag(speed_milli, a->flaps)
                + (a->grounded ? 0.0f : (float)airplane_pitch_accel_milli((int)(a->pitch * 1000.0f)) * 0.001f);
    a->vx += fx * along * dts;
    a->vz += fz * along * dts;

    /* lateral grip: it flies where the nose points */
    {
        float rxv = cosf(r), rzv = sinf(r);
        float fwd_v = a->vx * fx + a->vz * fz;
        float lat_v = a->vx * rxv + a->vz * rzv;
        float grip = (a->grounded ? AF_GROUND_GRIP : AF_AIR_GRIP) * dts;
        if (grip > 1.0f) grip = 1.0f;
        lat_v *= (1.0f - grip);
        a->vx = fx * fwd_v + rxv * lat_v;
        a->vz = fz * fwd_v + rzv * lat_v;
    }

    /* Vertical: the flight path follows the nose (PARENA target = speed*sin(pitch) minus the sink of a
     * wing short of lift), vy chases it. On the wheels vy is pinned until the PARENA unstick rule
     * (rotation speed AND a rotated nose) lets go -- a takeoff, not a hop. */
    int unstick = a->grounded && airplane_unstick(speed_milli, (int)(a->pitch * 1000.0f), a->flaps);
    int wheels_down = a->grounded && !unstick;
    if (!wheels_down) {
        float tvy = (float)airplane_vspeed_target_milli(speed_milli, (int)(a->pitch * 1000.0f), a->flaps) * 0.001f;
        if (rolling_barrel) tvy *= 0.6f + 0.4f * cosf(a->roll * 0.0174532925f);
        a->vy += (tvy - a->vy) * AF_VSPEED_RESPONSE * dts;
    }

    a->prop_angle += (35.0f + a->throttle * 0.045f) * dts;
    if (a->prop_angle > 360.0f) a->prop_angle -= 360.0f;
    a->airspeed = sqrtf(a->vx * a->vx + a->vy * a->vy + a->vz * a->vz);

    float nx = a->x + a->vx * dts;
    float nz = a->z + a->vz * dts;
    float nose_x = nx + fx * 7.0f, nose_z = nz + fz * 7.0f;
    if (heli_point_collides(nx, a->y, nz) || heli_point_collides(nose_x, a->y, nose_z)) {
        a->vx *= 0.08f;
        a->vz *= 0.08f;
        a->health -= 25.0f;
        if (a->health < 0.0f) a->health = 0.0f;
    } else {
        a->x = nx;
        a->z = nz;
    }
    float ground = af_world_y(a->x, a->z, a->y + 1.0f);
    if (wheels_down) {
        a->y = ground;
        a->vy = 0.0f;
        a->grounded = 1;
    } else {
        a->y += a->vy * dts;
        if (a->y <= ground) {
            a->y = ground;
            if (a->vy < 0.0f) a->vy = 0.0f;
            a->grounded = 1;
            a->pitch *= 0.85f;
            a->roll *= 0.85f;
        } else {
            a->grounded = 0;
        }
    }
    a->brake_on = 0;
    if (a->grounded) {
        float resist = (a->throttle < 1.0f) ? 0.97f : 0.9995f;
        a->vx *= resist;
        a->vz *= resist;
        if (c->brake > 0.02f) {
            float k = 1.0f - (float)airplane_brake_permille(af_permille(c->brake)) * 0.001f * dts;
            if (k < 0.0f) k = 0.0f;
            a->vx *= k;
            a->vz *= k;
            a->brake_on = 1;
        }
    }
}

/* airplane_simulate_step -- the OLD signature, kept so any caller that still has it compiles. It is now a
 * thin mapping onto airplane_step in SIM mode: elevator is the pitch stick (+ = nose up), rudder drives
 * the pedals (and the nosewheel on the wheels), roll_dir is the barrel roll, cam_yaw is ignored (the
 * camera-heading tiller is gone). On the wheels the elevator is also the taxi throttle, as before. */
static inline void airplane_simulate_step(AirplaneState *a, float elevator, float rudder, int roll_dir,
                                          float cam_yaw, int throttle_up, int throttle_down, float dt) {
    (void)cam_yaw;
    AirplaneControls c;
    airplane_controls_neutral(&c);
    c.pitch = elevator;
    c.rudder = rudder;
    c.barrel = roll_dir;
    c.throttle_up = throttle_up;
    c.throttle_down = throttle_down;
    c.ground_throttle = elevator;
    c.kb_ground_spool = 1;
    c.assist = 0;
    airplane_step(a, &c, dt);
}

#endif /* SHANKPIT_AIRPLANE_FLIGHT_H */
