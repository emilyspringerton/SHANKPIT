/* apps/game/event_log.c -- the host-side JSONL tail over the level-mod REFLUX family.
 *
 * No rules live here: the row numbers and their payload meanings are reflux_runtime.h's, and the
 * registry already applies them. This file only turns the rows it has not written yet into lines.
 */
#include "event_log.h"

#include "reflux_runtime.h"

#include <stdio.h>

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
