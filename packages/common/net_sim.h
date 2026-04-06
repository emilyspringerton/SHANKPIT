#ifndef SHANKPIT_NET_SIM_H
#define SHANKPIT_NET_SIM_H

#include "protocol.h"
#include "physics.h"
#include "shared_movement.h"

#define SHANKPIT_NET_FIXED_DT 0.016f

#define HELI_HOVER_LIFT 0.0165f
#define HELI_ASCEND_ACCEL 0.036f
#define HELI_DESCEND_ACCEL 0.040f
#define HELI_FORWARD_ACCEL 0.030f
#define HELI_STRAFE_ACCEL 0.024f
#define HELI_DRAG 0.050f
#define HELI_VERTICAL_DAMPING 0.090f
#define HELI_MAX_HSPEED 0.95f
#define HELI_MAX_VSPEED_UP 0.52f
#define HELI_MAX_VSPEED_DOWN 0.65f
#define HELI_YAW_RATE 2.8f
#define HELI_PITCH_VISUAL_MAX 16.0f
#define HELI_ROLL_VISUAL_MAX 18.0f
#define HELI_ENTER_RADIUS 7.0f
#define HELI_EXIT_OFFSET 4.5f
#define HELI_COLLIDER_RADIUS 2.2f

void update_entity(PlayerState *p, float dt, void *server_context, unsigned int cmd_time);

static inline void shankpit_heli_simulate(HelicopterState *h, float dt) {
    if (!h || !h->active) return;
    YawBasis basis = shankpit_yaw_basis_from_deg(h->yaw);
    h->input.forward = shankpit_clampf(h->input.forward, -1.0f, 1.0f);
    h->input.strafe = shankpit_clampf(h->input.strafe, -1.0f, 1.0f);
    h->input.yaw = shankpit_clampf(h->input.yaw, -1.0f, 1.0f);

    h->yaw = norm_yaw_deg(h->yaw + h->input.yaw * HELI_YAW_RATE);

    float ax = basis.fwd_x * h->input.forward * HELI_FORWARD_ACCEL + basis.right_x * h->input.strafe * HELI_STRAFE_ACCEL;
    float az = basis.fwd_z * h->input.forward * HELI_FORWARD_ACCEL + basis.right_z * h->input.strafe * HELI_STRAFE_ACCEL;
    h->vx += ax;
    h->vz += az;
    h->vx *= (1.0f - HELI_DRAG);
    h->vz *= (1.0f - HELI_DRAG);

    float up = HELI_HOVER_LIFT - GRAVITY_DROP;
    if (h->input.ascend) up += HELI_ASCEND_ACCEL;
    if (h->input.descend) up -= HELI_DESCEND_ACCEL;
    h->vy += up;
    if (!h->input.ascend && !h->input.descend) {
        h->vy *= (1.0f - HELI_VERTICAL_DAMPING);
    }

    float hs = sqrtf(h->vx * h->vx + h->vz * h->vz);
    if (hs > HELI_MAX_HSPEED && hs > 0.0f) {
        float s = HELI_MAX_HSPEED / hs;
        h->vx *= s; h->vz *= s;
    }
    if (h->vy > HELI_MAX_VSPEED_UP) h->vy = HELI_MAX_VSPEED_UP;
    if (h->vy < -HELI_MAX_VSPEED_DOWN) h->vy = -HELI_MAX_VSPEED_DOWN;

    float next_x = h->x + h->vx;
    float next_y = h->y + h->vy;
    float next_z = h->z + h->vz;
    float hit_x = 0.0f, hit_y = 0.0f, hit_z = 0.0f, nx = 0.0f, ny = 0.0f, nz = 0.0f;
    if (trace_map(h->x, h->y + 1.0f, h->z, next_x, next_y + 1.0f, next_z, &hit_x, &hit_y, &hit_z, &nx, &ny, &nz)) {
        h->x = hit_x;
        h->y = hit_y - 1.0f;
        h->z = hit_z;
        if (fabsf(nx) > 0.1f) h->vx = 0.0f;
        if (fabsf(nz) > 0.1f) h->vz = 0.0f;
        if (ny > 0.5f) {
            h->grounded = 1;
            if (h->vy < -0.20f) h->health -= 2;
            h->vy = 0.0f;
        } else if (ny < -0.2f) {
            h->vy = 0.0f;
        }
    } else {
        h->x = next_x;
        h->y = next_y;
        h->z = next_z;
        h->grounded = 0;
    }
    if (h->y < 0.2f) {
        h->y = 0.2f;
        h->grounded = 1;
        if (h->vy < -0.2f) h->health -= 2;
        h->vy = 0.0f;
    }

    h->rotor_speed += ((h->occupant_player_id >= 0 ? 32.0f : 10.0f) - h->rotor_speed) * 0.12f;
    h->rotor_angle = fmodf(h->rotor_angle + h->rotor_speed * dt * 20.0f, 360.0f);
    h->pitch_visual = shankpit_clampf(-h->input.forward * HELI_PITCH_VISUAL_MAX, -HELI_PITCH_VISUAL_MAX, HELI_PITCH_VISUAL_MAX);
    h->roll_visual = shankpit_clampf(-h->input.strafe * HELI_ROLL_VISUAL_MAX - h->input.yaw * 8.0f, -HELI_ROLL_VISUAL_MAX, HELI_ROLL_VISUAL_MAX);
    if (h->health < 0) h->health = 0;
}

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

    if (cmd->weapon_idx >= 0 && cmd->weapon_idx < MAX_WEAPONS) {
        p->current_weapon = cmd->weapon_idx;
    }
}

static inline void shankpit_simulate_movement_tick(PlayerState *p, unsigned int now_ms) {
    if (!p) return;
    if (p->in_vehicle && p->vehicle_type == VEH_HELICOPTER) {
        return;
    }

    // Net movement contract:
    // - Intent -> world-space wish conversion is shared (shankpit_move_wish_from_intent).
    // - Simulation order and fixed dt (SHANKPIT_NET_FIXED_DT) must stay identical for
    //   server authority and client prediction/replay.
    // - Reconciliation should only correct transport drift, not hide sim mismatches.

    MoveIntent move_intent = {
        .forward = p->in_fwd,
        .strafe = p->in_vehicle ? 0.0f : p->in_strafe,
        .control_yaw_deg = p->yaw,
        .wants_jump = p->in_jump,
        .wants_sprint = 0
    };
    MoveWish move_wish = shankpit_move_wish_from_intent(move_intent);

    float max_spd = p->in_vehicle ? BUGGY_MAX_SPEED : MAX_SPEED;
    float acc = p->in_vehicle ? BUGGY_ACCEL : ACCEL;
    float wish_speed = move_wish.magnitude * max_spd;
    accelerate(p, move_wish.dir_x, move_wish.dir_z, wish_speed, acc);

    float g = p->in_vehicle ? BUGGY_GRAVITY : (p->in_jump ? GRAVITY_FLOAT : GRAVITY_DROP);
    p->vy -= g;
    if (p->in_jump && p->on_ground) {
        p->y += 0.1f;
        p->vy += JUMP_FORCE;
    }

    update_entity(p, SHANKPIT_NET_FIXED_DT, NULL, now_ms);
}

#endif
