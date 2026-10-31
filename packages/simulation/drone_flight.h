/* drone_flight.h -- the two QUADCOPTERS (founder 2026-10-31: "next we want to add more aircraft can
 * we add a basic drone and a mini fly drone?").
 *
 * A drone sits beside the helicopter as a genuinely different airframe, not a reskin: the heli's
 * collective lever and airframe tilt take real ticks to turn into motion (net_sim.h's flight model),
 * while a quadcopter tilts its rotors and GOES -- translational thrust is applied straight to velocity
 * with the airframe's own response rate. That is the whole reason to have one: it can stop and back
 * into a window the heli would overshoot.
 *
 *   DRONE_KIND_RECON  a stable medium chassis: 2.5 kg, slow, steady in weather, stops when it hits a
 *                     wall -- a camera, and the one you fly indoors
 *   DRONE_KIND_FLY    a 45 g micro: hyper-responsive, faster, twice as buffeted by a storm, and it
 *                     BOUNCES off the world instead of stopping dead
 *
 * Every number is a PARENA mod (PARENA/stdlib/shankpit/drone_rules.prn, generated into
 * packages/simulation/drone_rules.c, installed by drone_rules_host.h) -- same convention as the buggy
 * and the rocket car: with nothing installed the built-in defaults below are used, so a test that
 * just includes this header still works.
 *
 * Weather reaches the airframes through drone_set_storm(), which a host feeds from the sky it is
 * already simulating; both kinds react, by their own PARENA rule. Not on the wire: this is the flight
 * model, and the host owns the entity's transform (same split heli_simulate_step uses). */
#ifndef SHANKPIT_DRONE_FLIGHT_H
#define SHANKPIT_DRONE_FLIGHT_H

#include "protocol.h"
#include "net_sim.h"   /* heli_point_collides -- the same world query the helicopter collides with */

#define DRONE_KIND_RECON 0
#define DRONE_KIND_FLY   1

typedef struct {
    int (*mass_grams)(int kind);
    int (*response_permille)(int kind);
    int (*top_speed_milli)(int kind);
    int (*climb_milli)(int kind);
    int (*yaw_rate_milli)(int kind);
    int (*drift_permille)(int kind, int storm_permille);
    int (*bounce_permille)(int kind);
    int (*hull)(int kind);
    int (*shard_kills)(int kind, int mass_grams, int rel_speed_milli);
    int (*gimbal_yaw_milli)(int kind);
    int (*gimbal_pitch_milli)(int kind);
} DroneRules;
static const DroneRules *g_drone_rules = NULL;
static inline void drone_set_rules(const DroneRules *r) { g_drone_rules = r; }

/* Built-in defaults, deliberately equal to the PARENA mod's own values. */
#define DRONE_RECON_RESPONSE 0.650f
#define DRONE_FLY_RESPONSE  1.850f
#define DRONE_RECON_TOP 1.8f
#define DRONE_FLY_TOP   3.4f
#define DRONE_RECON_CLIMB 0.9f
#define DRONE_FLY_CLIMB   1.4f
#define DRONE_RECON_YAW 90.0f
#define DRONE_FLY_YAW   220.0f
#define DRONE_FLY_BOUNCE 0.35f
#define DRONE_RECON_HULL 60
#define DRONE_FLY_HULL   12

/* The storm the host is in, permille 0..1000. Static to this header's translation unit, so the lobby
 * (which draws the weather) and the server (which does not) each simply never set it. */
static int g_drone_storm_permille = 0;
static inline void drone_set_storm(int permille) {
    if (permille < 0) permille = 0;
    if (permille > 1000) permille = 1000;
    g_drone_storm_permille = permille;
}
static inline int drone_storm_permille(void) { return g_drone_storm_permille; }

