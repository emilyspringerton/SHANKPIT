/* packages/simulation/level_manager_test.c -- `make test-level-manager`. The level manager's decisions
 * (PARENA stdlib/shankpit/level_manager_rules.prn, generated level_manager_rules.c) against the contract table
 * (docs/T0_LEVEL_WASM_CONTRACT.md section 2, "Level manager mod" and "Shortcuts"):
 *   - chords: exactly Shift+M, Shift+X, Ctrl+Shift+J (Cmd+Option+J on macOS); never with text focus or on a
 *     key-repeat; any extra modifier and it is not the chord;
 *   - the whole state machine, all 6 states x 8 commands (0 = none), against the table written out by hand;
 *   - the lifecycle row for all 36 (old, new) pairs;
 *   - who ticks, who owns input, the banner;
 *   - the fixed step: 60 Hz is one step a frame, 144 Hz averages out to 60/s, 30 Hz runs two, 20 Hz is capped
 *     at two and drops whole steps but keeps the fraction, a stall cannot overflow;
 *   - SDL keys -> DOM keyCodes, and every key BRAWLPIT reads (its kc-* table in ../PARENA bp_input.prn, or the
 *     fallback list below) is reachable from some SDL key. */
#include "level_manager_rules.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0, checks = 0;
#define CHECK(c, m) do { checks++; if (!(c)) { printf("FAIL: %s\n", m); failures++; } else printf("PASS: %s\n", m); } while (0)

#define SC(n) (0x40000000 + (n)) /* SDL_SCANCODE_TO_KEYCODE */

static void test_chords(void) {
    CHECK(lm_chord('m', 1, 0, 0, 0, 0, 0, 0) == 1, "Shift+M is TOGGLE");
    CHECK(lm_chord('x', 1, 0, 0, 0, 0, 0, 0) == 3, "Shift+X is CLOSE");
    CHECK(lm_chord('j', 1, 1, 0, 0, 0, 0, 0) == 2, "Ctrl+Shift+J is SOURCE (Windows, Linux)");
    CHECK(lm_chord('j', 0, 0, 1, 1, 1, 0, 0) == 2, "Cmd+Option+J is SOURCE on macOS");
    CHECK(lm_chord('j', 1, 1, 0, 0, 1, 0, 0) == 0 && lm_chord('j', 0, 0, 1, 1, 0, 0, 0) == 0,
          "each platform takes only its own view-source chord");
    CHECK(lm_chord('m', 0, 0, 0, 0, 0, 0, 0) == 0, "a bare M is not a chord (the world keeps its M)");
    CHECK(lm_chord('m', 1, 1, 0, 0, 0, 0, 0) == 0 && lm_chord('m', 1, 0, 1, 0, 0, 0, 0) == 0 &&
              lm_chord('m', 1, 0, 0, 1, 0, 0, 0) == 0,
          "Shift+M with Ctrl, Alt or GUI too is not TOGGLE");
    CHECK(lm_chord('x', 1, 1, 0, 0, 0, 0, 0) == 0 && lm_chord('j', 1, 0, 0, 0, 0, 0, 0) == 0 &&
              lm_chord('j', 0, 1, 0, 0, 0, 0, 0) == 0,
          "Ctrl+Shift+X, Shift+J and Ctrl+J are nothing");
    CHECK(lm_chord('m', 1, 0, 0, 0, 0, 1, 0) == 0 && lm_chord('x', 1, 0, 0, 0, 0, 1, 0) == 0 &&
              lm_chord('j', 1, 1, 0, 0, 0, 1, 0) == 0,
          "no chord fires while text focus is active");
    CHECK(lm_chord('m', 1, 0, 0, 0, 0, 0, 1) == 0, "a key-repeat never fires a chord (a held Shift+M toggles once)");
    CHECK(lm_chord('n', 1, 0, 0, 0, 0, 0, 0) == 0 && lm_chord(SC(225), 1, 0, 0, 0, 0, 0, 0) == 0,
          "other keys, and Shift itself, are nothing");
}

