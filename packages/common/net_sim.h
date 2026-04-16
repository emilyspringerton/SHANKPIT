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
    0.018f, 0.026f, 0.030f, 0.040f, 0.032f,
    0.08f, 0.16f, 0.95f, 0.42f, 0.58f, 95.0f,
    14.0f, 16.0f, 7.0f, 4.5f, 3.4f, 2.6f
};

static inline float shankpit_buggy_clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline void shankpit_buggy_sync_state(PlayerState *p) {
    if (!p) return;
    p->yaw = norm_yaw_deg(p->buggy_body_yaw);
}

static inline void shankpit_buggy_simulate(PlayerState *p) {
    if (!p) return;

    float planar_speed = sqrtf(p->vx * p->vx + p->vz * p->vz);
    float speed_ratio = shankpit_buggy_clampf(planar_speed / BUGGY_MAX_SPEED, 0.0f, 1.0f);
    float steer_input = shankpit_buggy_clampf(p->in_strafe, -1.0f, 1.0f);

    if (fabsf(steer_input) < 0.01f) steer_input = 0.0f;
    if (planar_speed < BUGGY_MIN_STEER_SPEED) steer_input *= (planar_speed / BUGGY_MIN_STEER_SPEED);

    p->buggy_steer += (steer_input - p->buggy_steer) * 0.22f;

    float target_yaw_rate = p->buggy_steer * BUGGY_STEER_STRENGTH;
    float yaw_rate_cap = BUGGY_MAX_YAW_RATE_LOW + (BUGGY_MAX_YAW_RATE_HIGH - BUGGY_MAX_YAW_RATE_LOW) * speed_ratio;
    if (target_yaw_rate > yaw_rate_cap) target_yaw_rate = yaw_rate_cap;
    if (target_yaw_rate < -yaw_rate_cap) target_yaw_rate = -yaw_rate_cap;

    p->buggy_yaw_rate += (target_yaw_rate - p->buggy_yaw_rate) * BUGGY_YAW_RATE_RESPONSE;
    p->buggy_yaw_rate *= (1.0f - BUGGY_YAW_DAMPING);
    p->buggy_body_yaw = norm_yaw_deg(p->buggy_body_yaw + p->buggy_yaw_rate);

    float yaw_rad = -p->buggy_body_yaw * 0.0174533f;
    float fx = sinf(yaw_rad), fz = -cosf(yaw_rad);
    float rx = cosf(yaw_rad), rz = sinf(yaw_rad);
    float forward_speed = p->vx * fx + p->vz * fz;
    float lateral_speed = p->vx * rx + p->vz * rz;

    float motor_axis = shankpit_buggy_clampf(p->in_fwd, -1.0f, 1.0f);
    float accel = BUGGY_ACCEL * BUGGY_MAX_SPEED;
    forward_speed += motor_axis * accel;

    float lateral_grip = BUGGY_GRIP_LOW_SPEED + (BUGGY_GRIP_HIGH_SPEED - BUGGY_GRIP_LOW_SPEED) * speed_ratio;
    float drift_commit = shankpit_buggy_clampf(fabsf(p->buggy_steer) * speed_ratio, 0.0f, 1.0f);
    float grip_scale = 1.0f - drift_commit * BUGGY_DRIFT_MULTIPLIER;
    if (grip_scale < 0.12f) grip_scale = 0.12f;
    lateral_speed *= (1.0f - lateral_grip * grip_scale);

    float induced_slip = p->buggy_steer * planar_speed * (0.08f + 0.16f * speed_ratio);
    lateral_speed += induced_slip;

    forward_speed *= (1.0f - BUGGY_LONGITUDINAL_DRAG);
    lateral_speed *= (1.0f - BUGGY_LATERAL_DRAG);

    if (forward_speed > BUGGY_MAX_SPEED) forward_speed = BUGGY_MAX_SPEED;
    if (forward_speed < -BUGGY_MAX_SPEED * 0.68f) forward_speed = -BUGGY_MAX_SPEED * 0.68f;

    p->vx = fx * forward_speed + rx * lateral_speed;
    p->vz = fz * forward_speed + rz * lateral_speed;

    float travel_yaw = p->buggy_body_yaw;
    if (planar_speed > 0.01f) {
        travel_yaw = norm_yaw_deg(atan2f(p->vx, -p->vz) * 57.29578f);
    }
    float realign_scale = (1.0f - fabsf(p->buggy_steer)) * shankpit_buggy_clampf(planar_speed / (BUGGY_MAX_SPEED * 0.75f), 0.0f, 1.0f);
    float yaw_err = angle_diff(travel_yaw, p->buggy_body_yaw);
    p->buggy_body_yaw = norm_yaw_deg(p->buggy_body_yaw + yaw_err * BUGGY_AUTO_REALIGN * realign_scale);

    p->buggy_forward_speed = forward_speed;
    p->buggy_lateral_speed = lateral_speed;
    p->buggy_slip_angle = atan2f(lateral_speed, fabsf(forward_speed) + 0.02f) * 57.29578f;
    p->buggy_visual_roll = shankpit_buggy_clampf(-(p->buggy_steer * 0.7f + p->buggy_slip_angle * 0.018f), -1.0f, 1.0f) * BUGGY_BODY_ROLL_MAX;
    p->buggy_visual_pitch = shankpit_buggy_clampf(-(motor_axis * 0.55f + (forward_speed - planar_speed) * 0.35f), -1.0f, 1.0f) * BUGGY_BODY_PITCH_MAX;
    shankpit_buggy_sync_state(p);
}

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

static inline void heli_simulate_step(HelicopterState *h, float dt) {
    if (!h || !h->active) return;
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
    if (p->in_vehicle && p->vehicle_type != VEH_HELICOPTER) {
        p->vehicle_type = VEH_BUGGY;
    }
    if (p->in_vehicle && p->vehicle_type == VEH_HELICOPTER) {
        p->vx = p->vy = p->vz = 0.0f;
        return;
    }
    if (!p->in_vehicle || p->vehicle_type != VEH_BUGGY) {
        phys_init_buggy_state(p);
    }

    // Net movement contract:
    // - Intent -> world-space wish conversion is shared (shankpit_move_wish_from_intent).
    // - Simulation order and fixed dt (SHANKPIT_NET_FIXED_DT) must stay identical for
    //   server authority and client prediction/replay.
    // - Reconciliation should only correct transport drift, not hide sim mismatches.

    if (p->in_vehicle && p->vehicle_type == VEH_BUGGY) {
        shankpit_buggy_simulate(p);
    } else {
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
    }

    float g = p->in_vehicle ? BUGGY_GRAVITY : (p->in_jump ? GRAVITY_FLOAT : GRAVITY_DROP);
    p->vy -= g;
    if (p->in_jump && p->on_ground) {
        p->y += 0.1f;
        p->vy += JUMP_FORCE;
    }

    update_entity(p, SHANKPIT_NET_FIXED_DT, NULL, now_ms);
}

#endif
