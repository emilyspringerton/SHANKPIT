/* widget_circuit.h -- composable world widgets: a tiny power circuit, an F interface, and panels.
 *
 * Founder: "we already have a widget primative ... general interface like F to interact and then after
 * that it needs to be able to go into custom programmed affordances ... the widgets need to be
 * composible ... think light switches, basic circuits, an elevator is a widget of widgets ... bring in
 * edu vm script it all needs to play nice with that interpreter".
 *
 * MODEL: every placed widget is one NODE in a directed circuit, evaluated once per tick in authoring
 * order (pins point at lower indices). Power is a single integer 0..15 -- the same tier range
 * DEADWEIGHT's own cables use -- so a whole level is a byte per node and any node can drive any other:
 *
 *   LEVER (6)       a light switch: press it, it stays. 0 or 15, no wiring needed.
 *   PLATE (5)       a pressure plate: the HOST writes it from bodies standing on it.
 *   DELAY (7)       a relay: output follows input N ticks late (both edges).
 *   TRANSISTOR (8)  a real AND: passes its input only while its gate input is powered.
 *   MACHINE (1..4)  a motor/actuator: follows its input. A lift is a motor plus a lever; a centrifuge
 *                   is a motor plus a plate; a security door is a plate -> transistor -> machine.
 *
 * EDUVM: every node can own a register. Its live value is mirrored into `reg_value` every tick, and a
 * terminal panel writes `reg_value`, so a script (set_reg / read_sensor) and the physical circuit are
 * looking at the same number instead of two copies of the world. This header does not include or call
 * the VM: it produces the values; the host that owns the EduWorldState copies them in.
 *
 * THE F INTERFACE: focus is a proximity query with hysteresis (the prompt holds for
 * widget-focus-prompt-ms, so it does not flicker at the edge of the radius). USE on a lever toggles it
 * and stays in the world; USE on anything with a register opens its PANEL, where a script-driven
 * affordance takes over the keys (that is the "custom programmed affordance" the founder asked for).
 *
 * NOT DONE, and named: nothing here draws. There is no prompt quad, no panel rendering and no lever
 * mesh -- those are renderer work, and this file deliberately contains no GL. Nothing calls
 * widget_circuit_tick from the game loop yet either: it is loaded and driven by the test, and wiring it
 * to a scene is the next step (see docs/WIDGET_AFFORDANCES.md).
 */
#ifndef SHANKPIT_WIDGET_CIRCUIT_H
#define SHANKPIT_WIDGET_CIRCUIT_H

#include <math.h>
#include <string.h>

#include "../world/level_boxes.h"
#include "widget_rules.h"

#define WIDGET_CIRCUIT_MAX LEVEL_BOXES_MAX_EDU_WIDGETS

/* Kinds, matching the level schema's machine_type for the circuit-only ones. */
enum {
    WIDGET_KIND_LEVER = 0,
    WIDGET_KIND_PLATE = 1,
    WIDGET_KIND_DELAY = 2,
    WIDGET_KIND_TRANSISTOR = 3,
    WIDGET_KIND_MOTOR = 4,
    WIDGET_KIND_NONE = -1
};

enum { WIDGET_MODE_WORLD = 0, WIDGET_MODE_PANEL = 1 };

/* Panel keys, so this file never includes SDL. */
enum { WIDGET_KEY_UP = 0, WIDGET_KEY_DOWN, WIDGET_KEY_LEFT, WIDGET_KEY_RIGHT, WIDGET_KEY_USE, WIDGET_KEY_BACK };

#define WIDGET_PANEL_STEP 500
#define WIDGET_PANEL_MIN 0
#define WIDGET_PANEL_MAX 8000

typedef struct {
    int active;
    char id[32];
    int kind;
    int input_pin, gate_pin;   /* widget indices, or -1 */
    int power;                 /* live output 0..15 */
    int armed;                 /* latched: lever position, plate reading, delay's held value */
    int timer_ticks;           /* delay countdown */
    int delay_ticks;           /* this delay's own length; 0 = the rules' default */
    int reg;                   /* EduVM register to mirror into; -1 = none */
    int reg_value;             /* what the host copies into that register */
    float x, y, z, radius;
    int box_index;
} WidgetNode;

typedef struct {
    WidgetNode n[WIDGET_CIRCUIT_MAX];
    int count;
    int mode;              /* WIDGET_MODE_* */
    int focused;           /* index, or -1 */
    int focus_hold_ms;     /* prompt hysteresis */
    int pressed;           /* index whose panel is open, or -1 */
} WidgetCircuit;

/* Which circuit kind an authored machine_type is. 1..4 are the education machines and behave as
 * motors here (a gate raises, a crate moves, a portal opens: all "this node drives geometry"); 5..8 are
 * the circuit kinds themselves. Everything else is scenery. */
