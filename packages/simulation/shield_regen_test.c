/* shield_regen_test.c -- the HOST half of shield regen, headless.
 *
 * The rule itself is checked in PARENA (apps/boot's regen-check, which runs in the boot smoke gate).
 * This checks what only a host can get wrong: the two numbers it keeps on a real PlayerState, the
 * ORDER damage is reported in, a repeated or missing tick, the spawn reset, an installed mod curve,
 * and the values a HUD reads. protocol.h is the real struct, and shield_rules.c is the real generated
 * rule -- neither is a stand-in that could drift from what ships.
 *
 *   make test-shield-regen
 */
#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "shield_regen.h"

static int fails;

static void ck(const char *what, int got, int want) {
    if (got != want) {
        printf("FAIL %s: got %d want %d\n", what, got, want);
        fails++;
    } else {
        printf("ok %s = %d\n", what, got);
    }
}

/* A body that has just been hit: shield value is what survived the hit, `now` is when it landed. */
static void hit(PlayerState *p, int shield_after, unsigned int now) {
    memset(p, 0, sizeof *p);
    p->active = 1;
    p->shield = shield_after;
    shield_regen_note_damage(p, now);
}

/* The shambler curve from packages/sim/shambler_mod.prn: flat one point, never the ramp. */
static int flat_one_hook(const PlayerState *p, int tick_index) {
    (void)p;
    (void)tick_index;
    return 1;
}

/* A mod with no opinion at all: every answer falls through to the PARENA rule. */
static int no_opinion_hook(const PlayerState *p, int tick_index) {
    (void)p;
    (void)tick_index;
    return 0;
}

/* A mod that repeats the rule's own curve, so the hook path and the closed form must agree. */
static int rule_curve_hook(const PlayerState *p, int tick_index) {
    (void)p;
    return sh_regen_tick_amount(tick_index);
}

int main(void) {
    PlayerState p;

    /* ---- the vulnerable four seconds, on the real struct ---- */
    hit(&p, 0, 1000);
    ck("counter at hit", shield_regen_counter_ms(&p, 1000), 4000);
    ck("counter tenths at hit", shield_regen_counter_tenths(&p, 1000), 40);
    ck("waiting at hit", shield_regen_waiting(&p, 1000), 1);
    shield_regen_tick(&p, 1000);
    ck("shield at hit", p.shield, 0);
    shield_regen_tick(&p, 4999);
    ck("shield at 3999 ms", p.shield, 0);
    ck("counter at 3999 ms", shield_regen_counter_ms(&p, 4999), 1);
    ck("counter tenths never early", shield_regen_counter_tenths(&p, 4999), 1);
    shield_regen_tick(&p, 4000);
    ck("shield at 3000 ms", p.shield, 0);

    /* ---- the first tick lands exactly at 4000, and the ramp grows ---- */
    shield_regen_tick(&p, 5000);
    ck("first tick at 4000", p.shield, 1);
    ck("counter after first tick", shield_regen_counter_ms(&p, 5000), 0);
    ck("waiting after first tick", shield_regen_waiting(&p, 5000), 0);
    ck("next tick is 2", shield_regen_next_amount(&p, 5000), 2);
    shield_regen_tick(&p, 6000);
    ck("two ticks", p.shield, 3);
    shield_regen_tick(&p, 9000);
    ck("five ticks", p.shield, 15);
    ck("bar permille", shield_regen_bar_permille(&p), 150);

    /* ---- a repeated call cannot bank points; a skipped tick is just later ---- */
    shield_regen_tick(&p, 9000);
    ck("repeat call is a no-op", p.shield, 15);
    shield_regen_tick(&p, 12000);
    /* 2000 ms later is EIGHT landed ticks (4000..11000), so 1+..+8 = 36 -- a host that skipped ticks
     * gets the ticks it would have had, not an extra one for the call. */
    ck("missed ticks are caught up", p.shield, 36);

    /* ---- a hit reports the shield AFTER absorbing, and restarts the four seconds ---- */
    p.shield = 0;
    shield_regen_note_damage(&p, 9000);
    shield_regen_tick(&p, 12999);
    ck("restart still 0", p.shield, 0);
    shield_regen_tick(&p, 13000);
    ck("restart first tick", p.shield, 1);

    /* ---- the bar is never exceeded, and a full shield keeps a fresh anchor ---- */
    hit(&p, 95, 20000);
    shield_regen_tick(&p, 29000);
    ck("clamped to the bar", p.shield, 100);
    hit(&p, 100, 30000);
    shield_regen_tick(&p, 200000);
    ck("full stays full", p.shield, 100);
    ck("full counter is fresh", shield_regen_counter_ms(&p, 200000), 4000);

    /* ---- a bad shield value is floored into the baseline, not carried ---- */
    hit(&p, -5, 0);
    ck("negative shield floored", p.shield, 0);
    ck("negative baseline floored", p.shield_regen_base, 0);

    /* ---- a stale or wrapped anchor cannot invent more than a bar ---- */
    p.shield = 0; p.shield_regen_base = 50; p.shield_regen_at_ms = 0;
    shield_regen_tick(&p, 4000000000u);
    ck("stale anchor inside the bar", p.shield <= 100 && p.shield >= 0, 1);
    ck("stale anchor recovers", p.shield, 100);

    /* ---- the spawn reset ---- */
    hit(&p, 0, 5000);
    p.shield = 100;
    p.shield_regen_base = 100;
    p.shield_regen_at_ms = 5000;
    p.shield_regen_timer = 0;
    shield_regen_tick(&p, 6000);
    ck("spawn leaves a full bar", p.shield, 100);

    /* ---- a mod's curve through the host hook ---- */
    shield_regen_set_amount_hook(flat_one_hook);
    hit(&p, 0, 1000);
    shield_regen_tick(&p, 6000);
    ck("mod flat curve after 2 ticks", p.shield, 2);
    ck("mod next amount is flat", shield_regen_next_amount(&p, 6000), 1);
    shield_regen_tick(&p, 16000);
    ck("mod flat curve after 12 ticks", p.shield, 12);

    /* a mod with no opinion falls through to the ramp */
    shield_regen_set_amount_hook(no_opinion_hook);
    hit(&p, 0, 1000);
    shield_regen_tick(&p, 9000);
    ck("no-opinion mod gets the ramp", p.shield, 15);

    /* a mod repeating the rule's own curve agrees with the closed form exactly -- checked BELOW the
     * clamp (1 point and 45 points), because comparing two bars of 100 would pass for the wrong reason */
    shield_regen_set_amount_hook(rule_curve_hook);
    hit(&p, 0, 20000);
    shield_regen_tick(&p, 24000);
    ck("hook path equals closed form (1 tick)", p.shield, sh_shield_at(0, 4000));
    shield_regen_tick(&p, 29000);
    ck("hook path equals closed form (6 ticks)", p.shield, sh_shield_at(0, 9000));
    shield_regen_set_amount_hook(NULL);
    shield_regen_tick(&p, 35000);
    ck("uninstalled hook is the rule", p.shield, sh_shield_at(0, 15000));

    printf(fails ? "shield_regen host: %d FAILED\n" : "shield_regen host: all checks ok\n", fails);
    return fails ? 1 : 0;
}
