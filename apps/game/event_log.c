/* apps/game/event_log.c -- the host-side JSONL tail over the level-mod REFLUX family.
 *
 * No rules live here: the row numbers and their payload meanings are reflux_runtime.h's, and the
 * registry already applies them. This file only turns the rows it has not written yet into lines.
 */
#include "event_log.h"

#include "game_api.h"
#include "level_mods_host.h"
#include "reflux_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *g_file;
static int g_cursor;

int event_log_open(const char *path) {
    if (!path) return 0;
    event_log_close();
    g_file = fopen(path, "ab");
    if (!g_file) return 0;
    /* Opening mid-run is not a replay: start at the current end. */
    g_cursor = reflux_host_total_dispatched();
    return 1;
}

void event_log_close(void) {
    if (g_file) fclose(g_file);
    g_file = NULL;
    g_cursor = 0;
}

void event_log_reset_cursor(void) {
    g_cursor = 0;
}

/* ---- replay ------------------------------------------------------------------------------------ */

static int line_int(const char *line, const char *key, int fallback) {
    char pattern[32];
    const char *p;
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    p = strstr(line, pattern);
    if (!p) return fallback;
    p = strchr(p, ':');
    if (!p) return fallback;
    return (int)strtol(p + 1, NULL, 10);
}

static int line_string(const char *line, const char *key, char *out, size_t cap) {
    char pattern[32];
    const char *p;
    size_t n = 0;

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    p = strstr(line, pattern);
    if (!p || cap == 0) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return 0;
    p++;
    while (*p && *p != '"' && n + 1 < cap) out[n++] = *p++;
    out[n] = '\0';
    return n > 0 && *p == '"';
}

int event_log_replay(const char *path) {
    FILE *f;
    char line[1024];
    int applied = 0;

    if (!path || !(f = fopen(path, "rb"))) return -1;
    while (fgets(line, sizeof(line), f)) {
        char event[24];
        int level, mod, on, mask, revision, env;

        if (!line_string(line, "event", event, sizeof(event))) continue;
        if (strcmp(event, "log_reset") == 0) {
            level_mods_reset();
            reflux_host_reset();
            event_log_reset_cursor();
            continue;
        }

        level = line_int(line, "level", 0);
        if (level <= 0) continue;

        if (strcmp(event, "registered") == 0) {
            env = line_int(line, "env", lm_env_surface());
            if (level_mods_env(level) < 0) applied += level_mods_register(level, env) ? 1 : 0;
        } else if (strcmp(event, "mod_set") == 0) {
            mod = line_int(line, "mod", 0);
            on = line_int(line, "on", 0);
            if (level_mods_set(level, mod, on)) applied++;
        } else if (strcmp(event, "state") == 0) {
            mask = line_int(line, "mask", 0);
            revision = line_int(line, "revision", 0);
            if (level_mods_env(level) < 0) level_mods_register(level, lm_env_surface());
            if (level_mods_apply_state(level, mask, revision)) applied++;
        }
    }
    fclose(f);
    return applied;
}

int event_log_sync(int tick) {
    int total, oldest, written = 0;

    if (!g_file) return 0;
    total = reflux_host_total_dispatched();
    oldest = reflux_host_oldest_abs();

    if (g_cursor > total) {
        /* The log was reset (a new round) and the cursor still points into the old one. */
        fprintf(g_file, "{\"seq\":-1,\"tick\":%d,\"event\":\"log_reset\"}\n", tick);
        g_cursor = 0;
        written++;
    }
    if (g_cursor < oldest) g_cursor = oldest; /* overtaken rows are gone; say nothing false */

    while (g_cursor < total) {
        int idx = reflux_host_index_for_abs(g_cursor);
        int type, a, b, c;

        if (idx < 0) {
            g_cursor = total;
            break;
        }
        type = reflux_host_action_type_at(idx);
        a = reflux_host_action_a_at(idx);
        b = reflux_host_action_b_at(idx);
        c = reflux_host_action_c_at(idx);

        if (reflux_action_is_level_mod(type)) {
            if (type == REFLUX_ACTION_LEVEL_REGISTERED) {
                fprintf(g_file,
                        "{\"seq\":%d,\"tick\":%d,\"event\":\"registered\","
                        "\"level\":%d,\"env\":%d,\"mask\":%d}\n",
                        g_cursor, tick, a, b, c);
            } else if (type == REFLUX_ACTION_LEVEL_MOD_SET) {
                fprintf(g_file,
                        "{\"seq\":%d,\"tick\":%d,\"event\":\"mod_set\","
                        "\"level\":%d,\"mod\":%d,\"on\":%d}\n",
                        g_cursor, tick, a, b, c);
            } else if (type == REFLUX_ACTION_LEVEL_STATE) {
                fprintf(g_file,
                        "{\"seq\":%d,\"tick\":%d,\"event\":\"state\","
                        "\"level\":%d,\"mask\":%d,\"revision\":%d}\n",
                        g_cursor, tick, a, b, c);
            }
            written++;
        }
        g_cursor++;
    }
    fflush(g_file);
    return written;
}