static inline int widget_circuit_kind_for_machine(int machine_type) {
    if (machine_type == 5) return WIDGET_KIND_PLATE;
    if (machine_type == 6) return WIDGET_KIND_LEVER;
    if (machine_type == 7) return WIDGET_KIND_DELAY;
    if (machine_type == 8) return WIDGET_KIND_TRANSISTOR;
    if (machine_type >= 1 && machine_type <= 4) return WIDGET_KIND_MOTOR;
    return WIDGET_KIND_NONE;
}

static inline void widget_circuit_init(WidgetCircuit *c) {
    if (!c) return;
    memset(c, 0, sizeof *c);
    c->focused = -1;
    c->pressed = -1;
    c->mode = WIDGET_MODE_WORLD;
}

/* Loads every authored widget. A widget with an unknown kind or an empty id is skipped rather than
 * half-loaded, and a pin that points at itself or out of range is dropped to -1 -- a mis-wired level
 * must degrade to "that switch does nothing", never to a feedback loop or a crash. */
static inline int widget_circuit_load(WidgetCircuit *c, const CustomLevelData *lvl) {
    int loaded = 0;
    widget_circuit_init(c);
    if (!c || !lvl) return 0;
    for (int i = 0; i < lvl->edu_widget_count && c->count < WIDGET_CIRCUIT_MAX; i++) {
        const LevelEduWidget *w = &lvl->edu_widgets[i];
        int kind = widget_circuit_kind_for_machine(w->machine_type);
        if (kind == WIDGET_KIND_NONE || !w->entity_id[0]) continue;
        WidgetNode *n = &c->n[c->count];
        memset(n, 0, sizeof *n);
        n->active = 1;
        snprintf(n->id, sizeof n->id, "%s", w->entity_id);
        n->kind = kind;
        n->armed = w->initial_state ? wg_on() : wg_off();
        n->reg = w->memory_address;
        n->reg_value = n->armed;
        n->delay_ticks = w->delay_ticks > 0 ? w->delay_ticks : 0;
        n->x = w->x; n->y = w->y; n->z = w->z;
        n->radius = w->radius > 0.0f ? w->radius : 3.0f;   /* authored reach, or a room-scale default */
        n->box_index = w->box_index;
        n->input_pin = (w->input_pin >= 0 && w->input_pin < c->count) ? w->input_pin : -1;
        n->gate_pin = (w->gate_pin >= 0 && w->gate_pin < c->count) ? w->gate_pin : -1;
        c->count++;
        loaded++;
    }
    return loaded;
}

static inline int widget_circuit_pin_power(const WidgetCircuit *c, int pin) {
    if (!c || pin < 0 || pin >= c->count) return 0;
    return c->n[pin].power;
}

/* The whole circuit, one tick. Sources first (lever/plate/delay resolve their own latched value), then
 * the pass-through nodes reading the values already computed this tick. */
static inline void widget_circuit_tick(WidgetCircuit *c) {
    if (!c) return;
    for (int i = 0; i < c->count; i++) {
        WidgetNode *n = &c->n[i];
        int src = widget_circuit_pin_power(c, n->input_pin);
        int gate = widget_circuit_pin_power(c, n->gate_pin);
        if (n->kind == WIDGET_KIND_DELAY) {
            int delay = n->delay_ticks > 0 ? n->delay_ticks : g_widget_rules.delay_default_ticks();
            if (delay < g_widget_rules.delay_min_ticks()) delay = g_widget_rules.delay_min_ticks();
            if (delay > g_widget_rules.delay_max_ticks()) delay = g_widget_rules.delay_max_ticks();
            if (g_widget_rules.delay_armed(src, n->armed, n->timer_ticks)) {
                n->timer_ticks = delay;
            } else if (n->timer_ticks > 0) {
                n->timer_ticks--;
                if (n->timer_ticks == 0) n->armed = src;   /* the relay closes on the new value */
            }
        }
        n->power = g_widget_rules.output_power(n->kind, n->armed, src, gate);
        /* A mirror, not a second copy: the register reads what the circuit actually did this tick. */
        n->reg_value = n->power;
    }
}

/* The host writes a plate from the bodies standing on it (a script can never forge a sensor). */
static inline void widget_circuit_set_plate(WidgetCircuit *c, int index, int occupants) {
    if (!c || index < 0 || index >= c->count) return;
    if (c->n[index].kind != WIDGET_KIND_PLATE) return;
    c->n[index].armed = g_widget_rules.plate_power(occupants);
}

