/* airplane_input.h -- device -> AirplaneControls for the C-110 (FLIGHT-0/1, founder 2026-11-24).
 *
 * The flight model (airplane_flight.h, laws in PARENA airplane_rules.prn) only ever sees an
 * AirplaneControls: pitch/roll/rudder axes, a persistent throttle lever, brake, barrel roll, assist.
 * This header turns a keyboard or a gamepad into that struct. It is SDL-free (keys arrive as a
 * scancode-indexed byte array, the pad as a plain struct) so it is unit-testable headless.
 *
 * KEYBINDING HOOK. Every keyboard action has THREE bind slots (AIR_BINDS_PER_ACTION) -- the founder's
 * "up to 3 key binds per operation" -- held in an AirplaneBinds table. A slot of 0 is unbound. The lobby
 * fills the table with defaults (airplane_binds_set) and, when the general keybinding system lands, loads
 * it from the player's saved bindings; nothing in here needs to change. The gamepad + shaping options
 * live in AirplaneInputConfig (deadzone, response curve, invert-Y, key ramp), also meant to be exposed
 * through that system.
 *
 * Keyboard feel: keys are RAMPED onto the axes (they climb to full deflection over ~8 ticks and fall
 * back faster), so a tap is a gentle input and holding is a firm one, instead of an on/off switch. */
#ifndef SHANKPIT_AIRPLANE_INPUT_H
#define SHANKPIT_AIRPLANE_INPUT_H

#include <string.h>
#include <math.h>
#include "airplane_flight.h"

typedef enum {
    AIR_ACT_PITCH_UP = 0,   /* pull back: nose up */
    AIR_ACT_PITCH_DOWN,     /* push forward: nose down */
    AIR_ACT_ROLL_LEFT,
    AIR_ACT_ROLL_RIGHT,
    AIR_ACT_RUDDER_LEFT,
    AIR_ACT_RUDDER_RIGHT,
    AIR_ACT_THROTTLE_UP,
    AIR_ACT_THROTTLE_DOWN,
    AIR_ACT_BRAKE,
    AIR_ACT_FLAPS,          /* toggle (edge) */
    AIR_ACT_BARREL_LEFT,
    AIR_ACT_BARREL_RIGHT,
    AIR_ACT_ASSIST,         /* toggle assist <-> sim (edge) */
    AIR_ACT_COUNT
} AirplaneAction;

#define AIR_BINDS_PER_ACTION 3
typedef struct { int key[AIR_ACT_COUNT][AIR_BINDS_PER_ACTION]; } AirplaneBinds;

static inline void airplane_binds_clear(AirplaneBinds *b) { memset(b, 0, sizeof(*b)); }
/* bind slot 0..2 of an action to a scancode; returns 0 on a bad index. Rebinding is just calling this. */
static inline int airplane_binds_set(AirplaneBinds *b, AirplaneAction act, int slot, int scancode) {
    if (!b || act < 0 || act >= AIR_ACT_COUNT || slot < 0 || slot >= AIR_BINDS_PER_ACTION) return 0;
    b->key[act][slot] = scancode;
    return 1;
}
static inline int airplane_action_down(const AirplaneBinds *b, AirplaneAction act, const unsigned char *keys, int nkeys) {
    for (int i = 0; i < AIR_BINDS_PER_ACTION; i++) {
        int sc = b->key[act][i];
        if (sc > 0 && sc < nkeys && keys[sc]) return 1;
    }
    return 0;
}

typedef struct {
    float deadzone;          /* stick radius ignored (0..0.5) */
    float expo;              /* 0 = linear, 1 = cubic: gentle near centre, full at the edge */
    int invert_pitch;        /* 1 = push forward pulls the nose up (the "flight invert" option) */
    float key_ramp_up;       /* axis units per tick a held key adds */
    float key_ramp_down;     /* axis units per tick released keys give back */
    float trigger_threshold; /* trigger travel that counts as a throttle press */
    int assist_default;      /* 1 = start in assist */
} AirplaneInputConfig;

static inline void airplane_input_config_default(AirplaneInputConfig *c) {
    c->deadzone = 0.12f;
    c->expo = 0.35f;
    c->invert_pitch = 0;
    c->key_ramp_up = 0.12f;
    c->key_ramp_down = 0.22f;
    c->trigger_threshold = 0.30f;
    c->assist_default = 1;
}

/* radial-free per-axis shaping: dead zone, rescale so the edge of the dead zone is 0, then expo */
static inline float airplane_shape_axis(float v, const AirplaneInputConfig *cfg) {
    float dz = cfg->deadzone < 0.0f ? 0.0f : (cfg->deadzone > 0.5f ? 0.5f : cfg->deadzone);
    float a = fabsf(v);
    if (a <= dz) return 0.0f;
    a = (a - dz) / (1.0f - dz);
    if (a > 1.0f) a = 1.0f;
    float e = cfg->expo < 0.0f ? 0.0f : (cfg->expo > 1.0f ? 1.0f : cfg->expo);
    a = (1.0f - e) * a + e * a * a * a;
    return v < 0.0f ? -a : a;
}

static inline float airplane_ramp_axis(float cur, float target, const AirplaneInputConfig *cfg) {
    float step = (fabsf(target) > fabsf(cur) && (target * cur >= 0.0f)) ? cfg->key_ramp_up : cfg->key_ramp_down;
    if (cur < target) { cur += step; if (cur > target) cur = target; }
    else if (cur > target) { cur -= step; if (cur < target) cur = target; }
    return cur;
}

