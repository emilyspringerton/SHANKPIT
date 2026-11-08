/* packages/wasm_host/wasm_host.c -- see wasm_host.h for scope and the STOPGAP note.
 *
 * The only file that sees a wasm3 type. Three rules it keeps:
 *   - the import check runs on the bytes BEFORE the runtime parses them (PARENA surface reader),
 *   - every host function goes through one trampoline that reads its arity from the bind record,
 *   - a trap is final for the instance (wh_is_dead), however the caller reacts to it.
 * Deferred, named: gas metering (this fork of wasm3 can meter, but only if armed before compiling and at
 * a cost per instruction -- a module that loops forever inside one call still hangs that frame), and
 * more than WH_MAX_BINDS distinct imports.
 */
#include "wasm_host.h"
#include "wasm_surface.h"
#include "../../third_party/wasm3/source/wasm3.h" /* relative: CI compiles LOBBY_SRC with a fixed -I list */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WH_MAX_BINDS 32
#define WH_NAME_MAX 128
#define WH_MEMORY_CAP (64u * 1024u * 1024u) /* a module may not make the host back more than this */

typedef struct {
    wh_host_fn fn;
    void *user;
    int nargs;
    int has_ret;
} wh_bind;

struct wh_instance {
    IM3Environment env;
    IM3Runtime rt;
    IM3Module mod;
    uint8_t *bytes;
    size_t n;
    int dead;
    int nbinds;
    wh_bind binds[WH_MAX_BINDS];
};

static void say(char *msg, size_t msglen, const char *text) {
    if (!msg || msglen == 0) return;
    strncpy(msg, text, msglen - 1);
    msg[msglen - 1] = '\0';
}

const char *wh_status_name(wh_status s) {
    switch (s) {
    case WH_OK: return "ok";
    case WH_ERR_ARGS: return "bad argument";
    case WH_ERR_PARSE: return "malformed module";
    case WH_ERR_IMPORT_UNBOUND: return "import not offered";
    case WH_ERR_IMPORT_SIG: return "import signature mismatch";
    case WH_ERR_IMPORT_KIND: return "non-function import";
    case WH_ERR_LOAD: return "runtime refused the module";
    case WH_ERR_EXPORT_MISSING: return "export missing";
    case WH_ERR_EXPORT_SIG: return "export signature mismatch";
    case WH_ERR_TRAP: return "trap";
    case WH_ERR_NOMEM: return "out of memory";
    }
    return "unknown";
}

/* ---- signatures --------------------------------------------------------------------------------- */

static char type_char(M3ValueType t) {
    switch (t) {
    case c_m3Type_i32: return 'i';
    case c_m3Type_i64: return 'I';
    case c_m3Type_f32: return 'f';
    case c_m3Type_f64: return 'F';
    default: return '?';
    }
}

/* the runtime's view of an export's signature, in the same notation the caller asks for */
static void func_sig(IM3Function f, char *out, size_t cap) {
    uint32_t na = m3_GetArgCount(f), nr = m3_GetRetCount(f), k;
    size_t p = 0;
    if (cap < 4) { if (cap) out[0] = '\0'; return; }
    out[p++] = (nr == 0) ? 'v' : (nr == 1) ? type_char(m3_GetRetType(f, 0)) : '?';
    out[p++] = '(';
    for (k = 0; k < na && p + 2 < cap; k++) out[p++] = type_char(m3_GetArgType(f, k));
    out[p++] = ')';
    out[p] = '\0';
}

/* the module's view of import i's signature (PARENA reader) */
static void import_sig_text(Bytes d, int i, char *out, size_t cap) {
    int sig = ws_import_sig(d, i), k = 0;
    while ((size_t)k + 1 < cap) {
        int c = ws_sig_char(sig, k);
        if (c == 0) break;
        out[k++] = (char)c;
    }
    out[k] = '\0';
}

/* a host function's signature must be all-i32: "v(...)" or "i(...)" with only 'i' parameters */
static int host_sig_ok(const char *sig, int *nargs, int *has_ret) {
    size_t len = strlen(sig), k;
    if (len < 3 || (sig[0] != 'v' && sig[0] != 'i') || sig[1] != '(' || sig[len - 1] != ')') return 0;
    for (k = 2; k + 1 < len; k++) if (sig[k] != 'i') return 0;
    *nargs = (int)(len - 3);
    *has_ret = sig[0] == 'i';
    return 1;
}

