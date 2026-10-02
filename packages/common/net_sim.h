#ifndef SHANKPIT_NET_SIM_H
#define SHANKPIT_NET_SIM_H

#include "protocol.h"
#include "physics.h"
#include "shared_movement.h"

#define SHANKPIT_NET_FIXED_DT 0.016f

typedef struct {
    float hover_lift;
    float ascend_accel;
    float descend_accel;
    float forward_accel;
    float strafe_accel;
    float drag;
    float vertical_damping;
    float max_hspeed;
    float max_vspeed_up;
    float max_vspeed_down;
    float yaw_rate;
    float pitch_visual_max;
    float roll_visual_max;
    float enter_radius;
    float exit_offset;
    float collider_radius;
    float collider_height;
} HelicopterTuning;

static const HelicopterTuning g_heli_tuning = {
    0.075f, 0.060f, 0.060f, 0.040f, 0.032f,
    0.08f, 0.16f, 0.95f, 0.42f, 0.58f, 95.0f,
    14.0f, 16.0f, 7.0f, 4.5f, 3.4f, 2.6f
};

static inline int heli_point_collides(float x, float y, float z) {
    for (int i = 1; i < map_count; i++) {
        Box b = map_geo[i];
        if (x > b.x - b.w * 0.5f && x < b.x + b.w * 0.5f &&
            y > b.y - b.h * 0.5f && y < b.y + b.h * 0.5f &&
            z > b.z - b.d * 0.5f && z < b.z + b.d * 0.5f) {
            return 1;
        }
    }
    return (y < 0.0f);
}

/* Programmable flight model (card #542). PARENA/stdlib/shankpit/heli_rules.prn, generated into
   packages/simulation/heli_rules.c and installed by heli_rules_install (heli_rules_host.h). With no rules
   installed (tests that only include this header) heli_simulate_step keeps the legacy arcade mover. */
typedef struct {
    int (*hover_collective_permille)(void);
    int (*collective_step_permille)(int cur, int up, int down);
    int (*lift_permille)(int collective);
    int (*cyclic_target_milli)(int input_permille);
    int (*attitude_step_milli)(int cur, int target);
    int (*yaw_target_milli)(int pedal_permille);
    int (*yaw_step_milli)(int cur, int target);
    int (*drag_permille)(int speed_milli);
    int (*vertical_damp_permille)(void);
    int (*max_hspeed_milli)(void);
    int (*max_vspeed_up_milli)(void);
    int (*max_vspeed_down_milli)(void);
} HeliRules;
static const HeliRules *g_heli_rules = NULL;
static inline void heli_set_rules(const HeliRules *r) { g_heli_rules = r; }

static inline int heli_permille_of(float v) {
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    return (int)(v * 1000.0f);
}

/* Rotorcraft step. Collective is a lever (h->collective, permille); cyclic tilts the airframe (att_pitch/
   att_roll lag the stick) and horizontal acceleration is the tilted thrust vector's horizontal part;
   pedals command a yaw rate that lags. Sign conventions: yaw + = turning LEFT (sim forward is
   (-sin yaw, -cos yaw)), so the pedal (+ = right, the D key) subtracts. */
