/* packages/wasm_host/level_host.c -- see level_host.h. Plumbing: the decisions are lm_* (PARENA). */
#include "level_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../reflux/reflux_runtime.h"
#include "../simulation/level_manager_rules.h"
#include "../world/png_decode.h"

#define LH_FB_MAX 4096

static void say(level_host *h, const char *what, const char *detail) {
    snprintf(h->msg, sizeof h->msg, "%s: %s%s%s", h->desc.name ? h->desc.name : "level", what,
             (detail && detail[0]) ? ": " : "", (detail && detail[0]) ? detail : "");
}

void level_host_init(level_host *h, const level_desc *d, int boot_state, int seed) {
    memset(h, 0, sizeof *h);
    h->desc = *d;
    if (h->desc.n_assets < 0) h->desc.n_assets = 0;
    if (h->desc.n_assets > LEVEL_HOST_MAX_ASSETS) h->desc.n_assets = LEVEL_HOST_MAX_ASSETS;
    h->state = boot_state;
    h->seed = seed;
}

/* ---- calls into the module ------------------------------------------------------------------- */

static wh_status lh_call(level_host *h, wh_func *f, const int32_t *args, int nargs, int32_t *ret) {
    char m[256];
    wh_status s;
    m[0] = 0;
    s = wh_call_i32(f, args, nargs, ret, m, sizeof m);
    if (s != WH_OK) say(h, s == WH_ERR_TRAP ? "trapped" : "call refused", m);
    return s;
}

static void unload(level_host *h) {
    if (!h->inst) return;
    if (!wh_is_dead(h->inst)) {
        char m[64];
        wh_call_i32(&h->x.exit, NULL, 0, NULL, m, sizeof m);
    }
    h->rejected_total += h->bridge.rejected;
    wh_destroy(h->inst);
    h->inst = NULL;
    memset(&h->x, 0, sizeof h->x);
    h->fb_addr = h->fb_w = h->fb_h = 0;
    h->fb_dirty = 0;
}

/* make the transition lm_next names; 1 = the state changed */
static int apply(level_host *h, int cmd) {
    int old = h->state, now = lm_next(old, cmd), act;
    if (now == old) return 0;
    if (old == lm_st_fg() && h->inst && !wh_is_dead(h->inst) && lh_call(h, &h->x.blur, NULL, 0, NULL) == WH_ERR_TRAP)
        h->pending_trap = 1;
    if (lm_loaded(old) && !lm_loaded(now)) unload(h);
    h->state = now;
    act = lm_announce(old, now);
    if (act) reflux_host_dispatch(act, h->desc.level_id, act == REFLUX_ACTION_LEVEL_CLOSED ? lm_close_reason(cmd) : 0, 0);
    if (now == lm_st_fg()) h->acc_us = 0;
    if (h->pending_trap && lm_loaded(h->state)) {
        h->pending_trap = 0;
        apply(h, lm_cmd_trap());
    }
    h->pending_trap = 0;
    return 1;
}

/* a call trapped (or was refused) while the level was running: it is gone */
static void trapped(level_host *h) {
    if (lm_loaded(h->state)) apply(h, lm_cmd_trap());
}

/* ---- loading ----------------------------------------------------------------------------------- */

static unsigned char *read_all(const char *path, size_t *n) {
    FILE *f = path ? fopen(path, "rb") : NULL;
    long len;
    unsigned char *b;
    *n = 0;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    b = (unsigned char *)malloc((size_t)len);
    if (b && fread(b, 1, (size_t)len, f) != (size_t)len) { free(b); b = NULL; }
    fclose(f);
    if (b) *n = (size_t)len;
    return b;
}