static void test_state_machine(void) {
    /* expect[state][cmd], cmd 0 = none .. 7 = OVERLAY_CLOSED */
    static const int expect[6][8] = {
        /* NONE        */ {0, 1, 5, 0, 0, 0, 0, 0},
        /* LOADING     */ {1, 1, 1, 1, 2, 0, 1, 1},
        /* FG          */ {2, 3, 4, 2, 2, 2, 0, 2},
        /* BG          */ {3, 2, 4, 0, 3, 3, 0, 3},
        /* SOURCE      */ {4, 4, 4, 4, 4, 4, 0, 2},
        /* SOURCE_NONE */ {5, 5, 5, 5, 5, 5, 5, 0},
    };
    int s, c, bad = 0;
    CHECK(lm_st_none() == 0 && lm_st_loading() == 1 && lm_st_fg() == 2 && lm_st_bg() == 3 && lm_st_source() == 4 &&
              lm_st_source_none() == 5,
          "state numbering is the contract's");
    CHECK(lm_cmd_toggle() == 1 && lm_cmd_source() == 2 && lm_cmd_close() == 3 && lm_cmd_load_ok() == 4 &&
              lm_cmd_load_fail() == 5 && lm_cmd_trap() == 6 && lm_cmd_overlay_closed() == 7,
          "command numbering is the contract's");
    for (s = 0; s < 6; s++)
        for (c = 0; c < 8; c++)
            if (lm_next(s, c) != expect[s][c]) {
                printf("  lm_next(%d, %d) = %d, expected %d\n", s, c, lm_next(s, c), expect[s][c]);
                bad++;
            }
    CHECK(bad == 0, "lm_next matches the contract's table in all 48 cases");
    CHECK(lm_next(99, 1) == 99 && lm_next(2, 99) == 2, "an unknown state or command changes nothing");
    CHECK(lm_next(lm_next(lm_st_loading(), lm_cmd_load_ok()), lm_cmd_toggle()) == lm_st_bg() &&
              lm_next(lm_st_bg(), lm_cmd_toggle()) == lm_st_fg(),
          "boot: LOADING -> FG on load, Shift+M -> BG, Shift+M -> FG");
}

static void test_announce(void) {
    int o, n, bad = 0;
    for (o = 0; o < 6; o++)
        for (n = 0; n < 6; n++) {
            int want = 0;
            if (o == n) want = 0;
            else if (n == 2) want = 1001;                         /* SHOWN from anywhere */
            else if (o == 2 && (n == 3 || n == 4)) want = 1002;    /* HIDDEN: FG -> BG or SOURCE */
            else if ((o == 2 || o == 3 || o == 4) && n == 0) want = 1003; /* CLOSED: a loaded level gone */
            if (lm_announce(o, n) != want) {
                printf("  lm_announce(%d, %d) = %d, expected %d\n", o, n, lm_announce(o, n), want);
                bad++;
            }
        }
    CHECK(bad == 0, "lm_announce: SHOWN into FG, HIDDEN out of FG, CLOSED when a loaded level goes (36 pairs)");
    CHECK(lm_announce(1, 0) == 0 && lm_announce(5, 0) == 0 && lm_announce(0, 1) == 0,
          "a failed load and the no-level overlay announce nothing here (the loader reports LOAD_FAILED)");
    CHECK(lm_close_reason(lm_cmd_trap()) == 1 && lm_close_reason(lm_cmd_close()) == 0,
          "LEVEL_CLOSED reason: 1 trap, 0 the player");
    CHECK(lm_loaded(2) && lm_loaded(3) && lm_loaded(4) && !lm_loaded(0) && !lm_loaded(1) && !lm_loaded(5),
          "an instance exists in FG, BG and SOURCE only");
    CHECK(lm_ticks(2) && !lm_ticks(3) && !lm_ticks(4) && lm_owns_input(2) && !lm_owns_input(3) && !lm_owns_input(0),
          "only FG ticks and owns input");
    CHECK(lm_banner(3) == 1 && lm_banner(0) == 2 && lm_banner(2) == 0 && lm_banner(4) == 0,
          "banner: resume/close over a suspended level, play with none, nothing otherwise");
}

/* run `frames` display frames of dt_us each; returns the total steps */
static int run_frames(int frames, int dt_us, int *max_per_frame) {
    int acc = 0, total = 0, f;
    *max_per_frame = 0;
    for (f = 0; f < frames; f++) {
        int n;
        acc = lm_acc_add(acc, dt_us);
        n = lm_steps(acc);
        acc = lm_acc_left(acc);
        total += n;
        if (n > *max_per_frame) *max_per_frame = n;
        if (acc < 0 || acc >= lm_tick_us()) return -1;
    }
    return total;
}

static void test_fixed_step(void) {
    int mx, t;
    CHECK(lm_max_steps() == 2 && lm_tick_us() == 16667, "the cap is 2 steps; a step is 16667 us");
    t = run_frames(600, 16667, &mx);
    CHECK(t == 600 && mx == 1, "60 Hz display: exactly one step a frame");
    t = run_frames(1440, 6944, &mx);
    CHECK(t >= 599 && t <= 600 && mx == 1, "144 Hz display: 10 s of frames run 600 steps, never two at once");
    t = run_frames(300, 33333, &mx);
    CHECK(t >= 598 && t <= 600 && mx == 2, "30 Hz display: two steps a frame keep real time");
    t = run_frames(200, 50000, &mx);
    CHECK(t == 400 && mx == 2, "20 Hz display: capped at two a frame, the third whole step is dropped");
    CHECK(lm_acc_left(50000 + 16666) < lm_tick_us(), "after the cap the fraction is kept, whole steps are not");
    CHECK(lm_acc_add(0, 2000000000) == lm_dt_cap_us() && lm_acc_add(0, -5) == 0 && lm_acc_add(-7, 100) == 100,
          "a stall is capped, a negative dt or accumulator is 0");
    CHECK(lm_steps(-1) == 0 && lm_acc_left(-1) == 0 && lm_steps(0) == 0 && lm_steps(16666) == 0 && lm_steps(16667) == 1,
          "steps: none before a whole tick, one at it");
}