static inline void heli_simulate_step_rules(HelicopterState *h, float dt) {
    const HeliRules *R = g_heli_rules;
    const HelicopterTuning *t = &g_heli_tuning;
    (void)dt;

    int occupied = h->occupant_player_id >= 0;
    int up = h->input.ascend ? 1 : 0, down = h->input.descend ? 1 : 0;
    int col = (int)h->collective;
    if (col <= 0 && !up && !down && !occupied) col = R->hover_collective_permille();
    col = R->collective_step_permille(col, up, down);
    h->collective = (float)col;

    /* A heli on the skids doesn't lean or turn on the spot; the airframe levels and the pedals go dead. */
    float stick_p = h->grounded ? 0.0f : h->input.forward;
    float stick_r = h->grounded ? 0.0f : h->input.strafe;
    float pedal   = h->grounded ? 0.0f : h->input.yaw;
    int tgt_p = R->cyclic_target_milli(heli_permille_of(stick_p));
    int tgt_r = R->cyclic_target_milli(heli_permille_of(stick_r));
    h->att_pitch = (float)R->attitude_step_milli((int)(h->att_pitch * 1000.0f), tgt_p) * 0.001f;
    h->att_roll  = (float)R->attitude_step_milli((int)(h->att_roll  * 1000.0f), tgt_r) * 0.001f;
    h->yaw_rate  = (float)R->yaw_step_milli((int)(h->yaw_rate * 1000.0f),
                                            R->yaw_target_milli(heli_permille_of(pedal))) * 0.001f;
    h->yaw = norm_yaw_deg(h->yaw - h->yaw_rate);

    float yaw_rad = -h->yaw * 0.0174533f;
    float fx = sinf(yaw_rad), fz = -cosf(yaw_rad);
    float rx = cosf(yaw_rad), rz = sinf(yaw_rad);
    float pr = h->att_pitch * 0.0174533f, rr = h->att_roll * 0.0174533f;

    /* thrust vector of the tilted rotor disc, in units of "gravity" (lift_permille/1000 g) */
    float thrust = (float)R->lift_permille(col) * 0.001f * GRAVITY_DROP;
    float ax = sinf(pr) * thrust;          /* forward component */
    float ar = sinf(rr) * thrust;          /* rightward component */
    float ay = thrust * cosf(pr) * cosf(rr);
    h->vx += fx * ax + rx * ar;
    h->vz += fz * ax + rz * ar;
    h->vy += ay - GRAVITY_DROP;
    h->vy *= 1.0f - (float)R->vertical_damp_permille() * 0.001f;

    float hs = sqrtf(h->vx * h->vx + h->vz * h->vz);
    float drag = 1.0f - (float)R->drag_permille((int)(hs * 1000.0f)) * 0.001f;
    h->vx *= drag; h->vz *= drag;
    hs = sqrtf(h->vx * h->vx + h->vz * h->vz);
    float vmax = (float)R->max_hspeed_milli() * 0.001f;
    if (hs > vmax && hs > 0.0001f) { float s = vmax / hs; h->vx *= s; h->vz *= s; }
    float vup = (float)R->max_vspeed_up_milli() * 0.001f, vdn = (float)R->max_vspeed_down_milli() * 0.001f;
    if (h->vy > vup) h->vy = vup;
    if (h->vy < -vdn) h->vy = -vdn;

    h->x += h->vx; h->y += h->vy; h->z += h->vz;

    h->grounded = 0;
    float floor_y = t->collider_height * 0.5f;
    if (h->y < floor_y) {
        if (h->vy < -0.45f) h->health -= 1;
        h->y = floor_y;
        if (h->vy < 0.0f) h->vy = 0.0f;
        h->grounded = 1;
    }
    float pts[8][3] = {
        {h->x + t->collider_radius, h->y, h->z}, {h->x - t->collider_radius, h->y, h->z},
        {h->x, h->y, h->z + t->collider_radius}, {h->x, h->y, h->z - t->collider_radius},
        {h->x, h->y + t->collider_height * 0.5f, h->z}, {h->x, h->y - t->collider_height * 0.5f, h->z},
        {h->x + t->collider_radius * 0.7f, h->y + t->collider_height * 0.25f, h->z + t->collider_radius * 0.7f},
        {h->x - t->collider_radius * 0.7f, h->y + t->collider_height * 0.25f, h->z - t->collider_radius * 0.7f},
    };
    for (int i = 0; i < 8; i++) {
        if (heli_point_collides(pts[i][0], pts[i][1], pts[i][2])) {
            h->x -= h->vx; h->z -= h->vz; h->vx *= -0.15f; h->vz *= -0.15f;
            if (heli_point_collides(h->x, h->y + t->collider_height * 0.4f, h->z)) {
                h->y -= h->vy;
                if (h->vy > 0.0f) h->vy = 0.0f;
            }
            break;
        }
    }

    /* what the clients draw: nose-down = negative pitch_visual, bank right = negative roll_visual */
    h->pitch_visual = -h->att_pitch;
    h->roll_visual = -h->att_roll - h->yaw_rate * 2.0f;   /* a little extra bank into a turn */
    float target_rotor = occupied ? 42.0f : 14.0f;
    h->rotor_speed += (target_rotor - h->rotor_speed) * 0.1f;
    h->rotor_angle = norm_yaw_deg(h->rotor_angle + h->rotor_speed);
}

