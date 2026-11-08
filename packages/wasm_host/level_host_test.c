/* packages/wasm_host/level_host_test.c -- `make test-level-host`. One level's whole life in the world, headless
 * (T0 step 4, docs/T0_LEVEL_WASM_CONTRACT.md section 2): what the lobby's level shell does with SDL events,
 * driven through level_host.h exactly as the shell drives it, against the COMMITTED BRAWLPIT module and art.
 *   boot: LOADING -> one frame -> FG, LEVEL_LOADED (1, api 1, generation 1) then LEVEL_SHOWN; WOTAN's portrait
 *     tiles and duck atlas are in the module's asset slots byte for byte;
 *   the fixed step: a 5 ms frame runs no step and draws nothing, a 50 ms one runs the 2-step cap;
 *   keys reach BRAWLPIT as DOM keyCodes: '1', Enter, Enter starts a match (BP_MATCH_STARTED in the log);
 *   Shift+M: FG -> BG, LEVEL_HIDDEN, BRAWLPIT's own pause menu opens, no steps while suspended, keys go nowhere;
 *   Shift+M again: SHOWN, still paused (the player resumes, not the toggle); Shift+X in FG is passed through;
 *   with text focus no chord fires; Shift+X in BG closes (LEVEL_CLOSED reason 0) and frees the instance;
 *   Shift+M from nothing reloads (generation 2);
 *   failures: an unreadable path (READ), garbage bytes (BIND / WH_ERR_PARSE), no memory, a framebuffer outside
 *   memory, level_api 2 (API), level_enter 7 (ENTER, status 0), level_enter trapping (ENTER, WH_ERR_TRAP) --
 *   each one LEVEL_LOAD_FAILED with that payload, state NONE, a message, nothing left loaded;
 *   a trap in level_tick closes the level (LEVEL_CLOSED reason 1) and the world goes on;
 *   a lifecycle row dispatched by a level is refused and counted, its own family's row is logged. */
#define _POSIX_C_SOURCE 199309L
#include "level_host.h"
#include "level_host_test_fixtures.h"
#include "../reflux/reflux_runtime.h"
#include "../simulation/level_manager_rules.h"
#include "../world/png_decode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures = 0, checks = 0;
#define CHECK(c, m) do { checks++; if (!(c)) { printf("FAIL: %s\n", m); failures++; } else printf("PASS: %s\n", m); } while (0)

#define G_BASE 135168u
#define G_PHASE 8
#define G_PAUSED 26
#define G_LV_TICKS 201
#define SC(n) (0x40000000 + (n))
#define BRAWLPIT_WASM "assets/levels/brawlpit/brawlpit_level.wasm"
#define SPRITES_PNG "assets/levels/brawlpit/sprites.png"
#define DUCK_PNG "assets/levels/brawlpit/duck.png"

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

static level_desc brawlpit_desc(const char *module) {
    level_desc d;
    memset(&d, 0, sizeof d);
    d.name = "BRAWLPIT";
    d.module_path = module;
    d.level_id = REFLUX_LEVEL_BRAWLPIT;
    d.accepts = reflux_action_is_brawlpit;
    d.n_assets = 2;
    d.assets[0].png_path = SPRITES_PNG;   /* 512x256 -> 8 portrait tiles of 128x128, 65536 B apart */
    d.assets[0].tile_w = 128;
    d.assets[0].tile_h = 128;
    d.assets[0].stride = 65536;
    d.assets[1].png_path = DUCK_PNG;      /* 640x340 atlas, one tile */
    return d;
}

static int32_t gw(level_host *h, int k) {
    uint32_t size = 0;
    uint8_t *m = level_host_memory(h, &size);
    int32_t v = -999;
    if (m && G_BASE + 4u * (uint32_t)k + 4u <= size) memcpy(&v, m + G_BASE + 4u * (uint32_t)k, sizeof v);
    return v;
}

