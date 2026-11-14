/* phone_arcade.h -- the SHANKPHONE's PELLEGRINO app as a games app (founder, 2026-11-14: "lets build pellegrino on the
 * phone into a full app that can play the wasm games" / "bring in chromium to fill the gaps" / "parenafy it" / "mods
 * first everything").
 *
 * Open the phone (P), open PELLEGRINO: a list of games, then WEB BROWSER (the embedded browser as before). Up/Down and
 * Enter. A SHANKPIT level module (BRAWLPIT today) runs right in the phone on SHANKPIT's own wasm3 host -- its own
 * instance, separate from the BRAWLPIT the world can bring to the foreground with Shift+M. A web game (shankpit-460,
 * D2, REDGARDEN, Bedrock Racers) runs in headless Chromium, the stopgap (phone_chromium.h). Backspace leaves a game.
 *
 * Everything decided is PARENA: the games are a mod (PARENA/stdlib/shankpit/arcade_games.prn), the app's behaviour is
 * rules (arcade.prn), both generated into arcade_rules.c by scripts/gen_arcade_rules.sh. This file is the state and the
 * level-host calls; no GL (the lobby draws it), so packages/simulation/phone_arcade_test.c drives it headless with the
 * real BRAWLPIT module.
 */
#ifndef SHANKPIT_PHONE_ARCADE_H
#define SHANKPIT_PHONE_ARCADE_H

#include <stdio.h>
#include <string.h>

#include "../wasm_host/level_host.h"
#include "../reflux/reflux_runtime.h"
#include "level_manager_rules.h"

/* the mod and the rules (arcade_rules.c) */
int arcade_game_count(void);
char *arcade_game_name(int);
char *arcade_game_blurb(int);
int arcade_game_kind(int);
char *arcade_game_path(int);
int arcade_game_level(int);
char *arcade_game_asset(int, int);
int arcade_game_tile_w(int, int);
int arcade_game_tile_h(int, int);
int arcade_game_asset_stride(int, int);
int arcade_engine(int kind, int chromium_ok);
int arcade_rows(int games);
int arcade_row_is_browser(int row, int games);
int arcade_nav(int cursor, int delta, int rows);
int arcade_per_page(void);
int arcade_page_first(int cursor);
int arcade_key(int sym);
int arcade_fit_w(int fb_w, int fb_h, int box_w, int box_h);
int arcade_fit_h(int fb_w, int fb_h, int box_w, int box_h);
int arcade_shot_ms(void);
int arcade_chromium_w(void);
int arcade_chromium_h(void);
char *arcade_chromium_args(void);
char *arcade_wd_key_special(int);
int arcade_wd_key_char(int);

enum { ARCADE_LIST = 0, ARCADE_WASM = 1, ARCADE_BROWSER = 2, ARCADE_CHROMIUM = 3 };
enum { ARCADE_ENGINE_NONE = 0, ARCADE_ENGINE_WASM3 = 1, ARCADE_ENGINE_BROWSER = 2, ARCADE_ENGINE_CHROMIUM = 3 };
/* what a key on the list did */
enum { ARCADE_DID_NOTHING = 0, ARCADE_DID_START = 1, ARCADE_DID_BROWSER = 2, ARCADE_DID_CHROMIUM = 3, ARCADE_DID_CLOSE = 4 };

typedef struct {
    int screen;          /* ARCADE_* */
    int cursor;          /* the list row */
    int game;            /* the game running (screen WASM / CHROMIUM) */
    int chromium_ok;     /* set by the host: a Chromium + driver it can use */
    level_host lh;
    int lh_live;
    char msg[192];       /* the last thing to tell the player ("" none) */
} PhoneArcade;

static int arcade_accepts_none(int action_type) { (void)action_type; return 0; }

static inline void arcade_init(PhoneArcade *a) { memset(a, 0, sizeof *a); }

static inline const char *arcade_row_name(int row) {
    return arcade_row_is_browser(row, arcade_game_count()) ? "WEB BROWSER" : arcade_game_name(row);
}
static inline const char *arcade_row_blurb(int row) {
    return arcade_row_is_browser(row, arcade_game_count()) ? "Pellegrino's own browser" : arcade_game_blurb(row);
}

