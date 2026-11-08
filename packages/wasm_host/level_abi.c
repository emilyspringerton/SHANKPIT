/* packages/wasm_host/level_abi.c -- see level_abi.h. Plumbing only (INV-1.2): every decision a level makes is
 * in its own PARENA source; this file moves REFLUX rows across the module boundary and refuses the ones a
 * module may not dispatch, using the family predicate from the shared action-type table. */
#include "level_abi.h"

#include <stdio.h>
#include <string.h>

#include "../reflux/reflux_runtime.h"

void level_reflux_bridge_init(level_reflux_bridge *b, int (*accepts)(int action_type)) {
    if (!b) return;
    b->accepts = accepts;
    b->accepted = 0;
    b->rejected = 0;
    b->last_rejected = 0;
}

/* ---- the eight imports ------------------------------------------------------------------------- */

static int32_t lr_dispatch(void *user, const int32_t *a) {
    level_reflux_bridge *b = (level_reflux_bridge *)user;
    if (!b->accepts || !b->accepts(a[0])) {
        b->rejected++;
        b->last_rejected = a[0];
        return 0;
    }
    reflux_host_dispatch(a[0], a[1], a[2], a[3]);
    b->accepted++;
    return 0;
}

static int32_t lr_total(void *user, const int32_t *a) {
    (void)user;
    (void)a;
    return reflux_host_total_dispatched();
}

static int32_t lr_index_for_abs(void *user, const int32_t *a) {
    (void)user;
    return reflux_host_index_for_abs(a[0]);
}

static int32_t lr_oldest(void *user, const int32_t *a) {
    (void)user;
    (void)a;
    return reflux_host_oldest_abs();
}

static int32_t lr_type_at(void *user, const int32_t *a) {
    (void)user;
    return reflux_host_action_type_at(a[0]);
}

static int32_t lr_a_at(void *user, const int32_t *a) {
    (void)user;
    return reflux_host_action_a_at(a[0]);
}

static int32_t lr_b_at(void *user, const int32_t *a) {
    (void)user;
    return reflux_host_action_b_at(a[0]);
}

static int32_t lr_c_at(void *user, const int32_t *a) {
    (void)user;
    return reflux_host_action_c_at(a[0]);
}

int level_reflux_imports(level_reflux_bridge *b, wh_import out[LEVEL_ABI_N_IMPORTS]) {
    static const struct {
        const char *field;
        const char *sig;
        wh_host_fn fn;
    } k[LEVEL_ABI_N_IMPORTS] = {
        {"reflux_host_dispatch", "v(iiii)", lr_dispatch},
        {"reflux_host_total_dispatched", "i()", lr_total},
        {"reflux_host_index_for_abs", "i(i)", lr_index_for_abs},
        {"reflux_host_oldest_abs", "i()", lr_oldest},
        {"reflux_host_action_type_at", "i(i)", lr_type_at},
        {"reflux_host_action_a_at", "i(i)", lr_a_at},
        {"reflux_host_action_b_at", "i(i)", lr_b_at},
        {"reflux_host_action_c_at", "i(i)", lr_c_at},
    };
    int i;
    for (i = 0; i < LEVEL_ABI_N_IMPORTS; i++) {
        out[i].module = "env";
        out[i].field = k[i].field;
        out[i].sig = k[i].sig;
        out[i].fn = k[i].fn;
        out[i].user = b;
    }
    return LEVEL_ABI_N_IMPORTS;
}

/* ---- the exports ------------------------------------------------------------------------------- */

static const struct {
    const char *name;
    const char *sig;
    size_t off;
} k_exports[LEVEL_ABI_N_EXPORTS] = {
    {"level_api", "i()", offsetof(level_exports, api)},
    {"level_enter", "i()", offsetof(level_exports, enter)},
    {"level_poll", "i(i)", offsetof(level_exports, poll)},
    {"level_tick", "v()", offsetof(level_exports, tick)},
    {"level_render", "v()", offsetof(level_exports, render)},
    {"level_exit", "v()", offsetof(level_exports, exit)},
    {"level_seed", "v(i)", offsetof(level_exports, seed)},
    {"level_key", "v(ii)", offsetof(level_exports, key)},
    {"level_pointer", "v(iii)", offsetof(level_exports, pointer)},
    {"level_blur", "v()", offsetof(level_exports, blur)},
    {"level_fb_addr", "i()", offsetof(level_exports, fb_addr)},
    {"level_fb_w", "i()", offsetof(level_exports, fb_w)},
    {"level_fb_h", "i()", offsetof(level_exports, fb_h)},
    {"level_asset_addr", "i(i)", offsetof(level_exports, asset_addr)},
    {"level_asset_set", "v(ii)", offsetof(level_exports, asset_set)},
    {"level_lost_rows", "i()", offsetof(level_exports, lost_rows)},
};

wh_status level_exports_bind(wh_instance *inst, level_exports *x, char *msg, size_t msglen) {
    int i;
    if (!inst || !x) return WH_ERR_ARGS;
    memset(x, 0, sizeof *x);
    for (i = 0; i < LEVEL_ABI_N_EXPORTS; i++) {
        wh_func *f = (wh_func *)(void *)((char *)x + k_exports[i].off);
        wh_status s = wh_lookup(inst, k_exports[i].name, k_exports[i].sig, f, msg, msglen);
        if (s != WH_OK) return s;
    }
    return WH_OK;
}

int level_abi_export_info(int i, const char **name, const char **sig) {
    if (i < 0 || i >= LEVEL_ABI_N_EXPORTS) return 0;
    if (name) *name = k_exports[i].name;
    if (sig) *sig = k_exports[i].sig;
    return 1;
}