typedef struct {
    float pitch, roll, rudder;      /* ramped key axes */
    int flaps_prev, assist_prev;    /* edge detectors */
    int assist;                     /* current mode */
} AirplaneInputState;

static inline void airplane_input_state_init(AirplaneInputState *s, const AirplaneInputConfig *cfg) {
    memset(s, 0, sizeof(*s));
    s->assist = cfg->assist_default ? 1 : 0;
}

typedef struct {
    float lx, ly, rx, ry;   /* sticks, -1..1, SDL sign (up = negative Y) */
    float lt, rt;           /* triggers 0..1 */
    int dpad_up, dpad_down;
    int btn_a, btn_b;       /* lever up / lever down */
    int btn_lb, btn_rb;     /* flaps toggle / wheel brake */
    int btn_x;              /* assist <-> sim toggle */
    int connected;
} AirplanePad;

/* keyboard -> out. Writes the one-shot toggles into *flaps_edge / state->assist. */
static inline void airplane_keys_to_controls(AirplaneInputState *s, const AirplaneBinds *b,
                                             const unsigned char *keys, int nkeys,
                                             const AirplaneInputConfig *cfg,
                                             AirplaneControls *out, int *flaps_edge) {
    float tp = (float)airplane_action_down(b, AIR_ACT_PITCH_UP, keys, nkeys)
             - (float)airplane_action_down(b, AIR_ACT_PITCH_DOWN, keys, nkeys);
    float tr = (float)airplane_action_down(b, AIR_ACT_ROLL_RIGHT, keys, nkeys)
             - (float)airplane_action_down(b, AIR_ACT_ROLL_LEFT, keys, nkeys);
    float td = (float)airplane_action_down(b, AIR_ACT_RUDDER_RIGHT, keys, nkeys)
             - (float)airplane_action_down(b, AIR_ACT_RUDDER_LEFT, keys, nkeys);
    if (cfg->invert_pitch) tp = -tp;
    s->pitch = airplane_ramp_axis(s->pitch, tp, cfg);
    s->roll = airplane_ramp_axis(s->roll, tr, cfg);
    s->rudder = airplane_ramp_axis(s->rudder, td, cfg);
    airplane_controls_neutral(out);
    out->pitch = s->pitch;
    out->roll = s->roll;
    out->rudder = s->rudder;
    out->throttle_up = airplane_action_down(b, AIR_ACT_THROTTLE_UP, keys, nkeys);
    out->throttle_down = airplane_action_down(b, AIR_ACT_THROTTLE_DOWN, keys, nkeys);
    /* taxi: the same keys that pitch the nose also spool the engine on the wheels (push-forward = power) */
    out->ground_throttle = (float)airplane_action_down(b, AIR_ACT_PITCH_DOWN, keys, nkeys)
                         - (float)airplane_action_down(b, AIR_ACT_PITCH_UP, keys, nkeys);
    out->kb_ground_spool = 1;
    out->brake = airplane_action_down(b, AIR_ACT_BRAKE, keys, nkeys) ? 1.0f : 0.0f;
    out->barrel = airplane_action_down(b, AIR_ACT_BARREL_RIGHT, keys, nkeys) ? 1
                : (airplane_action_down(b, AIR_ACT_BARREL_LEFT, keys, nkeys) ? -1 : 0);
    int fl = airplane_action_down(b, AIR_ACT_FLAPS, keys, nkeys);
    if (flaps_edge) *flaps_edge = fl && !s->flaps_prev;
    s->flaps_prev = fl;
    int as = airplane_action_down(b, AIR_ACT_ASSIST, keys, nkeys);
    if (as && !s->assist_prev) s->assist = !s->assist;
    s->assist_prev = as;
    out->assist = s->assist;
}

/* gamepad -> out, MERGED over what the keyboard produced (the larger deflection wins per axis).
 * Left stick = pitch + roll (pull back = nose up unless invert_pitch), right stick X = rudder,
 * triggers / dpad / A,B = lever, LB flaps, RB brake, X assist toggle. */
static inline void airplane_pad_merge(AirplaneInputState *s, const AirplanePad *p,
                                      const AirplaneInputConfig *cfg, AirplaneControls *out,
                                      int *flaps_edge, int *pad_flaps_prev, int *pad_assist_prev) {
    if (!p || !p->connected) return;
    float pitch = airplane_shape_axis(p->ly, cfg);          /* stick down = pull back = nose up */
    if (cfg->invert_pitch) pitch = -pitch;
    float roll = airplane_shape_axis(p->lx, cfg);
    float rudder = airplane_shape_axis(p->rx, cfg);
    if (fabsf(pitch) > fabsf(out->pitch)) out->pitch = pitch;
    if (fabsf(roll) > fabsf(out->roll)) out->roll = roll;
    if (fabsf(rudder) > fabsf(out->rudder)) out->rudder = rudder;
    if (p->rt > cfg->trigger_threshold || p->dpad_up || p->btn_a) out->throttle_up = 1;
    if (p->lt > cfg->trigger_threshold || p->dpad_down || p->btn_b) out->throttle_down = 1;
    if (p->btn_rb) out->brake = 1.0f;
    int fl = p->btn_lb;
    if (flaps_edge && fl && !*pad_flaps_prev) *flaps_edge = 1;
    *pad_flaps_prev = fl;
    if (p->btn_x && !*pad_assist_prev) { s->assist = !s->assist; out->assist = s->assist; }
    *pad_assist_prev = p->btn_x;
}

#endif /* SHANKPIT_AIRPLANE_INPUT_H */