/* decode one asset PNG and write its tiles into the module; 1 = written */
static int load_asset(level_host *h, int slot, uint8_t *mem, uint32_t mem_size) {
    const level_asset *a = &h->desc.assets[slot];
    size_t n = 0;
    unsigned char *png = read_all(a->png_path, &n);
    Arena ar;
    Bytes in;
    PngImage img;
    int32_t base = 0, sarg[2];
    int tw, th, ntx, nty, k, r, ok = 0;
    if (!png) return 0;
    arena_init(&ar);
    in = bytes_alloc_impl(&ar, (int)n);
    memcpy(in.data, png, n);
    free(png);
    img = png_decode(in, &ar);
    if (img.ok && img.width > 0 && img.height > 0) {
        tw = a->tile_w > 0 ? a->tile_w : img.width;
        th = a->tile_h > 0 ? a->tile_h : img.height;
        ntx = img.width / tw;
        nty = img.height / th;
        sarg[0] = slot;
        if (img.width % tw == 0 && img.height % th == 0 && lh_call(h, &h->x.asset_addr, sarg, 1, &base) == WH_OK &&
            base > 0) {
            uint64_t stride = (uint64_t)(a->stride > 0 ? a->stride : tw * th * 4);
            uint64_t end = (uint64_t)base + (uint64_t)(ntx * nty - 1) * stride + (uint64_t)tw * (uint64_t)th * 4u;
            if (stride >= (uint64_t)tw * (uint64_t)th * 4u && end <= mem_size) {
                for (k = 0; k < ntx * nty; k++) {
                    int tx = k % ntx, ty = k / ntx;
                    for (r = 0; r < th; r++)
                        memcpy(mem + (uint64_t)base + (uint64_t)k * stride + (uint64_t)r * (uint64_t)tw * 4u,
                               img.pixels.data + ((size_t)(ty * th + r) * (size_t)img.width + (size_t)(tx * tw)) * 4u,
                               (size_t)tw * 4u);
                }
                sarg[1] = 1;
                ok = lh_call(h, &h->x.asset_set, sarg, 2, NULL) == WH_OK;
            }
        }
    }
    arena_free_all(&ar);
    return ok;
}

/* instantiate, bind, check the api, enter, write the assets, draw once. 0 = refused: *stage and *status
 * are LEVEL_LOAD_FAILED's payload (b, c) and h->msg says why. */
