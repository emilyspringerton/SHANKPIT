/* keybind_host.c -- see keybind_host.h. Decisions live in PARENA (keybind_rules.c). */
#include "keybind_host.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int g_kb[KB_MAX_OPS][KB_SLOTS + 1];

static const char *k_op_labels[KB_MAX_OPS] = {
    "",
    "Move Forward", "Move Backward", "Strafe Left", "Strafe Right", "Jump", "Crouch", "Boost / Sprint",
    "Reload", "Ability", "Throw Pheromone",
    "Interact / Use / Enter-Exit Vehicle", "Helicopter: Bank Left", "Helicopter: Bank Right",
    "Aircraft: Throttle Up", "Aircraft: Throttle Down", "Aircraft: Flaps (reserved)", "Aircraft: Wheel Brake (reserved)",
    "Toggle Camera (1st/3rd Person)", "Cycle Overlay Mode",
    "Action Button 1", "Action Button 2", "Action Button 3", "Action Button 4", "Action Button 5",
    "Action Button 6", "Action Button 7", "Action Button 8", "Action Button 9", "Action Button 10",
    "Action Bar: Shift Page", "Action Bar: Ctrl Page", "Action Bar: Alt Page",
};
static const char *k_cat_labels[7] = { "", "Movement", "Combat", "Action Bar", "Vehicles", "Camera", "Interface" };

const char *kb_host_op_label(int op) { return (op >= 1 && op < KB_MAX_OPS) ? k_op_labels[op] : ""; }
const char *kb_host_category_label(int cat) { return (cat >= 1 && cat <= 6) ? k_cat_labels[cat] : ""; }

void kb_host_init(void) {
    int op, s;
    memset(g_kb, 0, sizeof g_kb);
    for (op = 1; op < KB_MAX_OPS; op++)
        for (s = 1; s <= KB_SLOTS; s++) g_kb[op][s] = kb_default(op, s);
}

int kb_host_get(int op, int slot) {
    if (!kb_op_valid(op) || slot < 1 || slot > KB_SLOTS) return 0;
    return g_kb[op][slot];
}

void kb_host_unbind(int op, int slot) {
    if (kb_op_valid(op) && slot >= 1 && slot <= KB_SLOTS) g_kb[op][slot] = 0;
}

void kb_host_reset_op(int op) {
    int s;
    if (!kb_op_valid(op)) return;
    for (s = 1; s <= KB_SLOTS; s++) g_kb[op][s] = kb_default(op, s);
}

KbBindResult kb_host_bind(int op, int slot, int key) {
    KbBindResult r = {0, 0, 0, 0};
    int o, s;
    if (!kb_op_valid(op) || slot < 1 || slot > KB_SLOTS || !kb_bindable(key)) return r;
    for (o = 1; o < KB_MAX_OPS; o++) {
        for (s = 1; s <= KB_SLOTS; s++) {
            if (o == op && s == slot) continue;
            if (o == op) { if (g_kb[o][s] == key) g_kb[o][s] = 0; continue; }   /* own duplicate */
            if (kb_conflicts(op, o, g_kb[o][s], key)) {
                if (!r.stole) { r.stole = 1; r.prev_op = o; r.prev_slot = s; }
                g_kb[o][s] = 0;
            }
        }
    }
    g_kb[op][slot] = key;
    r.ok = 1;
    return r;
}

/* is `bind` of `op` beaten by a more specific bind of another live op? */
static int kb_shadowed_by_other(int op, int bind, int scan, int mods, int ctx) {
    int o, s;
    for (o = 1; o < KB_MAX_OPS; o++) {
        if (o == op || !kb_op_active(o, ctx)) continue;
        for (s = 1; s <= KB_SLOTS; s++)
            if (g_kb[o][s] && kb_shadowed(bind, g_kb[o][s], scan, mods)) return 1;
    }
    return 0;
}

int kb_host_down(const unsigned char *keys, int mods, int ctx, int op) {
    int s;
    if (!kb_op_valid(op) || !kb_op_active(op, ctx)) return 0;
    for (s = 1; s <= KB_SLOTS; s++) {
        int b = g_kb[op][s], scan;
        if (!b) continue;
        scan = kb_key_scan(b);
        if (scan <= 0 || scan >= KB_NUM_SCANCODES || !keys[scan]) continue;
        if (!kb_match(b, scan, mods)) continue;
        if (kb_shadowed_by_other(op, b, scan, mods, ctx)) continue;
        return 1;
    }
    return 0;
}

int kb_host_event(int scan, int mods, int ctx, int op) {
    int s;
    if (!kb_op_valid(op) || !kb_op_active(op, ctx)) return 0;
    for (s = 1; s <= KB_SLOTS; s++) {
        int b = g_kb[op][s];
        if (!b || !kb_match(b, scan, mods)) continue;
        if (kb_shadowed_by_other(op, b, scan, mods, ctx)) continue;
        return 1;
    }
    return 0;
}

int kb_host_serialize(char *out, int cap) {
    int n, op;
    n = snprintf(out, (size_t)cap, "SHANKPIT_KEYBINDS 1\n");
    if (n < 0 || n >= cap) return -1;
    for (op = 1; op < KB_MAX_OPS; op++) {
        int w;
        if (kb_is_default(op, g_kb[op][1], g_kb[op][2], g_kb[op][3])) continue;
        w = snprintf(out + n, (size_t)(cap - n), "%d %d %d %d\n", op, g_kb[op][1], g_kb[op][2], g_kb[op][3]);
        if (w < 0 || w >= cap - n) return -1;
        n += w;
    }
    return n;
}

int kb_host_deserialize(const char *text) {
    int loaded = 0;
    const char *p = text;
    if (strncmp(p, "SHANKPIT_KEYBINDS 1", 19) != 0) return -1;
    while (*p && *p != '\n') p++;
    kb_host_init();
    while (*p) {
        char line[96];
        int op, k[3], i, len = 0, extra = 0;
        while (*p == '\n' || *p == '\r') p++;
        if (!*p) break;
        while (*p && *p != '\n' && len < (int)sizeof line - 1) line[len++] = *p++;
        line[len] = '\0';
        while (*p && *p != '\n') p++;   /* over-long line: drop the rest */
        if (sscanf(line, "%d %d %d %d %d", &op, &k[0], &k[1], &k[2], &extra) != 4) continue;
        if (!kb_op_valid(op)) continue;
        for (i = 0; i < 3; i++) if (!kb_key_valid(k[i]) || k[i] == 41) break;
        if (i < 3) continue;
        for (i = 0; i < 3; i++) g_kb[op][i + 1] = k[i];
        loaded++;
    }
    return loaded;
}