/* the most recent row in the log, and whether its payload is (a, b, c) */
static int last_is(int type, int a, int b, int c) {
    int i = reflux_host_log_size() - 1;
    return i >= 0 && reflux_host_action_type_at(i) == type && reflux_host_action_a_at(i) == a &&
           reflux_host_action_b_at(i) == b && reflux_host_action_c_at(i) == c;
}

static int count_rows(int type) {
    int i, n = 0;
    for (i = 0; i < reflux_host_log_size(); i++)
        if (reflux_host_action_type_at(i) == type) n++;
    return n;
}

static void press(level_host *h, int sym) {
    level_host_key(h, sym, 1);
    level_host_frame(h, 16667);
    level_host_key(h, sym, 0);
    level_host_frame(h, 16667);
}

static int shift_m(level_host *h) { return level_host_chord(h, 'm', 1, 0, 0, 0, 0, 0, 0); }
static int shift_x(level_host *h) { return level_host_chord(h, 'x', 1, 0, 0, 0, 0, 0, 0); }

/* the module's asset slot k bytes equal tile k of the decoded PNG */
static int tile_matches(level_host *h, const char *png, int tile, int tw, int th, int stride, int32_t base) {
    FILE *f = fopen(png, "rb");
    long n;
    unsigned char *buf;
    Arena ar;
    Bytes in;
    PngImage img;
    uint32_t size = 0;
    uint8_t *mem = level_host_memory(h, &size);
    int ok = 0, r, ntx;
    if (!f || !mem) { if (f) fclose(f); return 0; }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (unsigned char *)malloc((size_t)n);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return 0; }
    fclose(f);
    arena_init(&ar);
    in = bytes_alloc_impl(&ar, (int)n);
    memcpy(in.data, buf, (size_t)n);
    free(buf);
    img = png_decode(in, &ar);
    if (img.ok) {
        if (tw == 0) { tw = img.width; th = img.height; }
        ntx = img.width / tw;
        ok = 1;
        for (r = 0; r < th && ok; r++) {
            const uint8_t *want = img.pixels.data + ((size_t)((tile / ntx) * th + r) * (size_t)img.width + (size_t)((tile % ntx) * tw)) * 4u;
            const uint8_t *have = mem + (uint32_t)base + (uint32_t)tile * (uint32_t)stride + (uint32_t)r * (uint32_t)tw * 4u;
            if (memcmp(want, have, (size_t)tw * 4u) != 0) ok = 0;
        }
    }
    arena_free_all(&ar);
    return ok;
}

