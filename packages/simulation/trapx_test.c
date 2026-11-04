/* packages/simulation/trapx_test.c -- TRAPX VS0.  make test-trapx
 *
 * Two jobs, the same two as every slice in this merge:
 *
 *   1. PARITY against ../../PARENA/stdlib/trapx/trapx_parity.txt -- the same rows the WOTAN sandbox's generated
 *      JavaScript asserts (WOTAN/scripts/trapx-rules-test.mjs). The fixture is generated FROM the C, so a drifting
 *      port goes red on one side instead of two worlds quietly disagreeing.
 *   2. THE VS0 ACCEPTANCE CRITERIA, as behaviour: two Field Offices in the Apartment and one in the School, a
 *      portal between them, alertness that really does rise faster in Jiangshi territory, a shared city memory,
 *      one receipt feed, BIG_O's public-kill reaction, K9 diminishing returns, and the mods-first delivery pass.
 *
 * Every expected value is hand-derived from the .prn, not read back out of the C.
 */

#include "trapx.h"
#include "trapx_events.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The generated rules, declared once, above their first use: -Werror and C99 do not let a switch body invent an
 * implicit declaration. trapx.c keeps its own copies private; this is the test's own view of the same functions. */
extern int trapx_scene_fos(int);
extern int trapx_scene_watcher_density(int);
extern int trapx_scene_district(int);
extern int trapx_portal_target(int);
extern int trapx_scene_known(int);
extern int trapx_alertness_step(int, int, int, int);
extern int trapx_trust_step(int, int, int);
extern int trapx_enforcement_level(int, int, int);
extern int trapx_media_press(int, int);
extern int trapx_heat_step(int, int, int);
extern int trapx_claim_cost(int, int);
extern int trapx_flow_per_tick(int, int);
extern int trapx_pressure_per_tick(int, int, int);
extern int trapx_attention_step(int, int);
extern int trapx_k9_worth(int);
extern int trapx_contest_window_ms(int);
extern int trapx_integrity_decay(int, int, int);
extern int trapx_rogue_swarm(int, int, int);
extern int trapx_receipt_digest(int, int, int);
extern int trapx_receipt_known(int);
extern int trapx_public_kill_heat(int);
extern int trapx_public_kill_trust(int);

/* Where the shared fixture lives: same default as every other slice in this repo, and TRAPX_PARITY_FILE overrides
 * it for a checkout somewhere else. */
#ifndef TRAPX_PARITY_PATH
#define TRAPX_PARITY_PATH "../PARENA/stdlib/trapx/trapx_parity.txt"
#endif

static int fails = 0;
static int checks = 0;

static void check(int cond, const char *msg) {
    checks += 1;
    if (!cond) {
        fails += 1;
        printf("FAIL: %s\n", msg);
    }
}

static void eq(int got, int want, const char *msg) {
    checks += 1;
    if (got != want) {
        fails += 1;
        printf("FAIL: %s (got %d want %d)\n", msg, got, want);
    }
}

