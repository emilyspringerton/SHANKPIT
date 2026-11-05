/* packages/reflux/reflux_runtime.c -- the missing native half of REFLUX.
 *
 * packages/reflux/reflux_runtime.h has declared this contract since S485, and the Makefile's own
 * LOBBY_SRC/SERVER_SRC already list this file, but the clean-room tree carried no implementation:
 * a genuinely missing piece of glue, not a rewrite of anything. This implements exactly what the
 * header documents -- an append-only ring of {type, a, b, c} rows, a per-process host log, and
 * absolute-count accessors so a subscriber cursor never goes permanently silent when the ring wraps
 * (the real bug the header's own comment describes).
 *
 * No callbacks and no function pointers: VS0 has neither, and the header's whole design is the
 * log-plus-cursor substitute. Dispatch never learns who is listening; a poller never has to be known.
 */
#include "reflux_runtime.h"

#include <string.h>

void reflux_log_reset(RefluxLog *log) {
    if (!log) return;
    memset(log, 0, sizeof(*log));
}

void reflux_log_dispatch(RefluxLog *log, int action_type, int a, int b, int c) {
    RefluxAction *slot;
    if (!log) return;
    slot = &log->actions[log->total_dispatched % REFLUX_LOG_CAPACITY];
    slot->action_type = action_type;
    slot->a = a;
    slot->b = b;
    slot->c = c;
    log->total_dispatched += 1;
}

int reflux_log_length(const RefluxLog *log) {
    if (!log) return 0;
    if (log->total_dispatched < REFLUX_LOG_CAPACITY) return log->total_dispatched;
    return REFLUX_LOG_CAPACITY;
}

const RefluxAction *reflux_log_at(const RefluxLog *log, int index) {
    int len;
    if (!log) return NULL;
    len = reflux_log_length(log);
    if (index < 0 || index >= len) return NULL;
    return &log->actions[(log->total_dispatched - len + index) % REFLUX_LOG_CAPACITY];
}

/* ---- the one per-process log (reflux_host_*) ----------------------------------------------------- */

static RefluxLog g_host_log;

void reflux_host_reset(void) { reflux_log_reset(&g_host_log); }

void reflux_host_dispatch(int action_type, int a, int b, int c) {
    reflux_log_dispatch(&g_host_log, action_type, a, b, c);
}

int reflux_host_log_size(void) { return reflux_log_length(&g_host_log); }

int reflux_host_total_dispatched(void) { return g_host_log.total_dispatched; }

int reflux_host_oldest_abs(void) {
    int len = reflux_log_length(&g_host_log);
    return g_host_log.total_dispatched - len;
}

int reflux_host_index_for_abs(int abs_index) {
    int oldest = reflux_host_oldest_abs();
    if (abs_index < oldest) return -1;
    if (abs_index >= g_host_log.total_dispatched) return -1;
    return abs_index - oldest;
}

static const RefluxAction *host_row(int index) {
    return reflux_log_at(&g_host_log, index);
}

int reflux_host_action_type_at(int index) {
    const RefluxAction *row = host_row(index);
    return row ? row->action_type : -1;
}

int reflux_host_action_a_at(int index) {
    const RefluxAction *row = host_row(index);
    return row ? row->a : 0;
}

int reflux_host_action_b_at(int index) {
    const RefluxAction *row = host_row(index);
    return row ? row->b : 0;
}

int reflux_host_action_c_at(int index) {
    const RefluxAction *row = host_row(index);
    return row ? row->c : 0;
}