static int load(level_host *h, int *stage, int *status) {
    wh_import imp[LEVEL_ABI_N_IMPORTS];
    size_t n = 0;
    unsigned char *bytes;
    char m[256];
    wh_status s;
    int32_t api = 0, rc = 0, seed;
    uint8_t *mem;
    uint32_t mem_size = 0;
    int i;

    *stage = REFLUX_LEVEL_FAIL_READ;
    *status = 0;
    bytes = read_all(h->desc.module_path, &n);
    if (!bytes) { say(h, "cannot read the module", h->desc.module_path); return 0; }

    *stage = REFLUX_LEVEL_FAIL_BIND;
    level_reflux_bridge_init(&h->bridge, h->desc.accepts);
    m[0] = 0;
    s = wh_instantiate(bytes, n, imp, level_reflux_imports(&h->bridge, imp), 0, &h->inst, m, sizeof m);
    free(bytes);
    if (s != WH_OK) { *status = (int)s; say(h, "refused at load", m); h->inst = NULL; return 0; }
    s = level_exports_bind(h->inst, &h->x, m, sizeof m);
    if (s != WH_OK) { *status = (int)s; say(h, "refused at load", m); goto fail; }
    mem = wh_memory(h->inst, &mem_size);
    if (!mem) { *status = (int)WH_ERR_LOAD; say(h, "refused at load", "it exports no memory"); goto fail; }
    if ((s = lh_call(h, &h->x.fb_addr, NULL, 0, &h->fb_addr)) != WH_OK ||
        (s = lh_call(h, &h->x.fb_w, NULL, 0, &h->fb_w)) != WH_OK ||
        (s = lh_call(h, &h->x.fb_h, NULL, 0, &h->fb_h)) != WH_OK) { *status = (int)s; goto fail; }
    if (h->fb_w < 1 || h->fb_w > LH_FB_MAX || h->fb_h < 1 || h->fb_h > LH_FB_MAX || h->fb_addr < 0 ||
        (uint64_t)h->fb_addr + (uint64_t)h->fb_w * (uint64_t)h->fb_h * 4u > mem_size) {
        say(h, "refused at load", "its framebuffer is not inside its memory");
        goto fail;
    }

    *stage = REFLUX_LEVEL_FAIL_API;
    if ((s = lh_call(h, &h->x.api, NULL, 0, &api)) != WH_OK) { *status = (int)s; goto fail; }
    if (api != LEVEL_ABI_API) {
        snprintf(m, sizeof m, "level_api() is %d, this host speaks %d", (int)api, LEVEL_ABI_API);
        say(h, "refused at load", m);
        goto fail;
    }

    *stage = REFLUX_LEVEL_FAIL_ENTER;
    seed = h->seed;
    if ((s = lh_call(h, &h->x.seed, &seed, 1, NULL)) != WH_OK || (s = lh_call(h, &h->x.enter, NULL, 0, &rc)) != WH_OK) {
        *status = (int)s;
        goto fail;
    }
    if (rc != 0) {
        snprintf(m, sizeof m, "level_enter() returned %d", (int)rc);
        say(h, "refused at load", m);
        goto fail;
    }

    h->assets_loaded = 0;
    for (i = 0; i < h->desc.n_assets; i++)
        if (h->desc.assets[i].png_path) h->assets_loaded += load_asset(h, i, mem, mem_size);
    if (wh_is_dead(h->inst) || lh_call(h, &h->x.render, NULL, 0, NULL) != WH_OK) {
        *status = (int)WH_ERR_TRAP;
        goto fail;
    }
    h->generation++;
    h->ticks = 0;
    h->renders = 1;
    h->fb_dirty = 1;
    h->acc_us = 0;
    h->cursor = reflux_host_total_dispatched();
    h->msg[0] = 0;
    return 1;

fail:
    wh_destroy(h->inst);
    h->inst = NULL;
    memset(&h->x, 0, sizeof h->x);
    h->fb_addr = h->fb_w = h->fb_h = 0;
    return 0;
}

int level_host_swap(level_host *h, const char *module_path) {
    wh_instance *old_inst;
    level_exports old_x;
    level_reflux_bridge old_bridge;
    int32_t old_fb_addr, old_fb_w, old_fb_h, old_cursor;
    int old_ticks, old_renders, old_assets, stage = 0, status = 0;
    const char *old_path;
    char m[64];
    if (!h->inst || !lm_loaded(h->state) || !module_path) return 0;
    /* the old instance is parked, not called, while the new one loads into the same fields (its imports point at
     * h->bridge too, so the bridge is saved and put back if the new module is refused) */
    old_inst = h->inst;
    old_x = h->x;
    old_bridge = h->bridge;
    old_fb_addr = h->fb_addr;
    old_fb_w = h->fb_w;
    old_fb_h = h->fb_h;
    old_cursor = h->cursor;
    old_ticks = h->ticks;
    old_renders = h->renders;
    old_assets = h->assets_loaded;
    old_path = h->desc.module_path;
    h->inst = NULL;
    h->desc.module_path = module_path;
    if (!load(h, &stage, &status)) {
        h->inst = old_inst;
        h->x = old_x;
        h->bridge = old_bridge;
        h->fb_addr = old_fb_addr;
        h->fb_w = old_fb_w;
        h->fb_h = old_fb_h;
        h->cursor = old_cursor;
        h->ticks = old_ticks;
        h->renders = old_renders;
        h->assets_loaded = old_assets;
        h->desc.module_path = old_path;
        return 0; /* load() counts generations only on success: nothing changed, h->msg says why */
    }
    if (!wh_is_dead(old_inst)) wh_call_i32(&old_x.exit, NULL, 0, NULL, m, sizeof m);
    h->rejected_total += old_bridge.rejected;
    wh_destroy(old_inst);
    reflux_host_dispatch(REFLUX_ACTION_LEVEL_SWAPPED, h->desc.level_id, h->generation, 0);
    return 1;
}

/* ---- the shell's calls ------------------------------------------------------------------------- */

