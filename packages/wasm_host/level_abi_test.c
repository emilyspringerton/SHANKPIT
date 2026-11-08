/* packages/wasm_host/level_abi_test.c -- `make test-level-abi`. T0 step 3: a level module talks to the world
 * only through REFLUX, and only within its own family (docs/T0_LEVEL_WASM_CONTRACT.md section 2).
 *
 *   1. the family predicates hold at every boundary (999/1000/1099/1100/1199/1200);
 *   2. hand-assembled modules: a module's dispatch reaches the log only inside its family -- a lifecycle
 *      row, a world row, and the types just past either end are dropped and counted; a module importing
 *      reflux_host_log_size (which saturates) or the dispatch import with the wrong signature is refused;
 *   3. the host's export table agrees with the level's build manifest (../PARENA, skipped loudly if absent);
 *   4. the real thing, the COMMITTED assets/levels/brawlpit/brawlpit_level.wasm under wasm3, driven like a
 *      player: a press made in the frame the level loses focus is dropped (level_blur);
 *      '1' (VS CPU) -> Enter -> Enter starts a match and BP_MATCH_STARTED (1100) lands in the log
 *      with the mode/stage/fighter payload; LEVEL_HIDDEN on that very step (the hard frame: the countdown
 *      is still 170) opens BRAWLPIT's own pause menu one step later and the match is announced exactly once
 *      across 100 paused steps; LEVEL_HIDDEN mid-fight pauses at once; one for another level id, or another
 *      lifecycle row, changes nothing; an overtaken cursor is counted, not hidden; P1 walks off the stage
 *      until out of stocks and BP_MATCH_ENDED (1101) carries the winner, sim frames and mode; REMATCH
 *      announces again; the real
 *      module never had a dispatch refused.
 * The level's decisions are checked on its own state words (the G region, PARENA stdlib/brawlpit/bp_mem.prn
 * and bp_level.prn), not just on its events: the test proves the pause happened, not that a row was sent.
 *   LEVEL_WASM=<path> overrides the module, LEVEL_MANIFEST=<path> the manifest. */
#define _POSIX_C_SOURCE 199309L
#include "level_abi.h"
#include "level_abi_test_fixtures.h"
#include "../reflux/reflux_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures = 0;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); failures++; } else printf("PASS: %s\n", m); } while (0)