static inline float df_response(int kind) {
    if (g_drone_rules && g_drone_rules->response_permille) {
        float v = (float)g_drone_rules->response_permille(kind) * 0.001f;
        if (v > 0.01f) return v;
    }
    return kind == DRONE_KIND_FLY ? DRONE_FLY_RESPONSE : DRONE_RECON_RESPONSE;
}
static inline float df_top_speed(int kind) {
    if (g_drone_rules && g_drone_rules->top_speed_milli) {
        float v = (float)g_drone_rules->top_speed_milli(kind) * 0.001f;
        if (v > 0.05f) return v;
    }
    return kind == DRONE_KIND_FLY ? DRONE_FLY_TOP : DRONE_RECON_TOP;
}
static inline float df_climb_speed(int kind) {
    if (g_drone_rules && g_drone_rules->climb_milli) {
        float v = (float)g_drone_rules->climb_milli(kind) * 0.001f;
        if (v > 0.05f) return v;
    }
    return kind == DRONE_KIND_FLY ? DRONE_FLY_CLIMB : DRONE_RECON_CLIMB;
}
static inline float df_yaw_rate(int kind) {
    if (g_drone_rules && g_drone_rules->yaw_rate_milli) {
        float v = (float)g_drone_rules->yaw_rate_milli(kind) * 0.001f;
        if (v > 0.05f) return v;
    }
    return kind == DRONE_KIND_FLY ? DRONE_FLY_YAW : DRONE_RECON_YAW;
}
static inline int df_drift_permille(int kind, int storm) {
    if (g_drone_rules && g_drone_rules->drift_permille) return g_drone_rules->drift_permille(kind, storm);
    if (storm < 0) storm = 0;
    if (storm > 1000) storm = 1000;
    return kind == DRONE_KIND_FLY ? storm * 2 : storm / 4;
}
static inline float df_bounce(int kind) {
    if (g_drone_rules && g_drone_rules->bounce_permille) {
        float v = (float)g_drone_rules->bounce_permille(kind) * 0.001f;
        if (v > 0.0f) return v;
        return 0.0f;
    }
    return kind == DRONE_KIND_FLY ? DRONE_FLY_BOUNCE : 0.0f;
}
static inline int df_hull(int kind) {
    if (g_drone_rules && g_drone_rules->hull) return g_drone_rules->hull(kind);
    return kind == DRONE_KIND_FLY ? DRONE_FLY_HULL : DRONE_RECON_HULL;
}

/* ---- shards --------------------------------------------------------------------------------------
 * Founder: "wire the Mini Fly Drone to explode instantly upon any impact with a rigid shard". The
 * airframe does not decide that -- PARENA does (drone-shard-kills): any shard kills the micro, while
 * the recon's frame needs mass x relative speed to clear a real threshold, so ordinary dust and
 * pebbles bounce off it. The sweep that feeds this lives where the debris does (the host), because the
 * XPBD/rigid bodies are not this header's business. */
static inline int df_shard_kills(int kind, int mass_grams, int rel_speed_milli) {
    if (g_drone_rules && g_drone_rules->shard_kills)
        return g_drone_rules->shard_kills(kind, mass_grams, rel_speed_milli);
    if (mass_grams <= 0) return 0;
    if (kind == DRONE_KIND_FLY) return 1;
    if (rel_speed_milli < 0) rel_speed_milli = 0;
    return (mass_grams * rel_speed_milli) > 40000000;
}
static inline int df_gimbal_yaw_limit(int kind) {
    if (g_drone_rules && g_drone_rules->gimbal_yaw_milli) {   /* milli-degrees */
        int v = g_drone_rules->gimbal_yaw_milli(kind);
        if (v > 0) return v;
    }
    return kind == DRONE_KIND_FLY ? 20000 : 90000;
}
static inline int df_gimbal_pitch_limit(int kind) {
    if (g_drone_rules && g_drone_rules->gimbal_pitch_milli) {
        int v = g_drone_rules->gimbal_pitch_milli(kind);
        if (v > 0) return v;
    }
    return kind == DRONE_KIND_FLY ? 25000 : 60000;
}

/* how big the airframe is to a shard: a 1.2 m quad, a 0.35 m micro */
static inline float df_hit_radius(int kind) {
    return kind == DRONE_KIND_FLY ? 0.35f : 1.2f;
}

/* drone_kill -- retire it. Deliberately not "health -= n": both airframes are one-hit bodies, and the
 * pop is the host's job (it owns the FX). */
static inline void drone_kill(DroneState *d) {
    if (!d || !d->active) return;
    d->destroyed = 1;
    d->active = 0;
    d->health = 0.0f;
    d->vx = d->vy = d->vz = 0.0f;
}

/* drone_eject_pilot -- the pilot is thrown clear where the drone was and falls; the airframe is gone. */
static inline void drone_eject_pilot(PlayerState *p, const DroneState *d) {
    if (!p || !d) return;
    p->x = d->x; p->y = d->y; p->z = d->z;
    p->in_vehicle = 0;
    p->vehicle_type = VEH_NONE;
    p->on_ground = 0;
}

/* drone_shard_contact -- one rigid body against the airframe. The shard is a point (its centre) and the
 * airframe is a sphere of df_hit_radius, which is the honest broad phase for bodies this small. Returns
 * 1 when this contact destroyed the drone. */