/* ---- 1. parity ---------------------------------------------------------------------------------- */
static int parity_rows(void) {
    const char *env = getenv("TRAPX_PARITY_FILE");
    const char *path = (env && env[0]) ? env : TRAPX_PARITY_PATH;
    FILE *fh = fopen(path, "r");
    if (!fh) {
        printf("FAIL: cannot open %s (set TRAPX_PARITY_DIR to the PARENA checkout)\n", path);
        fails += 1;
        checks += 1;
        return 0;
    }
    char line[512];
    int rows = 0;
    while (fgets(line, sizeof(line), fh)) {
        int q = 0, a = 0, b = 0, c = 0, d = 0, want = 0;
        if (sscanf(line, "%d %d %d %d %d | %d", &q, &a, &b, &c, &d, &want) != 6) continue;
        int got = 0;
        switch (q) {
        case 0: got = trapx_scene_fos(a); break;
        case 1: got = trapx_scene_watcher_density(a); break;
        case 2: got = trapx_scene_district(a); break;
        case 3: got = trapx_portal_target(a); break;
        case 4: got = trapx_scene_known(a); break;
        case 5: got = trapx_alertness_step(a, b, c, d); break;
        case 6: got = trapx_trust_step(a, b, c); break;
        case 7: got = trapx_enforcement_level(a, b, c); break;
        case 8: got = trapx_media_press(a, b); break;
        case 9: got = trapx_heat_step(a, b, c); break;
        case 10: got = trapx_claim_cost(a, b); break;
        case 11: got = trapx_flow_per_tick(a, b); break;
        case 12: got = trapx_pressure_per_tick(a, b, c); break;
        case 13: got = trapx_attention_step(a, b); break;
        case 14: got = trapx_k9_worth(a); break;
        case 15: got = trapx_contest_window_ms(a); break;
        case 16: got = trapx_integrity_decay(a, b, c); break;
        case 17: got = trapx_rogue_swarm(a, b, c); break;
        case 18: got = trapx_receipt_digest(a, b, c); break;
        case 19: got = trapx_receipt_known(a); break;
        case 20: got = trapx_public_kill_heat(a); break;
        case 21: got = trapx_public_kill_trust(a); break;
        default:
            check(0, "parity row names an unknown question");
            continue;
        }
        rows += 1;
        checks += 1;
        if (got != want) {
            fails += 1;
            printf("FAIL: parity row %d (q=%d %d,%d,%d,%d) got %d want %d\n", rows, q, a, b, c, d, got, want);
        }
    }
    fclose(fh);
    check(rows >= 24, "read at least 24 parity rows");
    return rows;
}