static inline void arcade_stop(PhoneArcade *a) {
    if (a->lh_live) { level_host_shutdown(&a->lh); a->lh_live = 0; }
    a->screen = ARCADE_LIST;
}

/* Run game i on the wasm3 host, in the phone. */
static inline int arcade_start_wasm(PhoneArcade *a, int i, int seed) {
    level_desc d;
    memset(&d, 0, sizeof d);
    d.name = arcade_game_name(i);
    d.module_path = arcade_game_path(i);
    d.level_id = arcade_game_level(i);
    d.accepts = d.level_id == REFLUX_LEVEL_BRAWLPIT ? reflux_action_is_brawlpit : arcade_accepts_none;
    for (int s = 0; s < LEVEL_HOST_MAX_ASSETS; s++) {
        const char *png = arcade_game_asset(i, s);
        if (!png || !png[0]) break;
        d.assets[s].png_path = png;
        d.assets[s].tile_w = arcade_game_tile_w(i, s);
        d.assets[s].tile_h = arcade_game_tile_h(i, s);
        d.assets[s].stride = arcade_game_asset_stride(i, s);
        d.n_assets = s + 1;
    }
    if (a->lh_live) level_host_shutdown(&a->lh);
    level_host_init(&a->lh, &d, lm_st_loading(), seed);
    a->lh_live = 1;
    a->game = i;
    a->screen = ARCADE_WASM;
    a->msg[0] = 0;
    return 1;
}

/* A key on the list (SDL keycodes: Up 1073741906, Down 1073741905, Enter 13, Backspace 8). */
static inline int arcade_list_key(PhoneArcade *a, int sym, int seed) {
    int games = arcade_game_count(), rows = arcade_rows(games);
    if (sym == 1073741906) { a->cursor = arcade_nav(a->cursor, -1, rows); return ARCADE_DID_NOTHING; }
    if (sym == 1073741905) { a->cursor = arcade_nav(a->cursor, 1, rows); return ARCADE_DID_NOTHING; }
    if (sym == 8) return ARCADE_DID_CLOSE;
    if (sym != 13) return ARCADE_DID_NOTHING;
    if (arcade_row_is_browser(a->cursor, games)) { a->screen = ARCADE_BROWSER; return ARCADE_DID_BROWSER; }
    switch (arcade_engine(arcade_game_kind(a->cursor), a->chromium_ok)) {
    case ARCADE_ENGINE_WASM3:
        arcade_start_wasm(a, a->cursor, seed);
        return ARCADE_DID_START;
    case ARCADE_ENGINE_CHROMIUM:
        a->game = a->cursor;
        a->screen = ARCADE_CHROMIUM;
        a->msg[0] = 0;
        return ARCADE_DID_CHROMIUM;
    default:
        snprintf(a->msg, sizeof a->msg, "%s is a web game: it runs in Chromium, which this phone cannot reach",
                 arcade_game_name(a->cursor));
        return ARCADE_DID_NOTHING;
    }
}

/* A key while a wasm game runs: Backspace leaves; everything else is the game's. Returns 1 when it left. */
static inline int arcade_game_key(PhoneArcade *a, int sym, int down) {
    if (arcade_key(sym) == 2) {
        if (down) { arcade_stop(a); return 1; }
        return 0;
    }
    if (a->screen == ARCADE_WASM && a->lh_live) level_host_key(&a->lh, sym, down);
    return 0;
}

/* One display frame of dt_us for the running wasm game; a load failure or trap comes back to the list, said. */
static inline void arcade_frame(PhoneArcade *a, int dt_us) {
    if (a->screen != ARCADE_WASM || !a->lh_live) return;
    level_host_frame(&a->lh, dt_us);
    int st = level_host_state(&a->lh);
    if (st == lm_st_none() && level_host_message(&a->lh)[0]) {
        snprintf(a->msg, sizeof a->msg, "%s", level_host_message(&a->lh));
        arcade_stop(a);
    }
}

/* The running wasm game's picture (RGBA, row 0 at the top), or NULL before its first frame. */
static inline const uint8_t *arcade_fb(PhoneArcade *a, int *w, int *h, int *dirty) {
    if (a->screen != ARCADE_WASM || !a->lh_live) return NULL;
    return level_host_fb(&a->lh, w, h, dirty);
}

#endif