/* BRAWLPIT's key ids, from its own source when the sibling checkout is here */
static int brawlpit_keys(int *out, int cap) {
    static const int fallback[] = {13, 27, 32, 37, 38, 39, 40, 16, 272, 273, 9, 65, 68, 87, 83, 74, 75, 80, 82, 191, 222, 8};
    FILE *f = fopen("../PARENA/stdlib/brawlpit/bp_input.prn", "r");
    char line[256];
    int n = 0, v;
    if (!f) {
        printf("SKIP: ../PARENA/stdlib/brawlpit/bp_input.prn not here; checking the fallback key list\n");
        for (n = 0; n < (int)(sizeof fallback / sizeof fallback[0]) && n < cap; n++) out[n] = fallback[n];
        return n;
    }
    while (fgets(line, sizeof line, f) && n < cap)
        if (strncmp(line, "(defn kc-", 9) == 0 && sscanf(strstr(line, ": I32"), ": I32 %d", &v) == 1) out[n++] = v;
    fclose(f);
    return n;
}

static void test_keys(void) {
    int keys[64], nk, k, reach[512], sym, missing = 0;
    CHECK(lm_dom_key('a') == 65 && lm_dom_key('z') == 90 && lm_dom_key('m') == 77, "letters: a..z -> 65..90");
    CHECK(lm_dom_key('0') == 48 && lm_dom_key('9') == 57, "digits keep their code");
    CHECK(lm_dom_key(13) == 13 && lm_dom_key(27) == 27 && lm_dom_key(32) == 32 && lm_dom_key(8) == 8 &&
              lm_dom_key(9) == 9,
          "Return, Escape, Space, Backspace, Tab");
    CHECK(lm_dom_key(SC(80)) == 37 && lm_dom_key(SC(82)) == 38 && lm_dom_key(SC(79)) == 39 && lm_dom_key(SC(81)) == 40,
          "arrows: left 37, up 38, right 39, down 40");
    CHECK(lm_dom_key(SC(225)) == 16 && lm_dom_key(SC(229)) == 272 && lm_dom_key(SC(224)) == 17 &&
              lm_dom_key(SC(228)) == 273 && lm_dom_key(SC(226)) == 18 && lm_dom_key(SC(230)) == 274,
          "modifiers: left Shift/Ctrl/Alt 16/17/18, right ones +256 (BRAWLPIT's P2 shield and jump)");
    CHECK(lm_dom_key('/') == 191 && lm_dom_key('\'') == 222 && lm_dom_key(';') == 186, "punctuation: / 191, ' 222, ; 186");
    CHECK(lm_dom_key(SC(58)) == 112 && lm_dom_key(SC(69)) == 123, "F1..F12 -> 112..123");
    CHECK(lm_dom_key(SC(88)) == 13 && lm_dom_key(SC(98)) == 96 && lm_dom_key(SC(89)) == 97 && lm_dom_key(SC(97)) == 105,
          "keypad Enter 13, keypad 0..9 -> 96..105");
    CHECK(lm_dom_key(SC(57)) == 0 && lm_dom_key(0) == 0 && lm_dom_key('!') == 0 && lm_dom_key(-1) == 0,
          "Caps Lock, 0, a shifted symbol, junk: not given to the level");

    memset(reach, 0, sizeof reach);
    for (sym = 0; sym < 128; sym++) { k = lm_dom_key(sym); if (k > 0 && k < 512) reach[k] = 1; }
    for (sym = 0; sym < 512; sym++) { k = lm_dom_key(SC(sym)); if (k > 0 && k < 512) reach[k] = 1; }
    nk = brawlpit_keys(keys, 64);
    for (k = 0; k < nk; k++)
        if (keys[k] <= 0 || keys[k] >= 512 || !reach[keys[k]]) { printf("  BRAWLPIT key %d unreachable\n", keys[k]); missing++; }
    {
        char m[96];
        snprintf(m, sizeof m, "every one of BRAWLPIT's %d key ids is reachable from an SDL key", nk);
        CHECK(nk >= 20 && missing == 0, m);
    }
}

int main(void) {
    test_chords();
    test_state_machine();
    test_announce();
    test_fixed_step();
    test_keys();
    if (failures) { printf("level_manager_test: %d of %d FAILED\n", failures, checks); return 1; }
    printf("level_manager_test: ALL PASS (%d checks)\n", checks);
    return 0;
}