static inline int widget_circuit_lever_toggle(WidgetCircuit *c, int index) {
    if (!c || index < 0 || index >= c->count) return 0;
    if (c->n[index].kind != WIDGET_KIND_LEVER) return 0;
    c->n[index].armed = g_widget_rules.lever_toggle(c->n[index].armed);
    return c->n[index].armed;
}

/* ---- the F interface ------------------------------------------------------------------------------ */
/* widget_circuit_focus -- the nearest in-reach widget, or -1. `busy` (a panel already open, a vehicle
 * seat, a cutscene) suppresses the prompt entirely. The hold timer keeps the LAST focus while nothing
 * qualifies, so the prompt does not strobe as the player walks along a shelf of levers. */
static inline int widget_circuit_focus(WidgetCircuit *c, float px, float py, float pz, int busy, int dt_ms) {
    int best = -1;
    int best_mm = 0;
    if (!c) return -1;
    for (int i = 0; i < c->count; i++) {
        WidgetNode *n = &c->n[i];
        float dx, dy, dz;
        int dist_mm, radius_mm;
        if (!n->active) continue;
        dx = px - n->x; dy = py - n->y; dz = pz - n->z;
        dist_mm = (int)(sqrtf(dx * dx + dy * dy + dz * dz) * 1000.0f);
        radius_mm = (int)(n->radius * 1000.0f);
        if (!g_widget_rules.focus_ok(dist_mm, radius_mm, busy)) continue;
        if (best < 0 || dist_mm < best_mm) { best = i; best_mm = dist_mm; }
    }
    if (best >= 0) {
        c->focused = best;
        c->focus_hold_ms = 250;   /* rules' widget-focus-prompt-ms */
    } else {
        if (c->focus_hold_ms > 0) c->focus_hold_ms -= (dt_ms > 0 ? dt_ms : 0);
        if (c->focus_hold_ms <= 0) c->focused = -1;
    }
    return c->focused;
}

/* USE (F). A lever toggles and stays in the world; anything with a register opens its panel, which is
 * where a custom, script-driven affordance takes over (a terminal, a lift call panel, a centrifuge). */
static inline int widget_circuit_use(WidgetCircuit *c) {
    int idx;
    if (!c || c->mode != WIDGET_MODE_WORLD) return c->mode;
    idx = c->focused;
    if (idx < 0 || idx >= c->count) return WIDGET_MODE_WORLD;
    if (c->n[idx].kind == WIDGET_KIND_LEVER) {
        widget_circuit_lever_toggle(c, idx);
        return WIDGET_MODE_WORLD;
    }
    if (c->n[idx].reg >= 0) {
        c->mode = WIDGET_MODE_PANEL;
        c->pressed = idx;
        return WIDGET_MODE_PANEL;
    }
    return WIDGET_MODE_WORLD;
}

/* Panel keys. Returns 1 if the panel consumed the key; BACK always closes and returns 1. A panel only
 * dials a value where a value makes sense (a machine setpoint); on a sensor it just displays. */
static inline int widget_circuit_panel_key(WidgetCircuit *c, int key, int panel_max) {
    WidgetNode *n;
    if (!c || c->mode != WIDGET_MODE_PANEL || c->pressed < 0 || c->pressed >= c->count) return 0;
    n = &c->n[c->pressed];
    if (key == WIDGET_KEY_BACK) {
        c->mode = WIDGET_MODE_WORLD;
        c->pressed = -1;
        return 1;
    }
    if (panel_max <= WIDGET_PANEL_MIN) panel_max = WIDGET_PANEL_MAX;
    if (key == WIDGET_KEY_UP) {
        n->reg_value = g_widget_rules.panel_key_adjust(n->reg_value, WIDGET_PANEL_STEP, WIDGET_PANEL_MIN, panel_max);
        return 1;
    }
    if (key == WIDGET_KEY_DOWN) {
        n->reg_value = g_widget_rules.panel_key_adjust(n->reg_value, -WIDGET_PANEL_STEP, WIDGET_PANEL_MIN, panel_max);
        return 1;
    }
    if (key == WIDGET_KEY_USE || key == WIDGET_KEY_LEFT || key == WIDGET_KEY_RIGHT) return 1; /* consumed */
    return 0;
}

/* The value a host copies into EduVM register `reg`. Returns -1 when no node owns that register, so a
 * caller can tell "nothing here" from "this machine is at zero". */
static inline int widget_circuit_reg_value(const WidgetCircuit *c, int reg) {
    if (!c || reg < 0) return -1;
    for (int i = 0; i < c->count; i++)
        if (c->n[i].active && c->n[i].reg == reg) return c->n[i].reg_value;
    return -1;
}

#endif /* SHANKPIT_WIDGET_CIRCUIT_H */