static inline void heli_simulate_step(HelicopterState *h, float dt) {
    if (!h || !h->active) return;
    if (g_heli_rules) { heli_simulate_step_rules(h, dt); return; }
    const HelicopterTuning *t = &g_heli_tuning;

    h->yaw = norm_yaw_deg(h->yaw + h->input.yaw * t->yaw_rate * dt);
    float yaw_rad = -h->yaw * 0.0174533f;
    float fx = sinf(yaw_rad), fz = -cosf(yaw_rad);
    float rx = cosf(yaw_rad), rz = sinf(yaw_rad);

    h->vx += fx * (h->input.forward * t->forward_accel);
    h->vz += fz * (h->input.forward * t->forward_accel);
    h->vx += rx * (h->input.strafe * t->strafe_accel);
    h->vz += rz * (h->input.strafe * t->strafe_accel);
    h->vy += t->hover_lift - GRAVITY_DROP;
    if (h->input.ascend) h->vy += t->ascend_accel;
    if (h->input.descend) h->vy -= t->descend_accel;
    if (!h->input.ascend && !h->input.descend) h->vy *= (1.0f - t->vertical_damping);

    h->vx *= (1.0f - t->drag);
    h->vz *= (1.0f - t->drag);
    float hs = sqrtf(h->vx * h->vx + h->vz * h->vz);
    if (hs > t->max_hspeed && hs > 0.0001f) {
        float s = t->max_hspeed / hs;
        h->vx *= s; h->vz *= s;
    }
    if (h->vy > t->max_vspeed_up) h->vy = t->max_vspeed_up;
    if (h->vy < -t->max_vspeed_down) h->vy = -t->max_vspeed_down;

    h->x += h->vx;
    h->y += h->vy;
    h->z += h->vz;

    h->grounded = 0;
    float floor_y = g_heli_tuning.collider_height * 0.5f;
    if (h->y < floor_y) {
        if (h->vy < -0.45f) h->health -= 1;
        h->y = floor_y;
        if (h->vy < 0.0f) h->vy = 0.0f;
        h->grounded = 1;
    }

    float pts[8][3] = {
        {h->x + t->collider_radius, h->y, h->z}, {h->x - t->collider_radius, h->y, h->z},
        {h->x, h->y, h->z + t->collider_radius}, {h->x, h->y, h->z - t->collider_radius},
        {h->x, h->y + t->collider_height * 0.5f, h->z}, {h->x, h->y - t->collider_height * 0.5f, h->z},
        {h->x + t->collider_radius * 0.7f, h->y + t->collider_height * 0.25f, h->z + t->collider_radius * 0.7f},
        {h->x - t->collider_radius * 0.7f, h->y + t->collider_height * 0.25f, h->z - t->collider_radius * 0.7f},
    };
    for (int i = 0; i < 8; i++) {
        if (heli_point_collides(pts[i][0], pts[i][1], pts[i][2])) {
            h->x -= h->vx; h->z -= h->vz; h->vx *= -0.15f; h->vz *= -0.15f;
            if (heli_point_collides(h->x, h->y + t->collider_height * 0.4f, h->z)) {
                h->y -= h->vy;
                if (h->vy > 0.0f) h->vy = 0.0f;
            }
            break;
        }
    }

    h->pitch_visual = -h->input.forward * t->pitch_visual_max;
    h->roll_visual = (-h->input.strafe + h->input.yaw * 0.45f) * t->roll_visual_max;
    if (h->pitch_visual > t->pitch_visual_max) h->pitch_visual = t->pitch_visual_max;
    if (h->pitch_visual < -t->pitch_visual_max) h->pitch_visual = -t->pitch_visual_max;
    if (h->roll_visual > t->roll_visual_max) h->roll_visual = t->roll_visual_max;
    if (h->roll_visual < -t->roll_visual_max) h->roll_visual = -t->roll_visual_max;

    float target_rotor = h->occupant_player_id >= 0 ? 42.0f : 14.0f;
    h->rotor_speed += (target_rotor - h->rotor_speed) * 0.1f;
    h->rotor_angle = norm_yaw_deg(h->rotor_angle + h->rotor_speed);
}