int level_host_chord(level_host *h, int sdl_sym, int shift, int ctrl, int alt, int gui, int mac, int text_focus,
                     int repeat) {
    int cmd = lm_chord(sdl_sym, shift, ctrl, alt, gui, mac, text_focus, repeat);
    return cmd ? apply(h, cmd) : 0;
}

int level_host_command(level_host *h, int cmd) { return apply(h, cmd); }

void level_host_frame(level_host *h, int dt_us) {
    int32_t arg;
    int n, i;
    if (h->state == lm_st_loading()) {
        int stage = 0, status = 0;
        if (load(h, &stage, &status)) {
            reflux_host_dispatch(REFLUX_ACTION_LEVEL_LOADED, h->desc.level_id, LEVEL_ABI_API, h->generation);
            apply(h, lm_cmd_load_ok());
        } else {
            reflux_host_dispatch(REFLUX_ACTION_LEVEL_LOAD_FAILED, h->desc.level_id, stage, status);
            apply(h, lm_cmd_load_fail());
        }
        return;
    }
    if (!lm_loaded(h->state) || !h->inst) return;
    arg = h->cursor;
    if (lh_call(h, &h->x.poll, &arg, 1, &h->cursor) != WH_OK) { trapped(h); return; }
    if (!lm_ticks(h->state)) return;
    h->acc_us = lm_acc_add(h->acc_us, dt_us);
    n = lm_steps(h->acc_us);
    h->acc_us = lm_acc_left(h->acc_us);
    for (i = 0; i < n; i++) {
        if (lh_call(h, &h->x.tick, NULL, 0, NULL) != WH_OK) { trapped(h); return; }
        h->ticks++;
    }
    if (n > 0) {
        if (lh_call(h, &h->x.render, NULL, 0, NULL) != WH_OK) { trapped(h); return; }
        h->renders++;
        h->fb_dirty = 1;
    }
}

void level_host_key(level_host *h, int sdl_sym, int down) {
    int32_t a[2];
    if (!h->inst || !lm_owns_input(h->state)) return;
    a[0] = lm_dom_key(sdl_sym);
    a[1] = down ? 1 : 0;
    if (a[0] > 0 && lh_call(h, &h->x.key, a, 2, NULL) != WH_OK) trapped(h);
}

void level_host_pointer(level_host *h, int fb_x, int fb_y, int buttons) {
    int32_t a[3];
    if (!h->inst || !lm_owns_input(h->state)) return;
    a[0] = fb_x;
    a[1] = fb_y;
    a[2] = buttons;
    if (lh_call(h, &h->x.pointer, a, 3, NULL) != WH_OK) trapped(h);
}

void level_host_blur(level_host *h) {
    if (h->inst && lm_loaded(h->state) && lh_call(h, &h->x.blur, NULL, 0, NULL) != WH_OK) trapped(h);
}

int level_host_state(const level_host *h) { return h->state; }
int level_host_owns_input(const level_host *h) { return lm_owns_input(h->state); }
int level_host_banner(const level_host *h) { return lm_banner(h->state); }
const char *level_host_message(const level_host *h) { return h->msg; }
int level_host_rejected(const level_host *h) { return h->rejected_total + (h->inst ? h->bridge.rejected : 0); }

const uint8_t *level_host_fb(level_host *h, int *w, int *hgt, int *dirty) {
    uint32_t size = 0;
    uint8_t *mem = h->inst ? wh_memory(h->inst, &size) : NULL;
    if (dirty) *dirty = h->fb_dirty;
    h->fb_dirty = 0;
    if (!mem) return NULL;
    if (w) *w = h->fb_w;
    if (hgt) *hgt = h->fb_h;
    return mem + h->fb_addr;
}

uint8_t *level_host_memory(level_host *h, uint32_t *size) {
    if (size) *size = 0;
    return h->inst ? wh_memory(h->inst, size) : NULL;
}

void level_host_shutdown(level_host *h) {
    unload(h);
    h->state = lm_st_none();
}
