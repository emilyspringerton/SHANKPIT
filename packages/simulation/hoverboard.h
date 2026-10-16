/* hoverboard.h -- wedge-shaped hover skateboards ("air ships"). EMILY/BACKLOG.md SECTION 536
 * follow-up, founder real-time (2026-09-24): "add air ships like wedge shaped hover skateboards
 * they have different physics per board and they can be programmed in parena friction and gravity
 * etc can be tuned".
 *
 * Real, own design -- a new BIG_O mechanic, not a reverse-port. Three board types
 * (BOARD_SPEEDSTER/TANK/GLIDER), each a real, genuinely different feel: SPEEDSTER is fast and
 * slippery with a floaty jump, TANK snaps off the line but tops out slow and hugs the ground,
 * GLIDER is the real "air ship" of the three -- gentle accel, gravity scaled way down for long
 * airtime over gaps. The four tuned constants per board (`day/packages/simulation/
 * hoverboard_rules.c`, generated from `PARENA/stdlib/big_o/hoverboard_rules.prn`) are pure PARENA
 * lookups; momentum integration itself is real, stateful host math (velocity carries across
 * ticks), same "mods first everything" split every other BIG_O PARENA consumer header draws.
 *
 * Pure C99, no GL/SDL/network -- same "state in, mutate, read back" contract party.h/
 * chat_say.h already establish. The host (day/apps/server/src/main.c) owns per-player mount
 * state and calls hoverboard_tick() once per movement tick while mounted, in place of (not
 * alongside) the normal direct-input on-foot horizontal movement.
 */
#ifndef HOVERBOARD_H
#define HOVERBOARD_H

#include <math.h>

#define BOARD_NONE 0 /* not mounted -- the host's own sentinel, never passed into the PARENA lookups */

/* Real, generated PARENA decisions -- day/packages/simulation/hoverboard_rules.c, built from
   PARENA/stdlib/big_o/hoverboard_rules.prn. Declared here rather than #included as a header because
   the generated file is a plain .c translation unit (same convention party_rules.c/chat_rules.c
   already use), linked in by scripts/build_day.sh. */
int board_count(void);
int board_speedster(void);
int board_tank(void);
int board_glider(void);
int board_is_valid(int board_type);
int board_accel_x100(int board_type);
int board_friction_x1000(int board_type);
int board_max_speed_x100(int board_type);
int board_gravity_x1000(int board_type);

static const char *const BOARD_NAMES[4] = { "NONE", "SPEEDSTER", "TANK", "GLIDER" }; /* index by board_type, 0..board_count() */

/* hoverboard_gravity_scale -- board_gravity_x1000(board_type) / 1000.0f, as a real
 * multiplier the host applies directly to PC_GRAVITY while this player is mounted. board_type
 * BOARD_NONE (or any invalid type) is a real, honest 1.0 (unscaled) -- callers only invoke
 * this while actually mounted, but a defensive caller gets normal gravity back, never a crash or
 * a divide/multiply by an undefined board's own zero-filled constants. */
static inline float hoverboard_gravity_scale(int board_type) {
    if (!board_is_valid(board_type)) return 1.0f;
    return (float)board_gravity_x1000(board_type) / 1000.0f;
}

/* hoverboard_tick -- one tick of real momentum physics for a mounted board: accelerate
 * toward (input_x, input_z) (a real, already-clamped-to-[-1,1] local input pair, same shape the
 * host's own on-foot movement already reads), apply drag, clamp to the board's own real top
 * speed. The two output pointers carry real momentum across calls -- the host owns storing them
 * per player (same "host owns the state, this function only mutates it" contract every other
 * bigo_*.h consumer header already follows). A call with an invalid board_type is a real, honest
 * no-op (velocity untouched) rather than silently applying zeroed-out constants.
 *
 * board-friction-x1000 is a real per-SECOND retention fraction (900 = keep 90% of speed after one
 * full real second coasting), applied via powf(friction, dt) rather than a flat per-tick multiply
 * -- frame-rate-independent, and keeps the tuned constants meaningful regardless of PC_TICK_HZ.
 * Every board's own real natural coasting speed (accel / -ln(friction), the point where
 * acceleration and drag balance) comfortably exceeds its own max-speed cap, so max_speed is what
 * actually governs top speed in practice -- friction instead shapes how quickly a board loses
 * (or holds onto) speed once the player lets off input, which is its own real, distinct feel. */
static inline void hoverboard_tick(int board_type, float input_x, float input_z, float dt, float *out_vx, float *out_vz) {
    if (!board_is_valid(board_type)) return;
    float accel = (float)board_accel_x100(board_type) / 100.0f;
    float friction = (float)board_friction_x1000(board_type) / 1000.0f;
    float max_speed = (float)board_max_speed_x100(board_type) / 100.0f;

    float vx = *out_vx + input_x * accel * dt;
    float vz = *out_vz + input_z * accel * dt;
    float decay = powf(friction, dt);
    vx *= decay;
    vz *= decay;

    float speed = sqrtf(vx * vx + vz * vz);
    if (speed > max_speed && speed > 0.0f) {
        float scale = max_speed / speed;
        vx *= scale;
        vz *= scale;
    }
    *out_vx = vx;
    *out_vz = vz;
}

#endif /* HOVERBOARD_H */