static void copy_name(Bytes d, int off, int len, char *out) {
    int k;
    if (off < 0 || len < 0 || len >= WH_NAME_MAX) { out[0] = '\0'; return; }
    for (k = 0; k < len; k++) out[k] = (char)d.data[off + k];
    out[len] = '\0';
}

/* ---- the one trampoline every import goes through ----------------------------------------------- */

static const void *wh_trampoline(IM3Runtime runtime, IM3ImportContext ctx, uint64_t *sp, void *mem) {
    const wh_bind *b = (const wh_bind *)ctx->userdata;
    int32_t args[16];
    int k, base = b->has_ret ? 1 : 0;
    int32_t r;
    (void)runtime;
    (void)mem;
    for (k = 0; k < b->nargs && k < 16; k++) {
        int32_t v;
        memcpy(&v, &sp[base + k], sizeof v);
        args[k] = v;
    }
    r = b->fn(b->user, args);
    if (b->has_ret) {
        uint64_t slot = 0;
        memcpy(&slot, &r, sizeof r);
        sp[0] = slot;
    }
    return m3Err_none;
}

/* ---- instantiate -------------------------------------------------------------------------------- */

static const wh_import *find_offer(const wh_import *imports, int n, const char *mod, const char *field) {
    int k;
    for (k = 0; k < n; k++)
        if (strcmp(imports[k].module, mod) == 0 && strcmp(imports[k].field, field) == 0) return &imports[k];
    return NULL;
}

