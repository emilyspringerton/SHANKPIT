/* packages/simulation/trapx_events.c -- the bus itself. Small on purpose: an event bus that decides things is a
 * second game. See the header for why this is the shape "mods first" takes without function pointers. */

#include "trapx_events.h"

#include <string.h>

static const char *const TRAPX_EVENT_NAMES[TRAPX_EV_COUNT] = {
    "tick", "claim", "contest", "k9", "alert", "enforce", "receipt", "kill", "rogue", "portal",
};

void trapx_events_reset(TrapxEvents *e) {
    if (!e) return;
    memset(e, 0, sizeof(*e));
}

unsigned long trapx_events_push(TrapxEvents *e, int kind, int tick, int scene, int a, int b) {
    if (!e || kind < 0 || kind >= TRAPX_EV_COUNT) return 0;
    TrapxEvent *slot = &e->ring[e->head];
    slot->kind = kind;
    slot->tick = tick;
    slot->scene = scene;
    slot->a = a;
    slot->b = b;
    e->head = (e->head + 1) % TRAPX_EVENT_CAP;
    e->seq += 1;
    if (e->count < TRAPX_EVENT_CAP) e->count += 1;
    return e->seq;
}

int trapx_events_count(const TrapxEvents *e) {
    return e ? e->count : 0;
}

const TrapxEvent *trapx_events_at(const TrapxEvents *e, int index) {
    if (!e || index < 0 || index >= e->count) return NULL;
    int slot = (e->head - 1 - index) % TRAPX_EVENT_CAP;
    if (slot < 0) slot += TRAPX_EVENT_CAP;
    return &e->ring[slot];
}

const char *trapx_event_name(int kind) {
    if (kind < 0 || kind >= TRAPX_EV_COUNT) return "unknown";
    return TRAPX_EVENT_NAMES[kind];
}

void trapx_mods_reset(TrapxMods *m) {
    if (!m) return;
    memset(m, 0, sizeof(*m));
}

static int name_ok(const char *name) {
    if (!name || name[0] == '\0') return 0;
    return strlen(name) < TRAPX_MOD_NAMELEN;
}

int trapx_mods_index(const TrapxMods *m, const char *name) {
    if (!m || !name) return -1;
    for (int i = 0; i < m->count; i++) {
        if (strcmp(m->mods[i].name, name) == 0) return i;
    }
    return -1;
}

int trapx_mods_add(TrapxMods *m, const char *name, unsigned mask, int priority) {
    if (!m || !name_ok(name)) return -1;
    if (m->count >= TRAPX_MOD_CAP) return -1;
    if (trapx_mods_index(m, name) >= 0) return -1;   /* one name, one mod: never overwrite silently */
    TrapxMod *mod = &m->mods[m->count];
    memset(mod, 0, sizeof(*mod));
    strncpy(mod->name, name, TRAPX_MOD_NAMELEN - 1);
    mod->mask = mask;
    mod->priority = priority;
    mod->enabled = 1;
    m->count += 1;
    return m->count - 1;
}

int trapx_mods_enable(TrapxMods *m, int index, int on) {
    if (!m || index < 0 || index >= m->count) return 0;
    m->mods[index].enabled = on ? 1 : 0;
    return 1;
}

int trapx_mods_wants(const TrapxMods *m, int index, int kind) {
    if (!m || index < 0 || index >= m->count || kind < 0 || kind >= TRAPX_EV_COUNT) return 0;
    const TrapxMod *mod = &m->mods[index];
    if (!mod->enabled) return 0;
    return (mod->mask >> (unsigned)kind) & 1u;
}

int trapx_mods_deliver(const TrapxMods *m, const TrapxEvent *ev, int *out, int out_cap) {
    if (!m || !ev || !out || out_cap <= 0) return 0;
    int written = 0;
    /* Priority first, then registration order. Selection sort over at most TRAPX_MOD_CAP entries: the ordering is
     * the contract, so it is written out rather than trusted to qsort's stability story. */
    int used[TRAPX_MOD_CAP];
    memset(used, 0, sizeof(used));
    while (written < out_cap) {
        int best = -1;
        for (int i = 0; i < m->count; i++) {
            if (used[i] || !trapx_mods_wants(m, i, ev->kind)) continue;
            if (best < 0 || m->mods[i].priority > m->mods[best].priority) best = i;
        }
        if (best < 0) break;
        used[best] = 1;
        out[written++] = best;
    }
    return written;
}