static unsigned char *read_file(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    long len;
    unsigned char *b;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    b = (unsigned char *)malloc((size_t)len + 1);
    if (b && fread(b, 1, (size_t)len, f) != (size_t)len) { free(b); b = NULL; }
    if (b) b[len] = 0;
    fclose(f);
    *n = (size_t)len;
    return b;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

/* rows of one type currently in the log */
static int count_rows(int type) {
    int i, n = 0, len = reflux_host_log_size();
    for (i = 0; i < len; i++)
        if (reflux_host_action_type_at(i) == type) n++;
    return n;
}

/* the most recent row of one type: its retained index, or -1 */
static int last_row(int type) {
    int i;
    for (i = reflux_host_log_size() - 1; i >= 0; i--)
        if (reflux_host_action_type_at(i) == type) return i;
    return -1;
}

/* ---- 1. predicates ----------------------------------------------------------------------------- */

static void test_predicates(void) {
    CHECK(!reflux_action_is_level(999) && reflux_action_is_level(1000) && reflux_action_is_level(1099) &&
              !reflux_action_is_level(1100),
          "reflux_action_is_level is exactly 1000-1099");
    CHECK(!reflux_action_is_brawlpit(1099) && reflux_action_is_brawlpit(1100) && reflux_action_is_brawlpit(1199) &&
              !reflux_action_is_brawlpit(1200),
          "reflux_action_is_brawlpit is exactly 1100-1199");
    CHECK(reflux_action_is_level(REFLUX_ACTION_LEVEL_LOADED) && reflux_action_is_level(REFLUX_ACTION_LEVEL_SWAPPED) &&
              reflux_action_is_brawlpit(REFLUX_ACTION_BP_MATCH_STARTED) &&
              reflux_action_is_brawlpit(REFLUX_ACTION_BP_MATCH_ENDED),
          "every defined level/BRAWLPIT type is inside its own family");
    CHECK(!reflux_action_is_level(REFLUX_ACTION_BUTTON_PRESSED) && !reflux_action_is_brawlpit(REFLUX_ACTION_CITIZEN_RETREATED) &&
              !reflux_action_is_brawlpit(REFLUX_ACTION_LEVEL_CLOSED),
          "no world type and no lifecycle type is in the BRAWLPIT family");
}

/* ---- 2. hand-assembled modules ----------------------------------------------------------------- */

static void test_fixtures(void) {
    level_reflux_bridge br;
    wh_import imp[LEVEL_ABI_N_IMPORTS];
    wh_instance *inst = NULL;
    wh_func go;
    wh_status s;
    char msg[256];
    int n;

    reflux_host_reset();
    level_reflux_bridge_init(&br, reflux_action_is_brawlpit);
    n = level_reflux_imports(&br, imp);
    CHECK(n == LEVEL_ABI_N_IMPORTS, "the bridge offers the eight env.reflux_host_* imports");
    s = wh_instantiate(fx_dispatch, sizeof fx_dispatch, imp, n, 0, &inst, msg, sizeof msg);
    CHECK(s == WH_OK, "a module importing only reflux_host_dispatch binds against the bridge");
    if (s == WH_OK) {
        s = wh_lookup(inst, "go", "v()", &go, msg, sizeof msg);
        if (s == WH_OK) s = wh_call_i32(&go, NULL, 0, NULL, msg, sizeof msg);
        CHECK(s == WH_OK, "go() runs (six dispatches across the family boundaries)");
        CHECK(reflux_host_total_dispatched() == 2, "exactly the two in-family dispatches reached the log");
        CHECK(reflux_host_action_type_at(0) == 1100 && reflux_host_action_a_at(0) == 7 &&
                  reflux_host_action_b_at(0) == 8 && reflux_host_action_c_at(0) == 9,
              "1100 (7,8,9) is logged with its payload intact");
        CHECK(reflux_host_action_type_at(1) == 1199 && reflux_host_action_c_at(1) == 3,
              "1199, the family's last type, is logged");
        CHECK(br.accepted == 2 && br.rejected == 4,
              "1003 (lifecycle), 5 (a world type), 1200 and -1 were refused and counted");
        CHECK(br.last_rejected == -1, "the most recent refusal is named (-1)");
        CHECK(count_rows(REFLUX_ACTION_LEVEL_CLOSED) == 0 && count_rows(REFLUX_ACTION_TYLER_BEAT) == 0,
              "no lifecycle row and no world row was faked into the log");
        wh_destroy(inst);
        inst = NULL;
    }

    /* a bridge with no family refuses everything */
    reflux_host_reset();
    level_reflux_bridge_init(&br, NULL);
    n = level_reflux_imports(&br, imp);
    s = wh_instantiate(fx_dispatch, sizeof fx_dispatch, imp, n, 0, &inst, msg, sizeof msg);
    if (s == WH_OK && wh_lookup(inst, "go", "v()", &go, msg, sizeof msg) == WH_OK)
        s = wh_call_i32(&go, NULL, 0, NULL, msg, sizeof msg);
    CHECK(s == WH_OK && reflux_host_total_dispatched() == 0 && br.rejected == 6,
          "a module with no family dispatches nothing (all six refused)");
    if (inst) { wh_destroy(inst); inst = NULL; }

    level_reflux_bridge_init(&br, reflux_action_is_brawlpit);
    n = level_reflux_imports(&br, imp);
    msg[0] = 0;
    s = wh_instantiate(fx_logsize, sizeof fx_logsize, imp, n, 0, &inst, msg, sizeof msg);
    CHECK(s == WH_ERR_IMPORT_UNBOUND && strstr(msg, "reflux_host_log_size") != NULL,
          "a module importing reflux_host_log_size (saturates at 256) is refused at load, by name");
    if (inst) { wh_destroy(inst); inst = NULL; }
    msg[0] = 0;
    s = wh_instantiate(fx_badsig, sizeof fx_badsig, imp, n, 0, &inst, msg, sizeof msg);
    CHECK(s == WH_ERR_IMPORT_SIG && strstr(msg, "reflux_host_dispatch") != NULL,
          "reflux_host_dispatch imported as v(ii) is refused at load, by name");
    if (inst) { wh_destroy(inst); inst = NULL; }
}

/* ---- 3. the host's table vs the level's manifest ---------------------------------------------- */

static void test_manifest(void) {
    const char *path = getenv("LEVEL_MANIFEST");
    size_t n = 0;
    char *text, *line;
    int exports = 0, exports_ok = 0, imports = 0, imports_ok = 0, i;
    level_reflux_bridge br;
    wh_import imp[LEVEL_ABI_N_IMPORTS];
    if (!path) path = "../PARENA/stdlib/brawlpit/level.manifest";
    text = (char *)read_file(path, &n);
    if (!text) {
        printf("SKIP: no level manifest at %s (a sibling PARENA checkout; set LEVEL_MANIFEST)\n", path);
        return;
    }
    level_reflux_bridge_init(&br, reflux_action_is_brawlpit);
    level_reflux_imports(&br, imp);
    for (line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        char key[16], a[96], b[32];
        if (sscanf(line, "%15s %95s %31s", key, a, b) != 3) continue;
        if (strcmp(key, "export") == 0) {
            exports++;
            for (i = 0; i < LEVEL_ABI_N_EXPORTS; i++) {
                const char *nm, *sg;
                level_abi_export_info(i, &nm, &sg);
                if (strcmp(nm, a) == 0 && strcmp(sg, b) == 0) { exports_ok++; break; }
            }
            if (i == LEVEL_ABI_N_EXPORTS) printf("  manifest export %s %s is not in the host's table\n", a, b);
        } else if (strcmp(key, "import") == 0) {
            imports++;
            for (i = 0; i < LEVEL_ABI_N_IMPORTS; i++)
                if (strncmp(a, "env.", 4) == 0 && strcmp(imp[i].field, a + 4) == 0 && strcmp(imp[i].sig, b) == 0) {
                    imports_ok++;
                    break;
                }
            if (i == LEVEL_ABI_N_IMPORTS) printf("  manifest import %s %s is not offered by the bridge\n", a, b);
        }
    }
    free(text);
    CHECK(exports == LEVEL_ABI_N_EXPORTS && exports_ok == exports,
          "the manifest's exports are exactly the host's table (name and signature)");
    CHECK(imports == LEVEL_ABI_N_IMPORTS && imports_ok == imports,
          "the manifest's imports are exactly what the bridge offers (name and signature)");
}

/* ---- 4. the real level ------------------------------------------------------------------------- */

/* BRAWLPIT's state words (PARENA stdlib/brawlpit/bp_mem.prn, bp_level.prn): G region base and indices */
#define CTL_BASE 131072u /* key down bytes at +code, presses not yet ticked at +512+code (bp_input.prn) */
#define G_BASE 135168u
#define G_MATCH_OVER 2
#define G_PHASE 8
#define G_SIM_FRAMES 12
#define G_WINNER 13
#define G_PAUSED 26
#define G_MODE 50
#define G_STEP 51
#define G_COUNTDOWN 52
#define G_STAGE_SEL 54
#define G_LV_PAUSE_DUE 204
#define PH_SELECT 1
#define PH_MATCH 2
#define PH_RESULTS 3

typedef struct {
    level_exports x;
    uint8_t *mem;
    uint32_t mem_size;
    int dead;
    char msg[256];
} lv;

static int32_t gw(lv *l, int k) {
    int32_t v;
    memcpy(&v, l->mem + G_BASE + 4u * (uint32_t)k, sizeof v);
    return v;
}

static int32_t call0(lv *l, wh_func *f) {
    int32_t r = 0;
    if (wh_call_i32(f, NULL, 0, &r, l->msg, sizeof l->msg) != WH_OK) l->dead = 1;
    return r;
}

static int32_t call1(lv *l, wh_func *f, int32_t a) {
    int32_t r = 0;
    if (wh_call_i32(f, &a, 1, &r, l->msg, sizeof l->msg) != WH_OK) l->dead = 1;
    return r;
}

static void key(lv *l, int code, int down) {
    int32_t a[2];
    a[0] = code;
    a[1] = down;
    if (wh_call_i32(&l->x.key, a, 2, NULL, l->msg, sizeof l->msg) != WH_OK) l->dead = 1;
}

static void tick(lv *l) { call0(l, &l->x.tick); }

/* a full press: down, one step, up, one step */
static void press(lv *l, int code) {
    key(l, code, 1);
    tick(l);
    key(l, code, 0);
    tick(l);
}

static int fb_nonblank(lv *l) {
    uint32_t addr = (uint32_t)call0(l, &l->x.fb_addr), i;
    for (i = 0; i < 960u * 540u * 4u; i += 4)
        if (l->mem[addr + i] | l->mem[addr + i + 1] | l->mem[addr + i + 2]) return 1;
    return 0;
}

static void test_real_level(void) {
    const char *path = getenv("LEVEL_WASM");
    size_t n = 0;
    unsigned char *bytes;
    level_reflux_bridge br;
    wh_import imp[LEVEL_ABI_N_IMPORTS];
    wh_instance *inst = NULL;
    wh_status s;
    lv l;
    int32_t cursor, total, fbw, fbh, fba, a0, a1, a2;
    int i, idx, ended = 0, started_before;
    double t0, tick_ms, render_ms;

    if (!path) path = "assets/levels/brawlpit/brawlpit_level.wasm";
    bytes = read_file(path, &n);
    CHECK(bytes != NULL, "the committed level module is present (assets/levels/brawlpit/brawlpit_level.wasm)");
    if (!bytes) return;
    memset(&l, 0, sizeof l);
    reflux_host_reset();
    level_reflux_bridge_init(&br, reflux_action_is_brawlpit);
    s = wh_instantiate(bytes, n, imp, level_reflux_imports(&br, imp), 0, &inst, l.msg, sizeof l.msg);
    CHECK(s == WH_OK, "the BRAWLPIT level instantiates against the bridge (every import it uses is offered)");
    if (s != WH_OK) { printf("  %s\n", l.msg); free(bytes); return; }
    s = level_exports_bind(inst, &l.x, l.msg, sizeof l.msg);
    CHECK(s == WH_OK, "all 16 level exports bind by exact name and signature");
    if (s != WH_OK) { printf("  %s\n", l.msg); wh_destroy(inst); free(bytes); return; }
    l.mem = wh_memory(inst, &l.mem_size);
    CHECK(l.mem != NULL && l.mem_size == 8u * 1024u * 1024u, "the module's memory is the fixed 8 MB");
    CHECK(call0(&l, &l.x.api) == LEVEL_ABI_API, "level_api() == 1, the surface this host speaks");
    call1(&l, &l.x.seed, 4242);
    CHECK(call0(&l, &l.x.enter) == 0, "level_enter() == 0: a fresh BRAWLPIT at its title screen");

    fbw = call0(&l, &l.x.fb_w);
    fbh = call0(&l, &l.x.fb_h);
    fba = call0(&l, &l.x.fb_addr);
    CHECK(fbw == 960 && fbh == 540 && fba > 0 && (uint32_t)fba + 960u * 540u * 4u <= l.mem_size,
          "the framebuffer is 960x540 RGBA inside memory");
    a0 = call1(&l, &l.x.asset_addr, 0);
    a1 = call1(&l, &l.x.asset_addr, 1);
    a2 = call1(&l, &l.x.asset_addr, 2);
    CHECK(a0 > 0 && (uint32_t)a0 + 8u * 65536u <= l.mem_size && a1 > 0 && (uint32_t)a1 + 870400u <= l.mem_size && a2 == 0,
          "asset slot 0 holds 8 portrait tiles, slot 1 the 640x340 duck atlas, slot 2 nothing");
    call0(&l, &l.x.render);
    CHECK(fb_nonblank(&l), "level_render() draws the title screen");

    cursor = call1(&l, &l.x.poll, 0);
    CHECK(cursor == reflux_host_total_dispatched(), "level_poll(0) on a quiet log returns the log's total");

    /* blur: the level loses focus in the frame '1' and Shift went down, before any tick saw them */
    key(&l, 49, 1);
    key(&l, 16, 1);
    call0(&l, &l.x.blur);
    CHECK(l.mem[CTL_BASE + 16] == 0 && l.mem[CTL_BASE + 512 + 16] == 0 && l.mem[CTL_BASE + 49] == 0 &&
              l.mem[CTL_BASE + 512 + 49] == 0,
          "level_blur releases held keys and drops presses no tick has seen (Shift = P1's shield)");
    key(&l, 49, 0);
    key(&l, 16, 0);
    tick(&l);
    CHECK(gw(&l, G_PHASE) == 0, "so the dropped '1' does not fire when the level next ticks");

    /* '1' = VS CPU straight from the title, then Enter (fighter) -> stage step, Enter again starts */
    press(&l, 49);
    CHECK(gw(&l, G_PHASE) == PH_SELECT && gw(&l, G_MODE) == 0, "'1' on the title opens fighter select, VS CPU");
    press(&l, 13);
    CHECK(gw(&l, G_STEP) == 2, "Enter confirms the fighter (VS CPU skips P2) and moves to the stage step");
    CHECK(count_rows(REFLUX_ACTION_BP_MATCH_STARTED) == 0, "no match announced before one starts");
    key(&l, 13, 1);
    tick(&l);
    idx = last_row(REFLUX_ACTION_BP_MATCH_STARTED);
    CHECK(gw(&l, G_PHASE) == PH_MATCH && gw(&l, G_COUNTDOWN) == 170, "Enter on the stage step starts the match");
    CHECK(idx >= 0 && count_rows(REFLUX_ACTION_BP_MATCH_STARTED) == 1, "BP_MATCH_STARTED (1100) is in the log");
    if (idx >= 0) {
        int sel = gw(&l, G_STAGE_SEL), stage = sel == 0 ? 0 : sel == 1 ? 1 : 4;
        CHECK(reflux_host_action_a_at(idx) == 0 && reflux_host_action_b_at(idx) == stage &&
                  reflux_host_action_c_at(idx) == 2,
              "its payload is (mode 0 VS CPU, the stage id, 2 fighters)");
    }

    /* the hard frame: Shift+M on the very step the match started (countdown still 170) */
    reflux_host_dispatch(REFLUX_ACTION_LEVEL_HIDDEN, REFLUX_LEVEL_BRAWLPIT, 0, 0);
    cursor = call1(&l, &l.x.poll, cursor);
    call0(&l, &l.x.blur);
    CHECK(cursor == reflux_host_total_dispatched(), "level_poll returns the new cursor (the log's total)");
    CHECK(gw(&l, G_PAUSED) == 0 && gw(&l, G_LV_PAUSE_DUE) == 1,
          "LEVEL_HIDDEN on the start step owes the pause instead of freezing the countdown at 170");
    key(&l, 13, 0);
    tick(&l);
    CHECK(gw(&l, G_PAUSED) == 1 && gw(&l, G_COUNTDOWN) == 169 && gw(&l, G_LV_PAUSE_DUE) == 0,
          "one step later BRAWLPIT's own pause menu is open (READY counted down by one, nothing else ran)");
    for (i = 0; i < 100; i++) tick(&l);
    CHECK(gw(&l, G_PAUSED) == 1 && gw(&l, G_SIM_FRAMES) == 0, "the pause menu holds across 100 steps; no sim ran");
    CHECK(count_rows(REFLUX_ACTION_BP_MATCH_STARTED) == 1, "the match was announced exactly once across them");

    /* resume (P), count down, fight */
    press(&l, 80);
    CHECK(gw(&l, G_PAUSED) == 0, "P on the pause menu resumes");
    for (i = 0; i < 200; i++) tick(&l);
    CHECK(gw(&l, G_SIM_FRAMES) > 0 && gw(&l, G_COUNTDOWN) == 0, "after the countdown the fight runs");

    /* LEVEL_HIDDEN mid-fight pauses at once; one for another level, or another lifecycle row, does nothing */
    reflux_host_dispatch(REFLUX_ACTION_LEVEL_HIDDEN, REFLUX_LEVEL_BRAWLPIT, 0, 0);
    cursor = call1(&l, &l.x.poll, cursor);
    CHECK(gw(&l, G_PAUSED) == 1, "LEVEL_HIDDEN mid-fight opens the pause menu at once");
    press(&l, 80);
    reflux_host_dispatch(REFLUX_ACTION_LEVEL_HIDDEN, 2, 0, 0);
    reflux_host_dispatch(REFLUX_ACTION_LEVEL_SHOWN, REFLUX_LEVEL_BRAWLPIT, 0, 0);
    reflux_host_dispatch(REFLUX_ACTION_BUTTON_PRESSED, 1, 0, 0);
    cursor = call1(&l, &l.x.poll, cursor);
    CHECK(gw(&l, G_PAUSED) == 0, "LEVEL_HIDDEN for another level id, LEVEL_SHOWN and a world row change nothing");

    /* an overtaken cursor: a busy world dispatches 300 rows between two polls (the ring holds 256) */
    for (i = 0; i < 300; i++) reflux_host_dispatch(REFLUX_ACTION_BUTTON_PRESSED, i, 0, 0);
    {
        int expect_lost = reflux_host_oldest_abs() - cursor, before = call0(&l, &l.x.lost_rows);
        cursor = call1(&l, &l.x.poll, cursor);
        CHECK(cursor == reflux_host_total_dispatched() && call0(&l, &l.x.lost_rows) - before == expect_lost &&
                  expect_lost > 0,
              "an overtaken cursor skips to the oldest retained row and counts the gap (level_lost_rows)");
    }
    total = call1(&l, &l.x.poll, cursor + 1000);
    CHECK(total == reflux_host_total_dispatched(), "a cursor past the end resynchronises to the total");
    cursor = total;

    /* P1 holds A and walks off the stage, stock after stock (an idle P1 would sit out the match on the
     * respawn platform): the CPU wins, and BP_MATCH_ENDED carries the winner, the sim frames and the mode */
    key(&l, 65, 1);
    t0 = now_ms();
    for (i = 0; i < 60000 && !l.dead; i++) {
        tick(&l);
        if ((i & 255) == 0) cursor = call1(&l, &l.x.poll, cursor);
        if (gw(&l, G_PHASE) == PH_RESULTS) { ended = 1; break; }
    }
    tick_ms = (now_ms() - t0) / (double)(i + 1);
    key(&l, 65, 0);
    CHECK(ended, "P1 walks off the stage until out of stocks: the results screen appears");
    idx = last_row(REFLUX_ACTION_BP_MATCH_ENDED);
    CHECK(idx >= 0 && count_rows(REFLUX_ACTION_BP_MATCH_ENDED) == 1, "BP_MATCH_ENDED (1101) is in the log, once");
    if (idx >= 0)
        CHECK(reflux_host_action_a_at(idx) == gw(&l, G_WINNER) && reflux_host_action_a_at(idx) == 1 &&
                  reflux_host_action_b_at(idx) == gw(&l, G_SIM_FRAMES) && reflux_host_action_b_at(idx) > 0 &&
                  reflux_host_action_c_at(idx) == 0,
              "its payload is (winner slot 1 = the CPU, the sim frames played, mode 0)");
    printf("  match: %d sim frames, %.3f ms per level_tick (no drawing)\n", gw(&l, G_SIM_FRAMES), tick_ms);

    /* REMATCH (R, once the results screen takes input) announces a second match */
    started_before = count_rows(REFLUX_ACTION_BP_MATCH_STARTED);
    for (i = 0; i < 60; i++) tick(&l);
    press(&l, 82);
    CHECK(gw(&l, G_PHASE) == PH_MATCH && count_rows(REFLUX_ACTION_BP_MATCH_STARTED) == started_before + 1,
          "REMATCH announces a new match");
    t0 = now_ms();
    for (i = 0; i < 30; i++) call0(&l, &l.x.render);
    render_ms = (now_ms() - t0) / 30.0;
    printf("  %.2f ms per level_render (960x540 software renderer, this box)\n", render_ms);

    CHECK(br.rejected == 0 && br.accepted == 3,
          "the real module dispatched only its own family: start, end, rematch start (nothing refused)");
    CHECK(!l.dead && !wh_is_dead(inst), "no call trapped");
    call0(&l, &l.x.exit);
    wh_destroy(inst);
    free(bytes);
}

int main(void) {
    test_predicates();
    test_fixtures();
    test_manifest();
    test_real_level();
    if (failures) { printf("level_abi_test: %d FAILED\n", failures); return 1; }
    printf("level_abi_test: ALL PASS\n");
    return 0;
}
