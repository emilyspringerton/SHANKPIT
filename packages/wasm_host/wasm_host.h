/* packages/wasm_host/wasm_host.h -- run an untrusted WebAssembly module inside SHANKPIT.
 *
 * Founder real-time 2026-11-08 (SHANKPIT/specs/2026-11-08-0000.md, T0): boot the SHANKPIT world with
 * BRAWLPIT as a WASM level in the foreground. Contract: docs/T0_LEVEL_WASM_CONTRACT.md, "Host C API".
 *
 * What this is: the host's one doorway to a wasm module. It is plumbing (INV-1.2): load, bind, call,
 * report. It makes no product decision -- which module, when to tick it, what a trap means for the
 * player are the level manager's (PARENA, level_manager_rules.prn).
 *
 *   bind-or-refuse   wh_instantiate reads the module's import section with the PARENA surface reader
 *                    (PARENA/stdlib/wasm/surface.prn, generated into wasm_surface_gen.c) BEFORE linking,
 *                    and refuses a module that imports anything the host does not offer, or offers with
 *                    another signature. The runtime alone would only trap on the first call.
 *   exact exports    wh_lookup binds an export by exact name AND signature.
 *   traps are final  a trap returns WH_ERR_TRAP and marks the instance dead: every later call refuses,
 *                    so a half-run module is never driven again.
 *
 * STOPGAP (core deps are PARENA-first, MONOREPO CLAUDE.md): the interpreter underneath is wasm3, vendored
 * at a pinned commit under third_party/wasm3 (see its VENDORED.md). Nothing outside wasm_host.c sees a
 * wasm3 type, so the queued PARENA-native interpreter (EMILY/BACKLOG.md PARENA-WASM-INTERP) or WAMR can
 * replace it without touching a caller.
 *
 * Signatures use wasm3's notation: result type, then parameters in parentheses -- v none, i i32, I i64,
 * f f32, F f64. "v(iiii)", "i()", "v(F)". Host functions take and return i32 only (every REFLUX call is
 * i32); exports may take i32 and f64 (BRAWLPIT's web entry bp_frame takes an f64 timestamp).
 *
 * Strict C99 (-std=c99 -Wall -Wextra -pedantic -Werror). Not thread-safe; one instance per caller.
 */
#ifndef SHANKPIT_WASM_HOST_H
#define SHANKPIT_WASM_HOST_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    WH_OK = 0,
    WH_ERR_ARGS = 1,             /* a NULL or out-of-range argument */
    WH_ERR_PARSE = 2,            /* not a well-formed module (header, sections, or the runtime's own parse) */
    WH_ERR_IMPORT_UNBOUND = 3,   /* the module imports a function the host does not offer */
    WH_ERR_IMPORT_SIG = 4,       /* offered, but with a different signature */
    WH_ERR_IMPORT_KIND = 5,      /* imports a memory, table or global: the host offers none */
    WH_ERR_LOAD = 6,             /* the runtime refused to load or link it */
    WH_ERR_EXPORT_MISSING = 7,   /* wh_lookup: no export of that name */
    WH_ERR_EXPORT_SIG = 8,       /* wh_lookup: exported with a different signature */
    WH_ERR_TRAP = 9,             /* the call trapped (or an earlier one did): the instance is dead */
    WH_ERR_NOMEM = 10
} wh_status;

/* One host function offered to modules. `fn` gets the call's i32 arguments in order and returns the
   i32 result (ignored for a "v(...)" signature). `user` is passed through untouched. */
typedef int32_t (*wh_host_fn)(void *user, const int32_t *args);

typedef struct {
    const char *module;     /* "env" */
    const char *field;      /* "reflux_host_dispatch" */
    const char *sig;        /* "v(iiii)" */
    wh_host_fn fn;
    void *user;
} wh_import;

/* An argument or result. `kind` is the signature letter: 'i' or 'F'. */
typedef struct {
    char kind;
    int32_t i32;
    double f64;
} wh_val;

typedef struct wh_instance wh_instance;

typedef struct {
    wh_instance *inst;
    void *impl;             /* the runtime's function handle */
    char sig[24];
    int nargs;
} wh_func;

/* Load `bytes` (copied; the caller may free them afterwards), check every import against `imports`,
   link, and run the module's start function if it has one. On failure *out stays NULL and `msg` (if
   non-NULL) says why in one line, naming the offending import or export. `stack_bytes` is the runtime's
   value stack (0 = 64 KiB). */
wh_status wh_instantiate(const uint8_t *bytes, size_t n, const wh_import *imports, int n_imports,
                         uint32_t stack_bytes, wh_instance **out, char *msg, size_t msglen);
void wh_destroy(wh_instance *inst);

/* Bind an export by exact name and signature. */
wh_status wh_lookup(wh_instance *inst, const char *name, const char *sig, wh_func *out, char *msg, size_t msglen);

/* Call a bound export. `args` must match its signature (wh_val.kind is checked); `ret` may be NULL for a
   "v(...)" export. A trap kills the instance: WH_ERR_TRAP now and for every later call. */
wh_status wh_call(wh_func *fn, const wh_val *args, int nargs, wh_val *ret, char *msg, size_t msglen);

/* Convenience for the all-i32 exports the level surface uses. */
wh_status wh_call_i32(wh_func *fn, const int32_t *args, int nargs, int32_t *ret, char *msg, size_t msglen);

/* The module's linear memory (NULL if it has none). Valid until the next call into the module. */
uint8_t *wh_memory(wh_instance *inst, uint32_t *size_out);

int wh_is_dead(const wh_instance *inst);

/* A one-line name for a status ("import unbound", ...). */
const char *wh_status_name(wh_status s);

#endif /* SHANKPIT_WASM_HOST_H */
