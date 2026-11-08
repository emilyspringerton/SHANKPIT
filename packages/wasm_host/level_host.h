/* packages/wasm_host/level_host.h -- one level's life inside the SHANKPIT world, with no SDL and no GL
 * (T0 step 4, docs/T0_LEVEL_WASM_CONTRACT.md section 2). The lobby's level shell (apps/lobby/src/level_shell.c)
 * turns SDL events into these calls and draws the framebuffer; everything between is here, so the whole
 * lifecycle -- load, the Shift+M toggle and pause, close, reload, traps, the fixed step -- runs headless in
 * level_host_test.c against the committed BRAWLPIT module.
 *
 * Founder real-time 2026-11-08: "Boot the SHANKPIT world plus BRAWLPIT as WASM, BRAWLPIT in the foreground.
 * Shift+M toggles to the world view and pauses BRAWLPIT." Every decision is the level manager's (PARENA
 * stdlib/shankpit/level_manager_rules.prn, generated level_manager_rules.c): this file holds the state the
 * rules ask about and does what they answer (INV-1.2). The level talks to the world only through REFLUX
 * (level_abi.h); the lifecycle rows it hears about are dispatched here:
 *   LOADED (id, api, generation) and LOAD_FAILED (id, REFLUX_LEVEL_FAIL_* stage, wh_status) by the loader,
 *   SHOWN / HIDDEN / CLOSED (id, reason) for the transitions lm_announce names.
 * Strict C99, one instance per level_host, no allocation after load except the decoder's scratch arena. */
#ifndef SHANKPIT_LEVEL_HOST_H
#define SHANKPIT_LEVEL_HOST_H

#include <stddef.h>
#include <stdint.h>

#include "level_abi.h"

#define LEVEL_HOST_MAX_ASSETS 4

/* An asset slot: a PNG, cut into row-major tiles of tile_w x tile_h RGBA, tile k written at
 * level_asset_addr(slot) + k * stride. tile_w = tile_h = 0 means the whole image is one tile. */
typedef struct {
    const char *png_path;
    int tile_w, tile_h;
    int stride;
} level_asset;

typedef struct {
    const char *name;                  /* for messages, e.g. "BRAWLPIT" */
    const char *module_path;           /* e.g. "assets/levels/brawlpit/brawlpit_level.wasm" */
    int level_id;                      /* lifecycle payload a, e.g. REFLUX_LEVEL_BRAWLPIT */
    int (*accepts)(int action_type);   /* the only types its dispatch import passes, e.g. reflux_action_is_brawlpit */
    int n_assets;
    level_asset assets[LEVEL_HOST_MAX_ASSETS]; /* index = asset slot */
} level_desc;

typedef struct {
    level_desc desc;
    int state;                 /* lm_st_* */
    int generation;            /* successful loads so far (LOADED payload c) */
    int acc_us;                /* the fixed-step accumulator */
    int32_t cursor;            /* the level's REFLUX cursor, held here (INV-3R.3) */
    int fb_dirty;              /* 1 = rendered since the shell last took the framebuffer */
    int ticks;                 /* level_tick calls since load (diagnostics, tests) */
    int renders;               /* level_render calls since load */
    int assets_loaded;         /* asset slots written at the last load */
    int seed;
    int32_t fb_addr, fb_w, fb_h; /* validated at load: inside memory */
    int pending_trap;          /* a call trapped mid-transition; close once the transition is made */
    wh_instance *inst;
    level_exports x;
    level_reflux_bridge bridge;
    int rejected_total;        /* refused dispatches, across loads */
    char msg[256];             /* the last load failure or trap, "" if none */
} level_host;

/* state = boot_state (lm_st_loading() boots the level into the foreground, lm_st_none() waits for Shift+M) */
void level_host_init(level_host *h, const level_desc *d, int boot_state, int seed);

/* A key-down the shell saw first: if it is a chord (lm_chord) that changes the state, the transition is made
 * and 1 is returned (the shell consumes the key). 0 = not a chord here: pass the key on. */
int level_host_chord(level_host *h, int sdl_sym, int shift, int ctrl, int alt, int gui, int mac, int text_focus,
                     int repeat);

/* An outside command (lm_cmd_*: e.g. OVERLAY_CLOSED from the view-source overlay). 1 = the state changed. */
int level_host_command(level_host *h, int cmd);

/* One display frame of dt_us: LOADING loads; a loaded level polls REFLUX; FG runs the steps it is owed
 * (lm_steps) and renders once if it ran any. */
void level_host_frame(level_host *h, int dt_us);

/* Input, forwarded only while the level owns it (lm_owns_input). Keys are SDL keycodes, mapped by
 * lm_dom_key; pointer coordinates are framebuffer pixels. */
void level_host_key(level_host *h, int sdl_sym, int down);
void level_host_pointer(level_host *h, int fb_x, int fb_y, int buttons);
void level_host_blur(level_host *h);

int level_host_state(const level_host *h);
int level_host_owns_input(const level_host *h);
int level_host_banner(const level_host *h);     /* lm_banner */
const char *level_host_message(const level_host *h);
int level_host_rejected(const level_host *h);   /* dispatches refused by the bridge, all loads */

/* The framebuffer (RGBA, w x h), NULL when no level is loaded. *dirty is set to the dirty flag, which
 * this call clears. */
const uint8_t *level_host_fb(level_host *h, int *w, int *hgt, int *dirty);

/* Swap a newly compiled module in for the running one (T0 step 6; the shell calls it on an entry lm_swap_on
 * names). The new module is loaded into a fresh instance FIRST; only if it fully loads (bind, api, enter, assets,
 * first render) is the old one exited and dropped, LEVEL_SWAPPED (id, generation) dispatched, and module_path
 * moved to the new file so a later reload uses it. Otherwise the old instance keeps running untouched, nothing is
 * dispatched (the level is still loaded), and h->msg says why. Only while a level is loaded. 1 = swapped. */
int level_host_swap(level_host *h, const char *module_path);

/* The module's memory, for tests and diagnostics; NULL when none. */
uint8_t *level_host_memory(level_host *h, uint32_t *size);

void level_host_shutdown(level_host *h);

#endif /* SHANKPIT_LEVEL_HOST_H */