void update_entity(PlayerState *p, float dt, void *server_context, unsigned int cmd_time);

static inline void shankpit_apply_usercmd_inputs(PlayerState *p, const UserCmd *cmd) {
    if (!p || !cmd) return;

    // Net movement contract:
    // 1) Raw command carries intent axes + control yaw/pitch.
    // 2) Axes are clamped/normalized once here before simulation.
    // 3) Client prediction/replay and server auth must both call this path.

    if (isfinite(cmd->yaw)) p->yaw = norm_yaw_deg(cmd->yaw);
    if (isfinite(cmd->pitch)) p->pitch = clamp_pitch_deg(cmd->pitch);

    p->in_fwd = cmd->fwd;
    p->in_strafe = cmd->str;

    float move_len = sqrtf(p->in_fwd * p->in_fwd + p->in_strafe * p->in_strafe);
    if (move_len > 1.0f) {
        p->in_fwd /= move_len;
        p->in_strafe /= move_len;
    }

    p->in_jump = (cmd->buttons & BTN_JUMP) != 0;
    p->in_shoot = (cmd->buttons & BTN_ATTACK) != 0;
    p->crouching = (cmd->buttons & BTN_CROUCH) != 0;
    p->in_reload = (cmd->buttons & BTN_RELOAD) != 0;
    p->in_use = (cmd->buttons & BTN_USE) != 0;
    p->in_ability = (cmd->buttons & BTN_ABILITY_1) != 0;
    p->in_bike = (cmd->buttons & BTN_VEHICLE_2) != 0;

    if (cmd->weapon_idx >= 0 && cmd->weapon_idx < MAX_WEAPONS) {
        p->current_weapon = cmd->weapon_idx;
    }
}

static inline void shankpit_simulate_movement_tick(PlayerState *p, unsigned int now_ms) {
    if (!p) return;
    if (p->in_vehicle && (p->vehicle_type == VEH_HELICOPTER || p->vehicle_type == VEH_BUGGY)) {
        p->vx = p->vy = p->vz = 0.0f;
        return;
    }

    // Net movement contract:
    // - Intent -> world-space wish conversion is shared (shankpit_move_wish_from_intent).
    // - Simulation order and fixed dt (SHANKPIT_NET_FIXED_DT) must stay identical for
    //   server authority and client prediction/replay.
    // - Reconciliation should only correct transport drift, not hide sim mismatches.

    if (!p->in_vehicle) {
        MoveIntent move_intent = {
            .forward = p->in_fwd,
            .strafe = p->in_strafe,
            .control_yaw_deg = p->yaw,
            .wants_jump = p->in_jump,
            .wants_sprint = 0
        };
        MoveWish move_wish = shankpit_move_wish_from_intent(move_intent);
        accelerate(p, move_wish.dir_x, move_wish.dir_z, move_wish.magnitude * MAX_SPEED, ACCEL);
    }

    float g = (p->in_jump ? GRAVITY_FLOAT : GRAVITY_DROP);
    p->vy -= g;
    if (p->in_jump && p->on_ground) {
        p->y += 0.1f;
        p->vy += JUMP_FORCE;
    }

    update_entity(p, SHANKPIT_NET_FIXED_DT, NULL, now_ms);
}

#endif
