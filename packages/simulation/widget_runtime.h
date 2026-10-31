/* widget_runtime.h -- the host half of a widget circuit: what makes one reachable in a level.
 *
 * widget_circuit.h decides (a lever latches, a relay is late, a transistor is an AND); this header does
 * the four things a RUNNING level needs and a unit test cannot do for it:
 *
 *   1. PLATES ARE FED FROM BODIES. Once per tick the host hands over the actors standing in the world and
 *      every plate node counts the ones inside its own authored radius (with a vertical window, so a body
 *      six units above a plate is not standing on it). A script can never forge a sensor; a player can.
 *   2. MACHINES MOVE REAL GEOMETRY. A node with an authored `box_index` drives that box through the
 *      callback, edge-triggered -- only when its power actually crosses, so an idle level does no work.
 *   3. REGISTERS ARE MIRRORED ON CHANGE. `read_sensor(reg)` and the physical circuit read one number.
 *   4. THE F INTERFACE AND ITS PANEL are forwarded, so the caller never touches WidgetCircuit directly.
 *
 * Deliberately free of GL, SDL, physics and the education package: the caller passes two function
 * pointers. That is what lets packages/simulation/widget_runtime_test.c drive a lever -> delay -> lift and
 * a plate -> transistor -> door headlessly, and lets apps/lobby pass the real
 * phys_set_custom_level_box_y / eduvm_host_widget_set.
 */
#ifndef SHANKPIT_WIDGET_RUNTIME_H
#define SHANKPIT_WIDGET_RUNTIME_H

#include <math.h>
#include <string.h>

#include "widget_circuit.h"

/* Anything that can stand on a plate. Kept to three floats so the host can fill a stack array from
 * PlayerState without this package depending on protocol.h. */
typedef struct {
    float x, y, z;
} WidgetActor;

/* value is the node's live register: circuit power 0..15, or a panel-dialled setpoint. */
typedef void (*WidgetRuntimeRegSink)(void *user, const char *id, int reg, int value);
/* open is 1 when the machine is powered. Called only when the box's state changes. */
typedef void (*WidgetRuntimeBoxSink)(void *user, int box_index, int open);

typedef struct {
    WidgetCircuit c;
    int box[WIDGET_CIRCUIT_MAX];       /* authored box per node, -1 = register only */
    int box_open[WIDGET_CIRCUIT_MAX];  /* last pushed state, -1 = never pushed */
    int reg_seen[WIDGET_CIRCUIT_MAX];  /* last mirrored value, -1 = never mirrored */
} WidgetRuntime;

static inline const char *widget_runtime_kind_name(int kind) {
    switch (kind) {
        case WIDGET_KIND_LEVER: return "LEVER";
        case WIDGET_KIND_PLATE: return "PLATE";
        case WIDGET_KIND_DELAY: return "RELAY";
        case WIDGET_KIND_TRANSISTOR: return "GATE";
        case WIDGET_KIND_MOTOR: return "MACHINE";
        default: return "SCENERY";
    }
}

static inline void widget_runtime_init(WidgetRuntime *rt) {
    if (!rt) return;
    memset(rt, 0, sizeof *rt);
    widget_circuit_init(&rt->c);
    for (int i = 0; i < WIDGET_CIRCUIT_MAX; i++) {
        rt->box[i] = -1;
        rt->box_open[i] = -1;
        rt->reg_seen[i] = -1;
    }
}

static inline int widget_runtime_find(const WidgetRuntime *rt, const char *id) {
    if (!rt || !id || !id[0]) return -1;
    for (int i = 0; i < rt->c.count; i++)
        if (rt->c.n[i].active && strcmp(rt->c.n[i].id, id) == 0) return i;
    return -1;
}

/* Loads a level's nodes and re-attaches each one's authored box BY ID. The circuit drops malformed
 * widgets, so a positional copy of box_index would silently shift every box after the dropped one. */
static inline int widget_runtime_load(WidgetRuntime *rt, const CustomLevelData *lvl) {
    int loaded;
    if (!rt) return 0;
    widget_runtime_init(rt);
    if (!lvl) return 0;
    loaded = widget_circuit_load(&rt->c, lvl);
    for (int i = 0; i < lvl->edu_widget_count; i++) {
        int idx = widget_runtime_find(rt, lvl->edu_widgets[i].entity_id);
        if (idx >= 0) rt->box[idx] = lvl->edu_widgets[i].box_index;
    }
    return loaded;
}

