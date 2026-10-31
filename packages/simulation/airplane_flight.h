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

/* airplane_simulate_step -- one tick.
 *   elevator  -1..1  W/S  (nose up / nose down; no authority below the stall speed)
 *   rudder    -1..1  A/D  (yaws the nose directly -- pedals in the air, nosewheel steering on the roll)
 *   roll_dir  -1/0/1 J/L and the arrow keys (a COMMANDED barrel roll: a full 360 turn, not a bank)
 *   cam_yaw          the pilot's heading -- still a tiller ASSIST at walking pace on the wheels
 *   throttle_up/down      LEVER trim (space raises, ctrl lowers), held between ticks; on the wheels W/S
 *                         spools the engine directly and S brakes back to idle
 */
static inline void airplane_simulate_step(AirplaneState *a, float elevator, float rudder, int roll_dir,
                                          float cam_yaw, int throttle_up, int throttle_down, float dt) {
    if (!a || !a->active) return;
    if (elevator > 1.0f) elevator = 1.0f;
    if (elevator < -1.0f) elevator = -1.0f;
    if (rudder > 1.0f) rudder = 1.0f;
    if (rudder < -1.0f) rudder = -1.0f;
    if (roll_dir > 1) roll_dir = 1;
    if (roll_dir < -1) roll_dir = -1;
    float dts = dt / 0.016f;
    if (dts < 0.0f) dts = 0.0f;
    if (dts > 4.0f) dts = 4.0f;

    /* the lever */
    if (throttle_up) a->throttle += AF_THROTTLE_RATE * dts / 60.0f;
    if (throttle_down) a->throttle -= AF_THROTTLE_RATE * dts / 60.0f;
    /* On the wheels, W/S is the throttle, the way it is in the rocket car. This is what makes a taxi a
     * taxi: hold W and it rolls off, release and the engine falls back to idle. In the air W/S is the
     * elevator again, so the stick keeps its meaning and nothing has to be re-learned on the runway. */
    if (a->grounded) {
        if (elevator > 0.05f)
            a->throttle += AF_GROUND_SPOOL_UP * fabsf(elevator) * dts / 60.0f;
        else if (elevator < -0.05f)
            a->throttle += AF_GROUND_SPOOL_DOWN * elevator * dts / 60.0f;   /* elevator is negative */
        else if (!throttle_up && !throttle_down)
            a->throttle -= AF_GROUND_IDLE_DECAY * dts / 60.0f;
    }
    if (a->throttle < 0.0f) a->throttle = 0.0f;
    if (a->throttle > 1000.0f) a->throttle = 1000.0f;

    float airspeed = sqrtf(a->vx * a->vx + a->vy * a->vy + a->vz * a->vz);
    int speed_milli = (int)(airspeed * 1000.0f);
    int stall_milli = (int)(af_stall(a->flaps) * 1000.0f);

    /* attitude.
     * ON THE WHEELS the gear carries the airframe, so the stick does not pitch or bank it: the nose
     * settles to level instead of being driven down every tick by the airborne stall rule. That drag
     * down was the taxi bug -- it rocked the plane onto its nose, the ground clamp shoved y back up,
     * and the airframe skipped instead of rolling, which reads as "jumping" and losing traction.
     * IN THE AIR, above the stall, the stick works; below it the wing is not flying and the nose falls --
     * that is the whole failure mode, and it is not recoverable by pulling harder. */
    int flying = speed_milli > stall_milli;
    int rolling_barrel = 0;
    if (a->grounded) {
        a->pitch *= 0.80f;
        a->roll *= 0.80f;
        a->pitch_rate = 0.0f;
        a->yaw_rate = 0.0f;
    } else if (flying) {
        /* W/S pitches the nose: a rate command, damped, that stops when the stick does. */
        a->pitch_rate += (elevator * AF_PITCH_RATE - a->pitch_rate) * AF_ATTITUDE_RESPONSE * dts;
        a->pitch += a->pitch_rate * dts;
        if (elevator > -0.05f && elevator < 0.05f)
            a->pitch += (0.0f - a->pitch) * AF_PITCH_TRIM * dts;   /* weathervane, no drift */
    } else {
        a->pitch_rate = 0.0f;
        a->pitch -= 1.8f * dts;
        a->roll *= 0.985f;
    }
    if (a->pitch > 32.0f) a->pitch = 32.0f;
    if (a->pitch < -32.0f) a->pitch = -32.0f;

    /* a barrel roll is a COMMANDED full turn, so it is not clamped the way a bank is: it rolls through
     * 360 and comes out level on the other side, which a +/-60 bank clamp could never express. */
    if (roll_dir != 0 && !a->grounded && flying) {
        a->roll += (float)roll_dir * 3.6f * dts;     /* degrees/tick: 360 in about 1.7 s */
        rolling_barrel = 1;
    }
    if (!rolling_barrel) {
        if (a->roll > 60.0f) a->roll = 60.0f;
        if (a->roll < -60.0f) a->roll = -60.0f;
    } else {
        if (a->roll > 180.0f) a->roll -= 360.0f;      /* normalised: a full turn ends at level again */
        if (a->roll < -180.0f) a->roll += 360.0f;
    }

    /* rudder -- the pedals. On the wheels this IS the nosewheel steering, which is what makes a taxi
     * a taxi; in the air it yaws the nose. Rate-commanded like pitch, so letting go stops the turn
     * instead of leaving it banked and circling. A barrel roll is a corkscrew and does not change
     * heading. */
    if (!rolling_barrel) {
        float want = rudder * (a->grounded ? 1.6f : AF_YAW_RATE);
        a->yaw_rate += (want - a->yaw_rate) * AF_ATTITUDE_RESPONSE * dts;
        a->yaw = norm_yaw_deg(a->yaw + a->yaw_rate * dts);
    } else {
        a->yaw_rate = 0.0f;
    }

    /* Bank follows the turn instead of being integrated separately (coordinated turn). The old code
     * added roll from the rudder AND turned from the bank, a loop that kept the airframe rocking. */
    if (!rolling_barrel && !a->grounded) {
        float bank_deg = a->yaw_rate / AF_YAW_RATE * AF_BANK_PER_YAW;
        a->roll += (bank_deg - a->roll) * 0.25f * dts;
        if (a->roll > 60.0f) a->roll = 60.0f;
        if (a->roll < -60.0f) a->roll = -60.0f;
    }
    /* The camera-heading tiller survives only as a self-align assist for a pilot who is NOT on the
     * pedals: with the rudder held it used to pull the nose back toward where the camera looked, which
     * cancelled the rudder almost exactly (1.5 in, 1.4 back) and is why the plane felt like it steered
     * itself. One steering authority at a time. */
    if (a->grounded && speed_milli < stall_milli * 2 && rudder > -0.001f && rudder < 0.001f) {
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

    /* Lateral grip: throw away the part of the velocity that is not along the nose, the way the rocket
     * car's tyres throw away its slide. Without it a turn left the aeroplane travelling sideways for
     * seconds -- the other half of "floppy". */
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

    /* lift versus weight: 1000 permille holds altitude, so anything above it climbs and a stall falls.
     * LEVEL flight sits at 880 permille, not 1000: at zero angle of attack a wing does not carry its own
     * weight at the liftoff speed, so the aeroplane has to be ROTATED (or run faster) to unstick. That is
     * what removes the old hop, where merely reaching 6.0 units/tick flicked it off the ground. */
    int aoa = (int)(880.0f + a->pitch * 18.0f);
    int lift = af_lift_permille(speed_milli, a->flaps, aoa);
    if (rolling_barrel) {
        /* Only the part of the lift along the world's up axis holds the aircraft up: wings vertical lift
         * sideways, inverted they lift downwards. 0.6 + 0.4*cos keeps a barrel roll roughly level on
         * average, so rolling does not secretly turn into a climb or a 100-unit drop. */
        float proj = 0.6f + 0.4f * cosf(a->roll * 0.0174532925f);
        lift = (int)((float)lift * proj);
    }
    float lift_accel = (float)lift * 0.001f * AF_GRAVITY;
    /* Cap the surplus. The rule's own v^2 curve reaches 1.8x weight at cruise, and integrating that
     * forever is what made the aeroplane balloon; 1.25x still climbs, it just does not accelerate away. */
    if (lift_accel > AF_GRAVITY * AF_MAX_LIFT_RATIO) lift_accel = AF_GRAVITY * AF_MAX_LIFT_RATIO;
    /* Vertical speed chases a target instead of integrating lift-minus-weight without limit. This is
     * the same thing the rocket car's hover controller does with its height error, and it is what makes
     * a climb settle at a rate rather than keep gaining speed. */
    float lift_ratio = lift_accel / AF_GRAVITY;
    float target_vy = (lift_ratio - 1.0f) * AF_CLIMB_RATE;
    /* While the gear is carrying the weight, weight does not get to pull the hull down: the tyres hold
     * it. Building vy here and zeroing it at the clamp below put a phantom -0.12 into the cached
     * airspeed every tick, which is exactly the kind of artefact that reads as the plane twitching. */
    int wheels_down = a->grounded && lift_accel <= AF_GRAVITY * 1.15f;
    if (!wheels_down)
        a->vy += (target_vy - a->vy) * AF_VSPEED_RESPONSE * dts;

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
    /* Ground contact, with hysteresis: while the wheels carry the weight the gear pins y and vy to the
     * surface, so lift that is merely CLOSE to weight cannot bounce the plane into the air one tick at
     * a time. It unsticks only on a real surplus (15%) -- that is a takeoff, not a hop. Rolling
     * resistance then keeps the tyres planted instead of letting the airframe skitter. */
    float ground = af_world_y(a->x, a->z, a->y + 1.0f);   /* only surfaces at or below the hull */
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
            a->pitch *= 0.85f;      /* sitting on the gear */
            a->roll *= 0.85f;
        } else {
            a->grounded = 0;
        }
    }
    if (a->grounded) {
        /* Brakes when the lever is closed, a LIGHT rolling resistance under power. 0.995 was the first
         * value tried and it is ~26% of speed per second at 60 Hz, which is a parachute: the takeoff
         * run plateaued at 6.0 and never reached liftoff. 0.9995 is ~3%/s, a tyre, not a brake. */
        float resist = (a->throttle < 1.0f) ? 0.97f : 0.9995f;
        a->vx *= resist;
        a->vz *= resist;
    }
}

#endif /* SHANKPIT_AIRPLANE_FLIGHT_H */