static void test_brawlpit_life(void) {
    level_host h;
    level_desc d = brawlpit_desc(BRAWLPIT_WASM);
    const uint8_t *fb;
    int w = 0, hh = 0, dirty = 0, before, i;
    double t0, load_ms;

    reflux_host_reset();
    level_host_init(&h, &d, lm_st_loading(), 4242);
    CHECK(level_host_state(&h) == lm_st_loading() && level_host_banner(&h) == 0, "boot state is LOADING");
    t0 = now_ms();
    level_host_frame(&h, 16667);
    load_ms = now_ms() - t0;
    CHECK(level_host_state(&h) == lm_st_fg(), "one frame later BRAWLPIT is loaded and in the foreground");
    if (level_host_state(&h) != lm_st_fg()) { printf("  %s\n", level_host_message(&h)); return; }
    CHECK(reflux_host_log_size() == 2 && reflux_host_action_type_at(0) == REFLUX_ACTION_LEVEL_LOADED &&
              reflux_host_action_a_at(0) == 1 && reflux_host_action_b_at(0) == 1 && reflux_host_action_c_at(0) == 1 &&
              last_is(REFLUX_ACTION_LEVEL_SHOWN, 1, 0, 0),
          "the log reads LEVEL_LOADED (1, api 1, generation 1) then LEVEL_SHOWN (1)");
    printf("  load (instantiate + enter + decode 2 PNGs + first render): %.1f ms\n", load_ms);
    fb = level_host_fb(&h, &w, &hh, &dirty);
    CHECK(fb != NULL && w == 960 && hh == 540 && dirty == 1, "a 960x540 framebuffer is ready to upload (dirty)");
    fb = level_host_fb(&h, &w, &hh, &dirty);
    CHECK(dirty == 0, "taking it clears the dirty flag");
    CHECK(h.assets_loaded == 2, "both asset slots were written");
    {
        int32_t a0 = 0, a1 = 0, arg = 0;
        char m[64];
        wh_call_i32(&h.x.asset_addr, &arg, 1, &a0, m, sizeof m);
        arg = 1;
        wh_call_i32(&h.x.asset_addr, &arg, 1, &a1, m, sizeof m);
        CHECK(tile_matches(&h, SPRITES_PNG, 0, 128, 128, 65536, a0) && tile_matches(&h, SPRITES_PNG, 7, 128, 128, 65536, a0),
              "portrait tiles 0 and 7 are sprites.png's (4x2 row-major cut, 65536 B apart)");
        CHECK(tile_matches(&h, DUCK_PNG, 0, 0, 0, 0, a1), "the duck atlas is duck.png byte for byte");
    }

    /* the fixed step (the accumulator starts at 0 on entering the foreground) */
    before = gw(&h, G_LV_TICKS);
    level_host_frame(&h, 5000);
    level_host_fb(&h, &w, &hh, &dirty);
    CHECK(gw(&h, G_LV_TICKS) == before && dirty == 0 && h.renders == 1, "a 5 ms frame runs no step and draws nothing");
    level_host_frame(&h, 50000);
    CHECK(gw(&h, G_LV_TICKS) - before == 2 && h.renders == 2,
          "the next, 50 ms, frame owes 3 steps: it runs the 2-step cap and draws once");

    /* keys, as SDL keycodes, reach BRAWLPIT as DOM keyCodes */
    press(&h, '1');
    CHECK(gw(&h, G_PHASE) == 1, "'1' (SDL 49) reaches BRAWLPIT: fighter select");
    press(&h, 13);
    press(&h, 13);
    CHECK(gw(&h, G_PHASE) == 2 && count_rows(REFLUX_ACTION_BP_MATCH_STARTED) == 1,
          "Enter, Enter: the match starts and BP_MATCH_STARTED is in the log");
    for (i = 0; i < 30; i++) level_host_frame(&h, 16667);

    /* Shift+M: to the world */
    CHECK(shift_m(&h) == 1 && level_host_state(&h) == lm_st_bg() && last_is(REFLUX_ACTION_LEVEL_HIDDEN, 1, 0, 0),
          "Shift+M: FG -> BG, LEVEL_HIDDEN (1)");
    CHECK(!level_host_owns_input(&h) && level_host_banner(&h) == 1, "the world owns input; banner 'resume / close'");
    level_host_frame(&h, 16667);
    CHECK(gw(&h, G_PAUSED) == 1, "the same frame's poll opened BRAWLPIT's own pause menu");
    before = gw(&h, G_LV_TICKS);
    for (i = 0; i < 20; i++) level_host_frame(&h, 16667);
    level_host_key(&h, 'p', 1);
    level_host_frame(&h, 16667);
    CHECK(gw(&h, G_LV_TICKS) == before && gw(&h, G_PAUSED) == 1, "suspended: no steps, and keys go nowhere");
    CHECK(level_host_chord(&h, 'm', 1, 0, 0, 0, 0, 1, 0) == 0 && level_host_state(&h) == lm_st_bg(),
          "with text focus Shift+M does nothing");
    CHECK(shift_m(&h) == 1 && level_host_state(&h) == lm_st_fg() && last_is(REFLUX_ACTION_LEVEL_SHOWN, 1, 0, 0),
          "Shift+M again: FG, LEVEL_SHOWN");
    level_host_frame(&h, 16667);
    CHECK(gw(&h, G_PAUSED) == 1 && gw(&h, G_LV_TICKS) == before + 1,
          "back in front, BRAWLPIT steps again and is still paused: the player resumes, not the toggle");
    CHECK(shift_x(&h) == 0 && level_host_state(&h) == lm_st_fg(), "Shift+X in the foreground is not a chord: passed on");

    /* close from the world, reload */
    shift_m(&h);
    CHECK(shift_x(&h) == 1 && level_host_state(&h) == lm_st_none() && last_is(REFLUX_ACTION_LEVEL_CLOSED, 1, 0, 0),
          "Shift+X in the world: NONE, LEVEL_CLOSED (1, reason 0 = the player)");
    CHECK(level_host_fb(&h, &w, &hh, &dirty) == NULL && level_host_memory(&h, NULL) == NULL && level_host_banner(&h) == 2,
          "the instance is gone; banner 'Shift+M play'");
    level_host_frame(&h, 16667);
    CHECK(level_host_state(&h) == lm_st_none(), "nothing loads by itself");
    CHECK(shift_m(&h) == 1 && level_host_state(&h) == lm_st_loading(), "Shift+M with nothing loaded: LOADING");
    level_host_frame(&h, 16667);
    CHECK(level_host_state(&h) == lm_st_fg() && reflux_host_action_type_at(reflux_host_log_size() - 2) == REFLUX_ACTION_LEVEL_LOADED &&
              reflux_host_action_c_at(reflux_host_log_size() - 2) == 2 && gw(&h, G_PHASE) == 0,
          "a fresh BRAWLPIT at its title screen, LEVEL_LOADED generation 2");
    CHECK(level_host_rejected(&h) == 0, "BRAWLPIT never had a dispatch refused");
    level_host_shutdown(&h);
    CHECK(level_host_state(&h) == lm_st_none() && level_host_memory(&h, NULL) == NULL, "shutdown frees the instance");
}

