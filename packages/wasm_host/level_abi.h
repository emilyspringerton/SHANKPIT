/* packages/wasm_host/level_abi.h -- the host's half of the level surface: the REFLUX imports a level module
 * is offered (range-checked), and the exports the host binds (T0 step 3, docs/T0_LEVEL_WASM_CONTRACT.md
 * section 2, "Exports" / "Imports" / "Action types").
 *
 * Founder real-time 2026-11-08: "Boot the SHANKPIT world plus BRAWLPIT as WASM, BRAWLPIT in the foreground",
 * then "implement as always mods first parena first mods driven development". A level is a mod: it talks to
 * the rest of the world ONLY through REFLUX (IFC-2.3), through the same eight reflux_host_* functions every
 * native mod and PARENA's stdlib/reflux/reflux.prn already use -- no new FFI. What a level adds is the trust
 * boundary: its dispatch import accepts only the module's own reserved family (reflux_runtime.h, e.g.
 * reflux_action_is_brawlpit for 1100-1199), so untrusted code can never put a lifecycle row ("level closed")
 * or another mod's row ("button pressed") into the log. A refused dispatch is counted, never logged.
 *
 * reflux_host_log_size is deliberately NOT offered: it saturates at REFLUX_LOG_CAPACITY, and a cursor taken
 * from it goes silent forever (reflux_runtime.h, the horse_sync bug). A module that imports it is refused at
 * load (WH_ERR_IMPORT_UNBOUND). The level reads the log from a HOST-held absolute cursor (INV-3R.3) through
 * level_poll(cursor) -> cursor.
 *
 * The export table is the host's declaration of the surface it speaks (LEVEL_ABI_API); a level's build
 * manifest declares what it provides (PARENA stdlib/<level>/level.manifest), and level_abi_test.c checks the
 * two agree. Strict C99 (-std=c99 -Wall -Wextra -pedantic -Werror), no SDL. */
#ifndef SHANKPIT_LEVEL_ABI_H
#define SHANKPIT_LEVEL_ABI_H

#include <stddef.h>
#include "wasm_host.h"

#define LEVEL_ABI_API 1          /* the only level_api() value this host binds */
#define LEVEL_ABI_N_IMPORTS 8    /* env.reflux_host_*: dispatch, total, index_for_abs, oldest, type/a/b/c_at */
#define LEVEL_ABI_N_EXPORTS 16   /* plus the exported memory */

/* One per instance: the user pointer of all eight imports. */
typedef struct {
    int (*accepts)(int action_type); /* the module's own family, e.g. reflux_action_is_brawlpit */
    int accepted;                    /* dispatches that reached the log */
    int rejected;                    /* dispatches outside the family, dropped before the log */
    int last_rejected;               /* the action type of the most recent refusal (diagnostics) */
} level_reflux_bridge;

void level_reflux_bridge_init(level_reflux_bridge *b, int (*accepts)(int action_type));

/* Fills out[0 .. LEVEL_ABI_N_IMPORTS-1] with the module "env" imports bound to b; returns the count.
 * b must outlive the instance. */
int level_reflux_imports(level_reflux_bridge *b, wh_import out[LEVEL_ABI_N_IMPORTS]);

/* The bound exports (contract section 2, "Exports"). */
typedef struct {
    wh_func api, enter, poll, tick, render, exit, seed, key, pointer, blur;
    wh_func fb_addr, fb_w, fb_h, asset_addr, asset_set, lost_rows;
} level_exports;

/* Binds all LEVEL_ABI_N_EXPORTS by exact name and signature. On failure the first export that did not
 * bind is named in msg (WH_ERR_EXPORT_MISSING / WH_ERR_EXPORT_SIG). */
wh_status level_exports_bind(wh_instance *inst, level_exports *x, char *msg, size_t msglen);

/* The table itself, for tests and diagnostics: entry i's wire name and wasm3 signature. 0 past the end. */
int level_abi_export_info(int i, const char **name, const char **sig);

#endif /* SHANKPIT_LEVEL_ABI_H */