wh_status wh_instantiate(const uint8_t *bytes, size_t n, const wh_import *imports, int n_imports,
                         uint32_t stack_bytes, wh_instance **out, char *msg, size_t msglen) {
    wh_instance *inst;
    Bytes d;
    M3Result r;
    int ni, i;
    char text[320];

    if (!out) return WH_ERR_ARGS;
    *out = NULL;
    if (!bytes || n < 8 || n > 0x7fffffffu || (n_imports > 0 && !imports) || n_imports < 0) {
        say(msg, msglen, "wh_instantiate: bad arguments");
        return WH_ERR_ARGS;
    }
    inst = (wh_instance *)calloc(1, sizeof *inst);
    if (!inst) return WH_ERR_NOMEM;
    inst->bytes = (uint8_t *)malloc(n); /* the runtime keeps pointers into the bytes for the module's life */
    if (!inst->bytes) { free(inst); return WH_ERR_NOMEM; }
    memcpy(inst->bytes, bytes, n);
    inst->n = n;

    /* 1. the surface, read by PARENA before the runtime sees a byte */
    d.data = inst->bytes;
    d.len = (int)n;
    ni = ws_valid_(d) ? ws_import_count(d) : -1;
    if (ni < 0) {
        say(msg, msglen, "not a well-formed wasm module (header or import section)");
        wh_destroy(inst);
        return WH_ERR_PARSE;
    }
    for (i = 0; i < ni; i++) {
        char mod[WH_NAME_MAX], field[WH_NAME_MAX], want[24];
        const wh_import *offer;
        int kind = ws_import_kind(d, i), nargs = 0, has_ret = 0;
        copy_name(d, ws_import_module_off(d, i), ws_import_module_len(d, i), mod);
        copy_name(d, ws_import_field_off(d, i), ws_import_field_len(d, i), field);
        if (kind != 0) {
            snprintf(text, sizeof text, "module imports %s.%s (a %s); this host offers functions only", mod, field,
                     kind == 1 ? "table" : kind == 2 ? "memory" : kind == 3 ? "global" : "malformed entry");
            say(msg, msglen, text);
            wh_destroy(inst);
            return kind < 0 ? WH_ERR_PARSE : WH_ERR_IMPORT_KIND;
        }
        offer = find_offer(imports, n_imports, mod, field);
        if (!offer) {
            snprintf(text, sizeof text, "module imports %s.%s, which this host does not offer", mod, field);
            say(msg, msglen, text);
            wh_destroy(inst);
            return WH_ERR_IMPORT_UNBOUND;
        }
        import_sig_text(d, i, want, sizeof want);
        if (strcmp(want, offer->sig) != 0 || !host_sig_ok(offer->sig, &nargs, &has_ret) || !offer->fn) {
            snprintf(text, sizeof text, "module imports %s.%s as %s; this host offers %s", mod, field,
                     want[0] ? want : "(unexpressible)", offer->sig);
            say(msg, msglen, text);
            wh_destroy(inst);
            return WH_ERR_IMPORT_SIG;
        }
    }

    /* 2. the runtime */
    inst->env = m3_NewEnvironment();
    if (!inst->env) { wh_destroy(inst); return WH_ERR_NOMEM; }
    inst->rt = m3_NewRuntime(inst->env, stack_bytes ? stack_bytes : 64u * 1024u, NULL);
    if (!inst->rt) { wh_destroy(inst); return WH_ERR_NOMEM; }
    (void)m3_SetResourceLimit(inst->rt, c_m3Limit_MemoryBytes, WH_MEMORY_CAP);
    r = m3_ParseModule(inst->env, &inst->mod, inst->bytes, (uint32_t)n);
    if (r) {
        snprintf(text, sizeof text, "runtime parse: %s", r);
        say(msg, msglen, text);
        inst->mod = NULL;
        wh_destroy(inst);
        return WH_ERR_PARSE;
    }
    r = m3_LoadModule(inst->rt, inst->mod); /* the runtime owns the module from here, success or not */
    if (r) {
        snprintf(text, sizeof text, "runtime load: %s", r);
        say(msg, msglen, text);
        wh_destroy(inst);
        return WH_ERR_LOAD;
    }

    /* 3. link exactly what was checked */
    for (i = 0; i < ni; i++) {
        char mod[WH_NAME_MAX], field[WH_NAME_MAX];
        const wh_import *offer;
        wh_bind *b;
        copy_name(d, ws_import_module_off(d, i), ws_import_module_len(d, i), mod);
        copy_name(d, ws_import_field_off(d, i), ws_import_field_len(d, i), field);
        offer = find_offer(imports, n_imports, mod, field);
        if (inst->nbinds >= WH_MAX_BINDS) {
            say(msg, msglen, "module has more distinct imports than this host binds");
            wh_destroy(inst);
            return WH_ERR_LOAD;
        }
        b = &inst->binds[inst->nbinds++];
        b->fn = offer->fn;
        b->user = offer->user;
        (void)host_sig_ok(offer->sig, &b->nargs, &b->has_ret);
        r = m3_LinkRawFunctionEx(inst->mod, mod, field, offer->sig, wh_trampoline, b);
        if (r) {
            snprintf(text, sizeof text, "runtime link %s.%s: %s", mod, field, r);
            say(msg, msglen, text);
            wh_destroy(inst);
            return WH_ERR_LOAD;
        }
    }
    r = m3_RunStart(inst->mod);
    if (r) {
        snprintf(text, sizeof text, "start function: %s", r);
        say(msg, msglen, text);
        wh_destroy(inst);
        return WH_ERR_TRAP;
    }
    *out = inst;
    say(msg, msglen, "ok");
    return WH_OK;
}

void wh_destroy(wh_instance *inst) {
    if (!inst) return;
    if (inst->rt) m3_FreeRuntime(inst->rt);          /* frees a loaded module too */
    else if (inst->mod) m3_FreeModule(inst->mod);
    if (inst->env) m3_FreeEnvironment(inst->env);
    free(inst->bytes);
    free(inst);
}

int wh_is_dead(const wh_instance *inst) { return !inst || inst->dead; }

/* ---- exports ------------------------------------------------------------------------------------ */