static const char *tmp_module(const char *nm, const unsigned char *b, size_t n) {
    static char path[512];
    const char *dir = getenv("TMPDIR");
    FILE *f;
    snprintf(path, sizeof path, "%s/%s.wasm", (dir && dir[0]) ? dir : "/tmp", nm);
    f = fopen(path, "wb");
    if (!f) return "/nonexistent";
    fwrite(b, 1, n, f);
    fclose(f);
    return path;
}

/* load a module from bytes; the state after one frame */
static int load_one(level_host *h, const char *nm, const unsigned char *b, size_t n) {
    level_desc d = brawlpit_desc(tmp_module(nm, b, n));
    d.n_assets = 0;
    reflux_host_reset();
    level_host_init(h, &d, lm_st_loading(), 1);
    level_host_frame(h, 16667);
    return level_host_state(h);
}

static void test_failures(void) {
    level_host h;
    level_desc d = brawlpit_desc("assets/levels/brawlpit/no_such_level.wasm");
    static const unsigned char garbage[] = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0xff, 0xff};
    char m[160];

    reflux_host_reset();
    level_host_init(&h, &d, lm_st_loading(), 1);
    level_host_frame(&h, 16667);
    CHECK(level_host_state(&h) == lm_st_none() && last_is(REFLUX_ACTION_LEVEL_LOAD_FAILED, 1, REFLUX_LEVEL_FAIL_READ, 0) &&
              strstr(level_host_message(&h), "no_such_level.wasm") != NULL,
          "an unreadable module: LOAD_FAILED (1, READ, 0), NONE, the message names the path");

    CHECK(load_one(&h, "lh_garbage", garbage, sizeof garbage) == lm_st_none() &&
              last_is(REFLUX_ACTION_LEVEL_LOAD_FAILED, 1, REFLUX_LEVEL_FAIL_BIND, WH_ERR_PARSE),
          "garbage bytes: LOAD_FAILED (1, BIND, WH_ERR_PARSE)");
    CHECK(load_one(&h, "lh_nomem", fx_lv_nomem, sizeof fx_lv_nomem) == lm_st_none() &&
              last_is(REFLUX_ACTION_LEVEL_LOAD_FAILED, 1, REFLUX_LEVEL_FAIL_BIND, WH_ERR_LOAD),
          "no exported memory: LOAD_FAILED (1, BIND, WH_ERR_LOAD)");
    CHECK(load_one(&h, "lh_bigfb", fx_lv_bigfb, sizeof fx_lv_bigfb) == lm_st_none() &&
              last_is(REFLUX_ACTION_LEVEL_LOAD_FAILED, 1, REFLUX_LEVEL_FAIL_BIND, 0) &&
              strstr(level_host_message(&h), "framebuffer") != NULL,
          "a framebuffer outside memory: LOAD_FAILED (1, BIND, 0), said so");
    CHECK(load_one(&h, "lh_api2", fx_lv_api2, sizeof fx_lv_api2) == lm_st_none() &&
              last_is(REFLUX_ACTION_LEVEL_LOAD_FAILED, 1, REFLUX_LEVEL_FAIL_API, 0) &&
              strstr(level_host_message(&h), "level_api() is 2") != NULL,
          "level_api 2: LOAD_FAILED (1, API, 0)");
    CHECK(load_one(&h, "lh_enter7", fx_lv_enter7, sizeof fx_lv_enter7) == lm_st_none() &&
              last_is(REFLUX_ACTION_LEVEL_LOAD_FAILED, 1, REFLUX_LEVEL_FAIL_ENTER, 0),
          "level_enter 7: LOAD_FAILED (1, ENTER, 0)");
    CHECK(load_one(&h, "lh_enter_trap", fx_lv_enter_trap, sizeof fx_lv_enter_trap) == lm_st_none() &&
              last_is(REFLUX_ACTION_LEVEL_LOAD_FAILED, 1, REFLUX_LEVEL_FAIL_ENTER, WH_ERR_TRAP) &&
              strstr(level_host_message(&h), "trapped") != NULL,
          "level_enter trapping: LOAD_FAILED (1, ENTER, WH_ERR_TRAP)");
    CHECK(count_rows(REFLUX_ACTION_LEVEL_LOADED) == 0 && count_rows(REFLUX_ACTION_LEVEL_SHOWN) == 0 &&
              level_host_memory(&h, NULL) == NULL,
          "a refused module is never announced as loaded or shown, and nothing stays loaded");

    CHECK(load_one(&h, "lh_ok", fx_lv_ok, sizeof fx_lv_ok) == lm_st_fg(), "a well-formed minimal level loads");
    level_host_shutdown(&h);

    CHECK(load_one(&h, "lh_tick_trap", fx_lv_tick_trap, sizeof fx_lv_tick_trap) == lm_st_fg(),
          "a level whose tick traps still loads");
    level_host_frame(&h, 16667);
    snprintf(m, sizeof m, "its first step traps: NONE, LEVEL_CLOSED (1, reason 1 = trap) [%s]", level_host_message(&h));
    CHECK(level_host_state(&h) == lm_st_none() && last_is(REFLUX_ACTION_LEVEL_CLOSED, 1, 1, 0) &&
              level_host_memory(&h, NULL) == NULL && strstr(level_host_message(&h), "trapped") != NULL,
          m);

    CHECK(load_one(&h, "lh_tick_lifecycle", fx_lv_tick_lifecycle, sizeof fx_lv_tick_lifecycle) == lm_st_fg(),
          "a level that dispatches in its tick loads");
    level_host_frame(&h, 16667);
    CHECK(count_rows(REFLUX_ACTION_LEVEL_CLOSED) == 0 && count_rows(REFLUX_ACTION_BP_MATCH_STARTED) == 1 &&
              level_host_rejected(&h) == 1,
          "its LEVEL_CLOSED is refused and counted, its own family's 1100 is logged");
    level_host_shutdown(&h);
    CHECK(level_host_rejected(&h) == 1, "the refusal count survives the unload");
}