/* Does the circuit own this box? A host that also has a legacy per-widget door path uses this to stay
 * out of the way: two systems writing one box is how a lift ends up half-open. */
static inline int widget_runtime_owns_box(const WidgetRuntime *rt, int box_index) {
    if (!rt || box_index < 0) return 0;
    for (int i = 0; i < rt->c.count; i++)
        if (rt->box[i] == box_index) return 1;
    return 0;
}

/* One tick: plates from the actors, then the circuit, then the two sinks on change. Returns how many
 * machines moved, so a caller can tell a quiet level from a working one without reading the sinks. */
static inline int widget_runtime_tick(WidgetRuntime *rt, const WidgetActor *actors, int count,
                                      WidgetRuntimeRegSink reg_sink, WidgetRuntimeBoxSink box_sink,
                                      void *user) {
    int moved = 0;
    if (!rt || rt->c.count <= 0) return 0;
    if (count < 0 || !actors) count = 0;
    for (int i = 0; i < rt->c.count; i++) {
        WidgetNode *n = &rt->c.n[i];
        int occupants = 0;
        if (n->kind != WIDGET_KIND_PLATE) continue;
        for (int a = 0; a < count; a++) {
            float dx = actors[a].x - n->x;
            float dz = actors[a].z - n->z;
            float dy = actors[a].y - n->y;
            if (dx * dx + dz * dz <= n->radius * n->radius && dy > -2.0f && dy < 3.0f) occupants++;
        }
        widget_circuit_set_plate(&rt->c, i, occupants);
    }
    widget_circuit_tick(&rt->c);
    for (int i = 0; i < rt->c.count; i++) {
        WidgetNode *n = &rt->c.n[i];
        if (n->reg >= 0 && n->reg_value != rt->reg_seen[i]) {
            rt->reg_seen[i] = n->reg_value;
            if (reg_sink) reg_sink(user, n->id, n->reg, n->reg_value);
        }
        if (rt->box[i] >= 0) {
            int open = n->power > 0 ? 1 : 0;
            if (open != rt->box_open[i]) {
                rt->box_open[i] = open;
                if (box_sink) box_sink(user, rt->box[i], open);
                moved++;
            }
        }
    }
    return moved;
}

static inline int widget_runtime_focus(WidgetRuntime *rt, float x, float y, float z, int busy, int dt_ms) {
    if (!rt) return -1;
    return widget_circuit_focus(&rt->c, x, y, z, busy, dt_ms);
}

/* F. Returns 1 when it acted (a lever toggled in place, or a panel opened), 0 when there was nothing to
 * use -- the caller consumes its USE press only on 1, so a widget never eats the key from a vehicle. */
static inline int widget_runtime_use(WidgetRuntime *rt) {
    int idx, acted = 0;
    if (!rt || rt->c.mode != WIDGET_MODE_WORLD) return 0;
    idx = rt->c.focused;
    if (idx < 0 || idx >= rt->c.count) return 0;
    if (rt->c.n[idx].kind == WIDGET_KIND_LEVER) acted = 1;
    else if (rt->c.n[idx].reg >= 0) acted = 1;
    if (!acted) return 0;
    widget_circuit_use(&rt->c);
    return 1;
}

static inline int widget_runtime_panel_open(const WidgetRuntime *rt) {
    return (rt && rt->c.mode == WIDGET_MODE_PANEL) ? 1 : 0;
}

static inline const WidgetNode *widget_runtime_panel_node(const WidgetRuntime *rt) {
    if (!rt || rt->c.mode != WIDGET_MODE_PANEL) return NULL;
    if (rt->c.pressed < 0 || rt->c.pressed >= rt->c.count) return NULL;
    return &rt->c.n[rt->c.pressed];
}

static inline int widget_runtime_panel_key(WidgetRuntime *rt, int key) {
    if (!rt) return 0;
    return widget_circuit_panel_key(&rt->c, key, WIDGET_PANEL_MAX);
}

#endif /* SHANKPIT_WIDGET_RUNTIME_H */