static inline int drone_shard_contact(DroneState *d, float sx, float sy, float sz,
                                      float svx, float svy, float svz, int mass_grams) {
    if (!d || !d->active || d->destroyed) return 0;
    if (mass_grams <= 0) return 0;
    float dx = sx - d->x, dy = sy - d->y, dz = sz - d->z;
    float r = df_hit_radius(d->kind);
    if ((dx * dx + dy * dy + dz * dz) > r * r) return 0;
    float rvx = d->vx - svx, rvy = d->vy - svy, rvz = d->vz - svz;
    float rel = sqrtf(rvx * rvx + rvy * rvy + rvz * rvz);
    if (!df_shard_kills(d->kind, mass_grams, (int)(rel * 1000.0f))) return 0;
    drone_kill(d);
    return 1;
}

/* drone_gimbal_track -- where the camera wants to look, expressed as a GIMBAL OFFSET from the airframe
 * and clamped to that airframe's own mechanical limits. This is what makes the recon a camera: its head
 * swings 90 degrees while the quad keeps flying its own heading. The micro's head barely moves. */
static inline void drone_gimbal_track(DroneState *d, float cam_yaw, float cam_pitch) {
    if (!d) return;
    float rel = norm_yaw_deg(cam_yaw - d->yaw);
    if (rel > 180.0f) rel -= 360.0f;
    if (rel < -180.0f) rel += 360.0f;
    float yl = (float)df_gimbal_yaw_limit(d->kind) * 0.001f;
    float pl = (float)df_gimbal_pitch_limit(d->kind) * 0.001f;
    if (rel > yl) rel = yl;
    if (rel < -yl) rel = -yl;
    if (cam_pitch > pl) cam_pitch = pl;
    if (cam_pitch < -pl) cam_pitch = -pl;
    d->gimbal_yaw = rel;
    d->gimbal_pitch = cam_pitch;
}

/* the yaw the camera actually looks along: the airframe plus its gimbal */
static inline float drone_gimbal_view_yaw(const DroneState *d) {
    return norm_yaw_deg(d->yaw + d->gimbal_yaw);
}

/* drone_spawn_defaults -- one parked/hovering drone. y is caller-supplied because where "the air
 * over this spot" is depends on what it was called onto (vehicle_call.h decides). */
static inline void drone_spawn_defaults(DroneState *d, int id, int scene_id, int kind,
                                       float x, float y, float z, float yaw) {
    memset(d, 0, sizeof(*d));
    d->active = 1;
    d->id = id;
    d->scene_id = scene_id;
    d->kind = (kind == DRONE_KIND_FLY) ? DRONE_KIND_FLY : DRONE_KIND_RECON;
    d->x = x; d->y = y; d->z = z;
    d->yaw = yaw;
    d->health = (float)df_hull(d->kind);
    d->occupant_player_id = -1;
}

/* Takes the world explicitly rather than reading local_state: this header is included by local_game.h
 * ABOVE where that global is defined, so a global reference here would not compile. */
static inline DroneState *drone_find_nearby(ServerState *st, int scene_id, float x, float y, float z, float radius) {
    float rr = radius * radius;
    if (!st) return NULL;
    for (int i = 0; i < MAX_DRONES; i++) {
        DroneState *d = &st->drones[i];
        if (!d->active || d->scene_id != scene_id) continue;
        float dx = d->x - x, dy = d->y - y, dz = d->z - z;
        if ((dx * dx + dy * dy + dz * dz) <= rr) return d;
    }
    return NULL;
}

static inline int drone_try_enter(PlayerState *p, DroneState *d) {
    if (!p || !d || !d->active || d->occupant_player_id >= 0) return 0;
    d->occupant_player_id = p->id;
    p->in_vehicle = 1;
    p->vehicle_type = VEH_DRONE;
    p->x = d->x; p->y = d->y; p->z = d->z;
    p->vx = p->vy = p->vz = 0.0f;
    p->on_ground = 0;
    return 1;
}

/* drone_try_exit -- put the pilot down beside the drone (it stays hovering where they left it, which
 * is the useful property of a camera you can park). */
static inline int drone_try_exit(PlayerState *p, DroneState *d) {
    if (!p || !d || d->occupant_player_id != p->id) return 0;
    float r = -d->yaw * 0.0174532925f;
    float offs[3][2] = { { cosf(r) * 1.6f, sinf(r) * 1.6f },
                         { -cosf(r) * 1.6f, -sinf(r) * 1.6f },
                         { 0.0f, 0.0f } };
    for (int i = 0; i < 3; i++) {
        float px = d->x + offs[i][0];
        float pz = d->z + offs[i][1];
        float py = terrain_sample_height(&g_scene_terrain, px, pz);
        if (py < 0.0f) py = 0.0f;
        if (!heli_point_collides(px, py + 1.0f, pz)) {
            p->x = px; p->y = py; p->z = pz;
            break;
        }
    }
    p->in_vehicle = 0;
    p->vehicle_type = VEH_NONE;
    p->on_ground = 1;
    d->occupant_player_id = -1;
    return 1;
}