wh_status wh_lookup(wh_instance *inst, const char *name, const char *sig, wh_func *out, char *msg, size_t msglen) {
    IM3Function f = NULL;
    M3Result r;
    char have[24], text[200];
    if (!inst || !name || !sig || !out || strlen(sig) >= sizeof out->sig) return WH_ERR_ARGS;
    memset(out, 0, sizeof *out);
    if (inst->dead) { say(msg, msglen, "instance is dead (an earlier call trapped)"); return WH_ERR_TRAP; }
    r = m3_FindFunctionIn(&f, inst->mod, name);
    if (r || !f) {
        snprintf(text, sizeof text, "module does not export %s", name);
        say(msg, msglen, text);
        return WH_ERR_EXPORT_MISSING;
    }
    func_sig(f, have, sizeof have);
    if (strcmp(have, sig) != 0) {
        snprintf(text, sizeof text, "module exports %s as %s, expected %s", name, have, sig);
        say(msg, msglen, text);
        return WH_ERR_EXPORT_SIG;
    }
    out->inst = inst;
    out->impl = (void *)f;
    strcpy(out->sig, sig);
    out->nargs = (int)strlen(sig) - 3;
    return WH_OK;
}

wh_status wh_call(wh_func *fn, const wh_val *args, int nargs, wh_val *ret, char *msg, size_t msglen) {
    const void *argp[16];
    int32_t ri = 0;
    double rf = 0.0;
    const void *retp[1];
    M3Result r;
    int k;
    char text[256];
    if (!fn || !fn->inst || !fn->impl || nargs != fn->nargs || nargs > 16 || (nargs > 0 && !args)) return WH_ERR_ARGS;
    if (fn->inst->dead) { say(msg, msglen, "instance is dead (an earlier call trapped)"); return WH_ERR_TRAP; }
    for (k = 0; k < nargs; k++) {
        char want = fn->sig[2 + k];
        if (args[k].kind != want || (want != 'i' && want != 'F')) return WH_ERR_ARGS;
        argp[k] = (want == 'i') ? (const void *)&args[k].i32 : (const void *)&args[k].f64;
    }
    r = m3_Call((IM3Function)fn->impl, (uint32_t)nargs, argp);
    if (r) {
        M3ErrorInfo info;
        memset(&info, 0, sizeof info);
        m3_GetErrorInfo(fn->inst->rt, &info);
        snprintf(text, sizeof text, "%s: %s%s%s", m3_GetFunctionName((IM3Function)fn->impl), r,
                 (info.message && info.message[0]) ? " -- " : "", (info.message && info.message[0]) ? info.message : "");
        say(msg, msglen, text);
        fn->inst->dead = 1;
        return WH_ERR_TRAP;
    }
    if (fn->sig[0] != 'v') {
        retp[0] = (fn->sig[0] == 'i') ? (const void *)&ri : (const void *)&rf;
        if (fn->sig[0] != 'i' && fn->sig[0] != 'F') return WH_ERR_ARGS;
        r = m3_GetResults((IM3Function)fn->impl, 1, retp);
        if (r) { say(msg, msglen, r); fn->inst->dead = 1; return WH_ERR_TRAP; }
        if (ret) {
            ret->kind = fn->sig[0];
            ret->i32 = ri;
            ret->f64 = rf;
        }
    }
    return WH_OK;
}

wh_status wh_call_i32(wh_func *fn, const int32_t *args, int nargs, int32_t *ret, char *msg, size_t msglen) {
    wh_val v[16], out;
    wh_status s;
    int k;
    if (nargs < 0 || nargs > 16 || (nargs > 0 && !args)) return WH_ERR_ARGS;
    for (k = 0; k < nargs; k++) { v[k].kind = 'i'; v[k].i32 = args[k]; v[k].f64 = 0.0; }
    out.kind = 'v'; out.i32 = 0; out.f64 = 0.0;
    s = wh_call(fn, v, nargs, &out, msg, msglen);
    if (s == WH_OK && ret) *ret = out.i32;
    return s;
}

uint8_t *wh_memory(wh_instance *inst, uint32_t *size_out) {
    size_t sz = 0;
    uint8_t *p;
    if (size_out) *size_out = 0;
    if (!inst || !inst->mod) return NULL;
    p = m3_GetMemory(inst->mod, &sz, 0);
    if (size_out) *size_out = (sz > 0xffffffffu) ? 0xffffffffu : (uint32_t)sz;
    return p;
}