/* the view-source overlay and SUSPEND (leaving the world for the menu): every overlay state has a way out,
 * the level never steps under the overlay, and only the transitions that change what is in front announce */
static void test_overlay_and_suspend(void) {
    level_host h;
    int hidden, ticks;
    CHECK(load_one(&h, "lh_ok_overlay", fx_lv_ok, sizeof fx_lv_ok) == lm_st_fg(), "a minimal level loads for the overlay run");
    CHECK(level_host_chord(&h, 'j', 1, 1, 0, 0, 0, 0, 0) == 1 && level_host_state(&h) == lm_st_source() &&
              last_is(REFLUX_ACTION_LEVEL_HIDDEN, 1, 0, 0),
          "Ctrl+Shift+J in the foreground: SOURCE, LEVEL_HIDDEN");
    CHECK(!level_host_owns_input(&h) && level_host_banner(&h) == 0, "under the overlay the level owns no input and no hint is drawn");
    ticks = h.ticks;
    level_host_frame(&h, 50000);
    CHECK(h.ticks == ticks && level_host_memory(&h, NULL) != NULL, "no steps under the overlay, and the level stays loaded");
    CHECK(level_host_command(&h, lm_cmd_overlay_closed()) == 1 && level_host_state(&h) == lm_st_fg() &&
              last_is(REFLUX_ACTION_LEVEL_SHOWN, 1, 0, 0),
          "OVERLAY_CLOSED: back into the level, LEVEL_SHOWN");
    CHECK(level_host_command(&h, lm_cmd_suspend()) == 1 && level_host_state(&h) == lm_st_bg() &&
              last_is(REFLUX_ACTION_LEVEL_HIDDEN, 1, 0, 0),
          "SUSPEND in the foreground: BG, LEVEL_HIDDEN (BRAWLPIT pauses its match on it)");
    hidden = count_rows(REFLUX_ACTION_LEVEL_HIDDEN);
    CHECK(level_host_chord(&h, 'j', 1, 1, 0, 0, 0, 0, 0) == 1 && level_host_state(&h) == lm_st_source() &&
              count_rows(REFLUX_ACTION_LEVEL_HIDDEN) == hidden,
          "Ctrl+Shift+J in the world view: SOURCE, nothing announced (already hidden)");
    CHECK(level_host_command(&h, lm_cmd_suspend()) == 1 && level_host_state(&h) == lm_st_bg(),
          "SUSPEND under the overlay: BG");
    CHECK(level_host_command(&h, lm_cmd_suspend()) == 0 && level_host_state(&h) == lm_st_bg(), "SUSPEND in BG changes nothing");
    CHECK(shift_x(&h) == 1 && level_host_state(&h) == lm_st_none(), "Shift+X closes it");
    CHECK(level_host_chord(&h, 'j', 1, 1, 0, 0, 0, 0, 0) == 1 && level_host_state(&h) == lm_st_source_none() &&
              level_host_command(&h, lm_cmd_overlay_closed()) == 1 && level_host_state(&h) == lm_st_none(),
          "with nothing loaded: SOURCE_NONE, and closing the overlay goes back to NONE");
    CHECK(level_host_chord(&h, 'j', 1, 1, 0, 0, 0, 0, 0) == 1 && level_host_command(&h, lm_cmd_suspend()) == 1 &&
              level_host_state(&h) == lm_st_none() && shift_m(&h) == 1 && level_host_state(&h) == lm_st_loading(),
          "SUSPEND from SOURCE_NONE: NONE, and Shift+M still loads (no wedge)");
    level_host_shutdown(&h);
}

int main(void) {
    test_brawlpit_life();
    test_failures();
    test_overlay_and_suspend();
    if (failures) { printf("level_host_test: %d of %d FAILED\n", failures, checks); return 1; }
    printf("level_host_test: ALL PASS (%d checks)\n", checks);
    return 0;
}