int main(void) {
    int rows = parity_rows();
    TrapxCity city;
    int delivered[TRAPX_MOD_CAP];

    /* ---- 2. the two scenes exist, and the host's list is not a third source of truth ---------------- */
    trapx_city_init(&city, TRAPX_SCENE_APARTMENT);
    for (int i = 0; i < TRAPX_VS0_SCENES; i++) {
        int scene = i == 0 ? TRAPX_SCENE_APARTMENT : TRAPX_SCENE_SCHOOL;
        check(trapx_scene_known(scene), "a VS0 scene is known to the rules");
        check(trapx_city_index(&city, scene) >= 0, "and the host holds a district for it");
    }
    eq(city.districts[trapx_city_index(&city, TRAPX_SCENE_APARTMENT)].fos_total, 2,
       "the Apartment holds two Field Offices");
    eq(city.districts[trapx_city_index(&city, TRAPX_SCENE_SCHOOL)].fos_total, 1, "the School holds one");
    eq(trapx_city_index(&city, 999), -1, "a scene outside VS0 has no district");
    eq(trapx_scene_known(999), 0, "and the rules agree it does not exist");

    /* ---- 3. Jiangshi territory notices faster --------------------------------------------------- */
    TrapxCity a, b;
    trapx_city_init(&a, TRAPX_SCENE_APARTMENT);
    trapx_city_init(&b, TRAPX_SCENE_SCHOOL);
    for (int i = 0; i < 5; i++) {
        trapx_city_tick(&a, 0);
        trapx_city_tick(&b, 0);
    }
    int apt = a.districts[trapx_city_index(&a, TRAPX_SCENE_APARTMENT)].alertness;
    int sch = b.districts[trapx_city_index(&b, TRAPX_SCENE_SCHOOL)].alertness;
    eq(apt, 45, "five ticks of Jiangshi density is +9 a tick");
    eq(sch, 20, "the custody zone is +4 a tick");
    check(apt > sch, "the Apartment notices faster than the School");

    /* laying low slows it -- 9 + 0 - 6 = 3 a tick */
    TrapxCity q;
    trapx_city_init(&q, TRAPX_SCENE_APARTMENT);
    trapx_city_tick(&q, 1);
    eq(q.districts[0].alertness, 3, "a quiet tick is 3, not 9");

    /* ---- 4. claiming: cost, refusal, and the bank ------------------------------------------------ */
    TrapxCity c;
    trapx_city_init(&c, TRAPX_SCENE_APARTMENT);
    eq(c.flow, TRAPX_START_FLOW, "a city starts with 100 Flow");
    eq(trapx_city_claim(&c, TRAPX_SCENE_SCHOOL), TRAPX_ERR_NOT_HERE, "you cannot claim an office where you are not");
    eq(trapx_city_claim(&c, TRAPX_SCENE_APARTMENT), TRAPX_OK, "the first Apartment office is claimable");
    eq(c.flow, 60, "and costs 40");
    eq(trapx_city_claim(&c, TRAPX_SCENE_APARTMENT), TRAPX_OK, "the second is claimable too");
    eq(c.flow, 0, "at 40 then 60");
    eq(trapx_city_claim(&c, TRAPX_SCENE_APARTMENT), TRAPX_ERR_FULL, "and the third is refused: the scene holds two");

    TrapxCity s;
    trapx_city_init(&s, TRAPX_SCENE_SCHOOL);
    eq(trapx_city_claim(&s, TRAPX_SCENE_SCHOOL), TRAPX_OK, "the School office is claimable");
    eq(s.flow, 10, "at the custody premium: 90");
    eq(trapx_city_claim(&s, TRAPX_SCENE_SCHOOL), TRAPX_ERR_FULL, "the School holds exactly one");

    TrapxCity poor;
    trapx_city_init(&poor, TRAPX_SCENE_APARTMENT);
    poor.flow = 39;
    eq(trapx_city_claim(&poor, TRAPX_SCENE_APARTMENT), TRAPX_ERR_POOR, "39 Flow cannot buy a 40 office");

    /* ---- 5. the portal, and the memory it shares ------------------------------------------------- */
    TrapxCity m;
    trapx_city_init(&m, TRAPX_SCENE_APARTMENT);
    for (int i = 0; i < 3; i++) trapx_city_tick(&m, 0);
    int before = m.districts[trapx_city_index(&m, TRAPX_SCENE_APARTMENT)].alertness;
    eq(before, 27, "three ticks in the Apartment");
    eq(trapx_city_portal(&m, TRAPX_SCENE_SCHOOL), TRAPX_OK, "the portal to the School is open");
    eq(m.scene, TRAPX_SCENE_SCHOOL, "and you are in the School");
    trapx_city_tick(&m, 0);
    eq(trapx_city_portal(&m, TRAPX_SCENE_APARTMENT), TRAPX_OK, "and back");
    eq(m.districts[trapx_city_index(&m, TRAPX_SCENE_APARTMENT)].alertness, before + 9,
       "the Apartment kept counting while you were away -- one city, not two saves");
    eq(trapx_city_portal(&m, TRAPX_SCENE_APARTMENT), TRAPX_ERR_NO_PORTAL, "there is no portal from a scene to itself");
    eq(trapx_city_portal(&m, 204), TRAPX_ERR_SCENE, "and none to a district VS0 does not have");

    /* ---- 6. receipts: one feed, shared by both scenes, chained ----------------------------------- */
    TrapxCity r1, r2;
    trapx_city_init(&r1, TRAPX_SCENE_APARTMENT);
    trapx_city_init(&r2, TRAPX_SCENE_APARTMENT);
    trapx_city_tick(&r1, 0);
    trapx_city_tick(&r2, 0);
    trapx_city_claim(&r1, TRAPX_SCENE_APARTMENT);
    trapx_city_claim(&r2, TRAPX_SCENE_APARTMENT);
    eq(r1.receipts, 1, "a claim writes one receipt");
    trapx_city_portal(&r1, TRAPX_SCENE_SCHOOL);
    trapx_city_portal(&r2, TRAPX_SCENE_SCHOOL);
    trapx_city_public_kill(&r1, TRAPX_SCENE_SCHOOL, 3);
    trapx_city_public_kill(&r2, TRAPX_SCENE_SCHOOL, 3);
    eq(r1.receipts, 2, "and the School's receipt lands in the same feed");
    eq(r1.ledger, r2.ledger, "the same actions in the same order chain to the same ledger");
    check(r1.ledger > 0, "the ledger holds a digest");

    /* ---- 7. BIG_O's half: a public killing stiffens the street ---------------------------------- */
    TrapxCity k;
    trapx_city_init(&k, TRAPX_SCENE_APARTMENT);
    int idx = trapx_city_index(&k, TRAPX_SCENE_APARTMENT);
    int trust0 = k.districts[idx].trust;
    eq(trapx_city_public_kill(&k, TRAPX_SCENE_APARTMENT, 5), TRAPX_OK, "a killing with five witnesses");
    eq(k.districts[idx].heat, 28, "adds 8 + 4 a witness of heat");
    eq(k.districts[idx].trust, trust0 - 34, "and costs 4 + 6 a witness of trust");
    eq(k.districts[idx].kills, 1, "it is counted");
    eq(trapx_city_public_kill(&k, TRAPX_SCENE_APARTMENT, -1), TRAPX_ERR_ARG, "a negative witness count is refused");

    /* trust that low, with heat there, is already Enforcement level 1 -- the loop closes */
    check(k.districts[idx].enforcement >= 1, "the city reacted before the next tick even ran");
    for (int i = 0; i < 8; i++) trapx_city_tick(&k, 0);
    uint32_t kills_band = 0;
    for (int i = 0; i < trapx_events_count(&k.events); i++) {
        const TrapxEvent *ev = trapx_events_at(&k.events, i);
        if (ev->kind == TRAPX_EV_KILL) kills_band |= 1u << ev->kind;
    }
    check(kills_band != 0, "the killing was announced on the bus");

    /* ---- 8. K9 Doctrine: diminishing returns, and attention ------------------------------------- */
    eq(trapx_k9_worth(0), 1000, "the first unit is worth 1000");
    eq(trapx_k9_worth(1), 850, "the second 850");
    eq(trapx_k9_worth(2), 722, "the third 722");
    check(trapx_k9_worth(2) < trapx_k9_worth(1) && trapx_k9_worth(6) == trapx_k9_worth(5),
          "worth falls and then floors");

    TrapxCity d;
    trapx_city_init(&d, TRAPX_SCENE_APARTMENT);
    trapx_city_claim(&d, TRAPX_SCENE_APARTMENT);
    eq(trapx_city_deploy_k9(&d, TRAPX_SCENE_APARTMENT, 2), TRAPX_OK, "two dogs on the street");
    idx = trapx_city_index(&d, TRAPX_SCENE_APARTMENT);
    eq(d.districts[idx].attention, 14, "raise Attention by 7 a unit");
    eq(trapx_city_deploy_k9(&d, TRAPX_SCENE_APARTMENT, 6), TRAPX_ERR_ARG, "six units is out of range");
    eq(trapx_attention_step(10, 0), 6, "with no dogs out, Attention decays by 4");

    /* ---- 9. an office that falls apart ---------------------------------------------------------- */
    TrapxCity g;
    trapx_city_init(&g, TRAPX_SCENE_APARTMENT);
    trapx_city_claim(&g, TRAPX_SCENE_APARTMENT);
    idx = trapx_city_index(&g, TRAPX_SCENE_APARTMENT);
    g.districts[idx].integrity = 15;
    g.districts[idx].attention = 85;
    g.districts[idx].pressure = 70;
    trapx_city_tick(&g, 0);
    eq(g.districts[idx].rogue, 1, "a beaten office under hard attention and real pressure swarms");
    int rogue_announced = 0;
    for (int i = 0; i < trapx_events_count(&g.events); i++) {
        if (trapx_events_at(&g.events, i)->kind == TRAPX_EV_ROGUE) rogue_announced = 1;
    }
    eq(rogue_announced, 1, "and says so on the bus, once");

    /* ---- 10. MODS FIRST: masks, priority, enable/disable ---------------------------------------- */
    eq(city.mods.count, 3, "VS0 ships three mods");
    eq(trapx_mods_index(&city.mods, "trapx.ledger"), 0, "the ledger mod is registered by name");

    TrapxEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = TRAPX_EV_RECEIPT;
    eq(trapx_mods_deliver(&city.mods, &ev, delivered, TRAPX_MOD_CAP), 1, "only the ledger wants a receipt");
    eq(delivered[0], trapx_mods_index(&city.mods, "trapx.ledger"), "and it is the ledger");

    ev.kind = TRAPX_EV_ALERT;
    eq(trapx_mods_deliver(&city.mods, &ev, delivered, TRAPX_MOD_CAP), 1, "only the watcher wants an alert");
    eq(delivered[0], trapx_mods_index(&city.mods, "trapx.watcher"), "and it is the watcher");

    ev.kind = TRAPX_EV_TICK;
    eq(trapx_mods_deliver(&city.mods, &ev, delivered, TRAPX_MOD_CAP), 0, "nobody subscribed to ticks");

    /* a new mod, higher priority, takes the front of the delivery order without touching the city */
    TrapxCity mm;
    trapx_city_init(&mm, TRAPX_SCENE_APARTMENT);
    int extra = trapx_mods_add(&mm.mods, "trapx.audit", (1u << TRAPX_EV_RECEIPT), 999);
    check(extra > 0, "a mod can be added at runtime");
    ev.kind = TRAPX_EV_RECEIPT;
    eq(trapx_mods_deliver(&mm.mods, &ev, delivered, TRAPX_MOD_CAP), 2, "two mods want receipts now");
    eq(delivered[0], extra, "the higher priority runs first");
    trapx_mods_enable(&mm.mods, extra, 0);
    eq(trapx_mods_deliver(&mm.mods, &ev, delivered, TRAPX_MOD_CAP), 1, "a disabled mod is not delivered to");
    eq(trapx_mods_add(&mm.mods, "trapx.audit", 0, 0), -1, "a duplicate name is refused, not overwritten");

    /* ---- 11. the bus itself --------------------------------------------------------------------- */
    TrapxEvents ring;
    trapx_events_reset(&ring);
    for (int i = 0; i < TRAPX_EVENT_CAP + 44; i++) {
        trapx_events_push(&ring, TRAPX_EV_TICK, i, TRAPX_SCENE_APARTMENT, i, 0);
    }
    eq(trapx_events_count(&ring), TRAPX_EVENT_CAP, "the ring holds 256 and no more");
    eq((int)ring.seq, TRAPX_EVENT_CAP + 44, "but the sequence keeps counting");
    eq(trapx_events_at(&ring, 0)->tick, TRAPX_EVENT_CAP + 43, "newest first");
    eq(trapx_events_push(&ring, TRAPX_EV_COUNT, 0, 0, 0, 0), 0, "an unknown kind is refused");
    check(trapx_events_at(&ring, TRAPX_EVENT_CAP) == NULL, "and reading past the end answers nothing");
    for (int i = 0; i < TRAPX_EV_COUNT; i++) {
        check(strcmp(trapx_event_name(i), "unknown") != 0, "every event kind has a name");
    }
    eq(strcmp(trapx_event_name(TRAPX_EV_COUNT), "unknown"), 0, "and only real kinds have one");

    /* ---- 12. the announce path the host actually runs -------------------------------------------- */
    TrapxCity ann;
    trapx_city_init(&ann, TRAPX_SCENE_APARTMENT);
    eq(trapx_city_announce(&ann, TRAPX_EV_ALERT, TRAPX_SCENE_APARTMENT, 30, 0, delivered, TRAPX_MOD_CAP), 1,
       "one announce reaches one subscriber");

    if (fails > 0) {
        printf("trapx_test: %d of %d FAILED\n", fails, checks);
        return 1;
    }
    printf("trapx_test OK (%d checks, %d parity rows)\n", checks, rows);
    return 0;
}