/* drone_simulate_step -- one tick of a quadcopter.
 *   fwd/strafe   translational intent, -1..1, applied IMMEDIATELY through the airframe's response
 *                rate (this is the difference from the helicopter: no lever, no lean-then-move)
 *   ascend       +1 climb, -1 descend, 0 hold
 *   turn         yaw intent, -1..1, scaled by the airframe's own yaw rate
 *   storm        weather severity 0..1000; PARENA decides what each airframe does with it
 * An empty hand hovers: with no vertical intent the vertical velocity damps to nothing. */
static inline void drone_simulate_step(DroneState *d, float fwd, float strafe, float ascend,
                                       float turn, int storm, float dt) {
    if (!d || !d->active) return;
    if (fwd > 1.0f) fwd = 1.0f;
    if (fwd < -1.0f) fwd = -1.0f;
    if (strafe > 1.0f) strafe = 1.0f;
    if (strafe < -1.0f) strafe = -1.0f;
    if (ascend > 1.0f) ascend = 1.0f;
    if (ascend < -1.0f) ascend = -1.0f;
    if (turn > 1.0f) turn = 1.0f;
    if (turn < -1.0f) turn = -1.0f;

    float dt_scale = dt / 0.016f;
    if (dt_scale < 0.0f) dt_scale = 0.0f;
    if (dt_scale > 4.0f) dt_scale = 4.0f;   /* a stalled frame does not get to teleport it */

    const int kind = d->kind;
    const float resp = df_response(kind);
    const float top = df_top_speed(kind);
    const float climb = df_climb_speed(kind);

    d->yaw = norm_yaw_deg(d->yaw + turn * df_yaw_rate(kind) * dt_scale);
    float r = -d->yaw * 0.0174532925f;
    float fx = sinf(r), fz = -cosf(r);
    float rx = cosf(r), rz = sinf(r);

    float wish_x = fx * fwd + rx * strafe;
    float wish_z = fz * fwd + rz * strafe;
    float wl = sqrtf(wish_x * wish_x + wish_z * wish_z);
    if (wl > 1.0f) { wish_x /= wl; wish_z /= wl; }

    float k = resp * dt_scale;
    if (k > 1.0f) k = 1.0f;
    d->vx += (wish_x * top - d->vx) * k;
    d->vz += (wish_z * top - d->vz) * k;

    float kv = resp * 0.85f * dt_scale;
    if (kv > 1.0f) kv = 1.0f;
    d->vy += (ascend * climb - d->vy) * kv;

    /* the weather: buffeting scaled by the airframe's own drift rule */
    int drift = df_drift_permille(kind, storm);
    if (drift != 0) {
        d->flight_ticks++;
        float t = (float)d->flight_ticks * 0.10f;
        float buffet = (float)drift * 0.000004f;
        d->vx += sinf(t) * buffet;
        d->vz += cosf(t * 1.3f) * buffet;
    } else {
        d->flight_ticks++;
    }

    /* attitude follows what it is doing: nose down into its own motion, banked into a strafe. This is
     * the read that makes a drone look like a drone instead of a floating brick. */
    float pf = (top > 0.01f) ? (d->vx * fx + d->vz * fz) / top : 0.0f;
    float rf = (top > 0.01f) ? (d->vx * rx + d->vz * rz) / top : 0.0f;
    float ka = 0.25f * dt_scale;
    if (ka > 1.0f) ka = 1.0f;
    d->pitch += (-pf * 22.0f - d->pitch) * ka;
    d->roll += (rf * 18.0f - d->roll) * ka;

    d->rotor_angle += 55.0f * dt_scale;
    if (d->rotor_angle > 360.0f) d->rotor_angle -= 360.0f;

    float nx = d->x + d->vx * dt_scale;
    float ny = d->y + d->vy * dt_scale;
    float nz = d->z + d->vz * dt_scale;
    if (heli_point_collides(nx, ny, nz)) {
        float b = df_bounce(kind);
        if (b > 0.0f) {
            /* the micro bounces: reverse a fraction of the speed it hit with */
            d->vx = -d->vx * b;
            d->vz = -d->vz * b;
            if (d->vy < 0.0f) d->vy = -d->vy * b;
        } else {
            /* the recon stops dead -- it is a camera, and a camera does not ricochet */
            d->vx *= 0.15f;
            d->vz *= 0.15f;
            d->vy *= 0.15f;
        }
    } else {
        d->x = nx;
        d->y = ny;
        d->z = nz;
    }
    if (d->y < 0.05f) {
        d->y = 0.05f;
        if (d->vy < 0.0f) d->vy = 0.0f;
    }
}

#endif /* SHANKPIT_DRONE_FLIGHT_H */
