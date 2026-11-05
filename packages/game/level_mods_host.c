/* packages/game/level_mods_host.c -- the live per-level module table.
 *
 * No rule lives here. Default masks, mandatory-module protection, mask arithmetic and the row numbers
 * are packages/game/level_mods.prn, compiled into game_rules.c and called by name. This file only holds
 * the state a pure function cannot: which levels exist, their mask/revision, and each one's cursor into
 * the shared REFLUX log.
 *
 * Why a cursor per level rather than one global subscriber: two levels can be live at once and a
 * WEATHER update addressed to level 7 must never touch level 8's mask. The level id is in field a of
 * every row for exactly that reason, and sync filters on it.
 */
#include "level_mods_host.h"

#include "game_api.h"
#include "reflux_runtime.h"

#include <string.h>

typedef struct {
    int used;
    int level_id;
    int env;
    int mask;
    int revision;
    int cursor; /* absolute REFLUX dispatch index this level has read up to */
} LevelModEntry;

static LevelModEntry g_levels[LEVEL_MODS_MAX_LEVELS];

/* Unset means "this build links every module". See level_mods_set_capabilities(). */
static int g_capabilities;
static int g_capabilities_set;

static int capabilities(void) {
    return g_capabilities_set ? g_capabilities : lm_all_mask();
}

int level_mods_set_capabilities(int mask) {
    /* lm_sanitize keeps the mandatory bits on, and lm_all_mask bounds the result to real modules. */
    g_capabilities = lm_sanitize(mask) & lm_all_mask();
    g_capabilities_set = 1;
    return g_capabilities;
}

int level_mods_capabilities(void) { return capabilities(); }

static LevelModEntry *find_level(int level_id) {
    int i;
    for (i = 0; i < LEVEL_MODS_MAX_LEVELS; i++) {
        if (g_levels[i].used && g_levels[i].level_id == level_id) return &g_levels[i];
    }
    return NULL;
}

static const LevelModEntry *find_level_const(int level_id) {
    return find_level(level_id);
}

void level_mods_reset(void) { memset(g_levels, 0, sizeof(g_levels)); }

int level_mods_count(void) {
    int i, n = 0;
    for (i = 0; i < LEVEL_MODS_MAX_LEVELS; i++) {
        if (g_levels[i].used) n++;
    }
    return n;
}

int level_mods_level_at(int index) {
    if (index < 0 || index >= LEVEL_MODS_MAX_LEVELS) return -1;
    return g_levels[index].used ? g_levels[index].level_id : -1;
}

int level_mods_register(int level_id, int env) {
    LevelModEntry *e;
    int i;

    if (level_id <= 0 || !lm_env_valid(env)) return 0;
    e = find_level(level_id);
    if (e) return e->mask; /* already live: do not reset a level that is running */

    for (i = 0; i < LEVEL_MODS_MAX_LEVELS; i++) {
        if (!g_levels[i].used) break;
    }
    if (i == LEVEL_MODS_MAX_LEVELS) return 0;

    e = &g_levels[i];
    e->used = 1;
    e->level_id = level_id;
    e->env = env;
    e->mask = lm_sanitize(lm_default_mask(env)) & capabilities();
    e->revision = 1;
    e->cursor = reflux_host_total_dispatched();

    reflux_host_dispatch(lm_evt_registered(), level_id, env, e->mask);
    return e->mask;
}

int level_mods_request(int level_id, int mod, int on) {
    if (!find_level(level_id)) return 0;
    if (!lm_mod_valid(mod)) return 0;
    if (on == 0 && lm_mandatory(mod)) return 0;
    if (on && !(capabilities() & lm_bit(mod))) return 0; /* this build has not ported that module */
    reflux_host_dispatch(lm_evt_mod_set(), level_id, mod, on ? 1 : 0);
    return 1;
}

int level_mods_apply(int level_id, int mod, int on) {
    LevelModEntry *e = find_level(level_id);
    int after;

    if (!e) return 0;
    if (on && !(capabilities() & lm_bit(mod))) return 0;
    after = lm_set(e->mask, mod, on ? 1 : 0);
    if (after == e->mask) return 0;

    e->mask = after;
    e->revision = lm_next_revision(e->revision);
    return 1;
}

int level_mods_apply_state(int level_id, int mask, int revision) {
    LevelModEntry *e = find_level(level_id);
    int clean, rev;

    if (!e) return 0;
    clean = lm_sanitize(mask) & capabilities();
    rev = revision > 0 ? revision : e->revision;
    if (rev < e->revision) return 0; /* stale snapshot: the level has already moved on */

    if (clean == e->mask) {
        if (rev > e->revision) e->revision = rev;
        return 0;
    }
    e->mask = clean;
    e->revision = rev;
    return 1;
}

int level_mods_sync(int level_id) {
    LevelModEntry *e = find_level(level_id);
    int total, oldest, applied = 0;

    if (!e) return 0;

    total = reflux_host_total_dispatched();
    oldest = reflux_host_oldest_abs();
    if (e->cursor < oldest) e->cursor = oldest; /* overtaken rows are gone, not silently skipped */
    if (e->cursor > total) e->cursor = total;   /* the log was reset under us */

    /* total is captured before applying: applying never dispatches, but bounding the pass means a
     * future row-producing apply cannot make this loop chase its own tail. */
    while (e->cursor < total) {
        int idx = reflux_host_index_for_abs(e->cursor);
        int type, a, b, c;

        if (idx < 0) {
            e->cursor = total;
            break;
        }
        type = reflux_host_action_type_at(idx);
        a = reflux_host_action_a_at(idx);
        b = reflux_host_action_b_at(idx);
        c = reflux_host_action_c_at(idx);

        if (a == level_id && type == lm_evt_mod_set()) {
            applied += level_mods_apply(level_id, b, c);
        } else if (a == level_id && type == lm_evt_state()) {
            /* Same authoritative-state rule the network relay uses: sanitise, apply forward only. */
            if (level_mods_apply_state(level_id, b, c)) applied++;
        }
        e->cursor++;
    }
    return applied;
}

int level_mods_set(int level_id, int mod, int on) {
    int changed;
    if (!level_mods_request(level_id, mod, on)) return 0;
    changed = level_mods_sync(level_id);
    /* Keep the full-state stream current for a reader that joined after the delta row, without
     * changing the delta row's meaning for everyone else. */
    if (changed > 0) level_mods_publish(level_id);
    return changed > 0;
}

int level_mods_publish(int level_id) {
    LevelModEntry *e = find_level(level_id);
    if (!e) return 0;
    reflux_host_dispatch(lm_evt_state(), level_id, e->mask, e->revision);
    return 1;
}

int level_mods_env(int level_id) {
    const LevelModEntry *e = find_level_const(level_id);
    return e ? e->env : -1;
}

int level_mods_mask(int level_id) {
    const LevelModEntry *e = find_level_const(level_id);
    return e ? e->mask : 0;
}

int level_mods_revision(int level_id) {
    const LevelModEntry *e = find_level_const(level_id);
    return e ? e->revision : 0;
}

int level_mods_enabled(int level_id, int mod) {
    const LevelModEntry *e = find_level_const(level_id);
    return e ? lm_enabled(e->mask, mod) : 0;
}

int level_mods_weather_enabled(int level_id) {
    return level_mods_enabled(level_id, lm_mod_weather_render());
}

int level_mods_sky_enabled(int level_id) {
    return level_mods_enabled(level_id, lm_mod_sky());
}
