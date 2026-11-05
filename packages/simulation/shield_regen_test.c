/* shield_regen_test.c -- the host half of shield regen, run against the REAL generated rule.
 *
 * Not a mock: shield_regen_rules.c is the checked-in output of the PARENA module, and this links it
 * exactly as the lobby and the server do. Every number below is a statement a player can feel --
 * the 4 seconds, the first point at 4.000 s, the ramp, the restart on a hit, and the ceiling.
 *
 *   make test-shield-regen
 */

#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "shield_regen.h"

static int checks = 0;
static int failures = 0;

static void ok(int cond, const char *what) {
    checks++;
    if (!cond) {
        failures++;
        printf("FAIL: %s\n", what);
    }
}

static PlayerState fresh(int shield, unsigned int now_ms) {
    PlayerState p;
    memset(&p, 0, sizeof(p));
    p.active = 1;
    p.state = STATE_ALIVE;
    p.shield = shield;
    p.shield_regen_base = shield;
    p.shield_regen_at_ms = now_ms;
    return p;
}

static int flat_five(const PlayerState *p, int tick_index) {
    (void)p; (void)tick_index;
    return 5;
}

static int no_opinion(const PlayerState *p, int tick_index) {
    (void)p; (void)tick_index;
    return 0;
}

int main(void) {
    PlayerState p;
    int i, sum;

    /* The rule's own numbers, read through the generated C, not restated here. */
    ok(shield_max() == 100, "shield tops out at 100");
    ok(shield_regen_delay_ms() == 4000, "the vulnerable window is four seconds");
    ok(shield_regen_interval_ms() == 1000, "a regen tick is one second");
    ok(shield_regen_ramp_cap() == 10, "the ramp caps at ten points a tick");
    ok(sh_regen_delay_ticks(16) == 250, "4000 ms rounds UP to 250 sixteen-ms ticks");
    ok(sh_regen_interval_ticks(16) == 63, "1000 ms rounds UP to 63 sixteen-ms ticks");

    /* A shield broken at t=1000 stays broken for the full four seconds. */
    p = fresh(0, 1000);
    ok(shield_regen_waiting(&p, 1000) == 1, "the body is vulnerable at the hit");
    ok(shield_regen_counter_tenths(&p, 1000) == 40, "the HUD starts the counter at 4.0");
    ok(shield_regen_counter_tenths(&p, 2500) == 25, "the counter reads 2.5 halfway");
    ok(shield_regen_counter_tenths(&p, 4999) == 1, "the last millisecond still shows 0.1");

    shield_regen_tick(&p, 4999);
    ok(p.shield == 0, "not one point before 4000 ms");
    ok(shield_regen_waiting(&p, 4999) == 1, "still waiting at 3999 ms");

    shield_regen_tick(&p, 5000);
    ok(p.shield == 1, "exactly one point at 4000 ms");
    ok(shield_regen_waiting(&p, 5000) == 0, "the window is over at 4000 ms");
    ok(shield_regen_counter_ms(&p, 5000) == 0, "the counter is spent when the ticks start");
    ok(shield_regen_next_amount(&p, 5000) == 2, "the next tick is worth 2");

    /* The ramp: 1, 2, 3, 4 -> 10 points at 3 s of healing. */
    shield_regen_tick(&p, 6000);
    ok(p.shield == 3, "1 + 2 = 3 after two ticks");
    shield_regen_tick(&p, 7000);
    ok(p.shield == 6, "1 + 2 + 3 = 6 after three ticks");
    shield_regen_tick(&p, 8000);
    ok(p.shield == 10, "1 + 2 + 3 + 4 = 10 after four ticks");

    /* Re-ticking at the same instant must not pay twice: the answer is elapsed time, not call count. */
    shield_regen_tick(&p, 8000);
    shield_regen_tick(&p, 8000);
    ok(p.shield == 10, "a duplicated tick pays nothing");

    /* A hit during the window restarts it -- with the shield the hit left behind. */
    p.shield = 10;
    shield_regen_note_damage(&p, 8000);
    ok(p.shield_regen_base == 10 && p.shield_regen_at_ms == 8000, "a hit re-snapshots the pair");
    shield_regen_tick(&p, 8000);
    ok(p.shield == 10, "a hit inside the window takes the regen back to zero");
    shield_regen_tick(&p, 11999);
    ok(p.shield == 10, "the restarted window holds for its own four seconds");
    shield_regen_tick(&p, 12000);
    ok(p.shield == 11, "and pays its first point one second later, exactly");

    /* A hit while the shield is already up still restarts: the counter is time since damage. */
    p = fresh(0, 0);
    shield_regen_tick(&p, 13000);
    ok(p.shield == 55, "ten ticks of the ramp give 55 points");
    p.shield = 55;
    shield_regen_note_damage(&p, 13000);
    shield_regen_tick(&p, 14000);
    ok(p.shield == 55, "landing a hit after regen started restarts the wait, not the regen");

    /* The closed form and the per-tick curve must agree everywhere, or a HUD and the shield drift. */
    for (i = 0; i <= 25; i++) {
        int landed = sh_regen_ticks_landed(4000 + i * 1000);
        int cap = shield_regen_ramp_cap();
        int k;
        sum = 0;
        for (k = 0; k < landed && k < cap; k++) sum += sh_regen_tick_amount(k);
        if (landed > cap) sum += (landed - cap) * sh_regen_tick_amount(cap);
        if (sum != sh_regen_total(4000 + i * 1000)) {
            printf("FAIL: closed form %d != curve %d at %d ticks\n",
                   sh_regen_total(4000 + i * 1000), sum, landed);
            failures++;
            break;
        }
        checks++;
    }

    /* Never more than the bar, however long the body is left alone. */
    p = fresh(0, 0);
    shield_regen_tick(&p, 1000000);
    ok(p.shield == 100, "a stale anchor cannot overflow the bar");
    p = fresh(0, 0);
    shield_regen_tick(&p, 600000 + 4000);
    ok(p.shield == 100, "the elapsed time is capped at ten minutes, still a full bar");
    ok(shield_regen_bar_permille(&p) == 1000, "a full shield is 1000 permille");

    /* A body already at the bar only refreshes its anchor -- the next hit gets its own four seconds. */
    p = fresh(100, 0);
    shield_regen_tick(&p, 50000);
    ok(p.shield == 100, "a full shield stays full");
    ok(p.shield_regen_at_ms == 50000 && p.shield_regen_base == 100, "and its anchor moves forward");
    p.shield = 40;
    shield_regen_note_damage(&p, 51000);
    shield_regen_tick(&p, 54000);
    ok(p.shield == 40, "a new hit after a full bar gets a real four-second wait");

    /* Adopting a snapshot mid-heal must not pay the ticks the server has already paid: the baseline is
     * derived FROM the wire shield, not set to it. */
    memset(&p, 0, sizeof(p));
    p.active = 1; p.state = STATE_ALIVE;
    shield_regen_adopt_clock(&p, 5000, 43, 5000);
    /* At 5000 ms two ticks have landed (1 + 2 = 3 points), so 43 - 3 = 40. */
    ok(p.shield_regen_base == 40, "the baseline backs the landed ticks out of the wire shield");
    p.shield = 43;
    shield_regen_tick(&p, 5000);
    ok(p.shield == 43, "the local tick agrees with the wire instead of adding the point twice");

    /* Snapshot interpolation: between packets the client's own clock keeps the countdown moving, and a
     * packet only re-phases it when it has really drifted -- otherwise every snapshot would reset the
     * countdown by its own arrival jitter and the number would stick and step backwards on screen. */
    memset(&p, 0, sizeof(p));
    p.active = 1; p.state = STATE_ALIVE;
    shield_regen_adopt_clock(&p, 1000, 0, 1000);
    ok(shield_regen_since_ms(&p, 1000) == 1000, "adopt anchors on the wire's own elapsed time");
    ok(shield_regen_counter_tenths(&p, 1000) == 30, "3.0 seconds left when the clock is adopted");
    ok(shield_regen_counter_tenths(&p, 1500) == 25, "0.5 s later, with NO packet, it reads 2.5");
    {
        unsigned int anchor = p.shield_regen_at_ms;
        shield_regen_adopt_clock(&p, 1530, 0, 1600);          /* 70 ms of packet jitter */
        ok(p.shield_regen_at_ms == anchor, "jitter inside the threshold leaves the anchor alone");
        ok(shield_regen_counter_tenths(&p, 1600) == 24, "so the countdown never steps backwards");
        shield_regen_adopt_clock(&p, 2000, 1, 3000);          /* 1000 ms of drift: lost packets */
        ok(shield_regen_since_ms(&p, 3000) == 2000, "a drifted clock resyncs to the server");
        ok(p.shield == 0, "adopting a clock never moves the shield by itself");
        shield_regen_tick(&p, 3000);
        ok(p.shield == 1, "the next local tick lands on exactly the wire's shield");
    }
    /* A fresh hit (the wire clock back to zero) restarts the counter at 4.0, whatever the old anchor
     * was -- the client must not keep interpolating through a hit. */
    shield_regen_adopt_clock(&p, 0, 0, 9000);
    ok(shield_regen_counter_tenths(&p, 9000) == 40, "a hit resets the counter to 4.0 on arrival");

    /* ── A lossy link, end to end ─────────────────────────────────────────────────────────
     * Both sides run the real host half on the same rule. The server is the authority and
     * snapshots at 20 Hz; the client receives them with up to 120 ms of jitter and 30% of them
     * dropped. Because the clock interpolates, the claims under test are narrow and real: the
     * countdown never runs BACKWARDS, and the client's bar never gets more than one tick ahead of
     * (or behind) the authority's. */
    {
        enum { SNAP_MS = 50, MAX_PEND = 512 };
        PlayerState srv, cli;
        int pend_t[MAX_PEND], pend_since[MAX_PEND], pend_shield[MAX_PEND];
        int n_pend = 0, have_clock = 0;
        int last_tenths = 40, backwards = 0, max_lead = 0, max_lag = 0;
        unsigned int rng = 0x5EED1234u, t;

        memset(&srv, 0, sizeof(srv));
        srv.active = 1; srv.state = STATE_ALIVE;          /* broken at t=0, the whole run */
        memset(&cli, 0, sizeof(cli));
        cli.active = 1; cli.state = STATE_ALIVE;

        for (t = 0; t <= 6000; t++) {
            int i;
            shield_regen_tick(&srv, t);
            if (have_clock) shield_regen_tick(&cli, t);

            if (t % SNAP_MS == 0) {
                rng = rng * 1664525u + 1013904223u;
                if ((rng % 100u) >= 30u && n_pend < MAX_PEND) {      /* 30% lost */
                    rng = rng * 1664525u + 1013904223u;
                    pend_t[n_pend] = (int)t + (int)(rng % 121u);     /* 0..120 ms late */
                    pend_since[n_pend] = shield_regen_since_ms(&srv, t);
                    pend_shield[n_pend] = srv.shield;
                    n_pend++;
                }
            }

            for (i = 0; i < n_pend; ) {
                if (pend_t[i] > (int)t) { i++; continue; }
                shield_regen_adopt_clock(&cli, pend_since[i], pend_shield[i], t);
                cli.shield = pend_shield[i];       /* the client trusts the bar on arrival, as main.c does */
                have_clock = 1;
                pend_t[i] = pend_t[n_pend - 1];
                pend_since[i] = pend_since[n_pend - 1];
                pend_shield[i] = pend_shield[n_pend - 1];
                n_pend--;
            }

            if (have_clock) {
                int lead = (int)cli.shield - (int)srv.shield;
                int tenths = shield_regen_counter_tenths(&cli, t);
                if (tenths > last_tenths) backwards = 1;
                last_tenths = tenths;
                if (lead > max_lead) max_lead = lead;
                if (-lead > max_lag) max_lag = -lead;
            }
        }
        ok(backwards == 0, "on a 30%-loss, 120 ms-jitter link the countdown never steps backwards");
        ok(max_lead <= shield_regen_ramp_cap(), "the client never leads the authority by more than one tick");
        ok(max_lag <= shield_regen_ramp_cap(), "nor lags it by more than one tick");
        ok(have_clock == 1, "the simulation actually delivered packets");
    }

    /* A hit that lands entirely inside a blackout: the client's anchor must jump, not interpolate
     * through it. 5 s of a stale anchor against a 1.2 s-old hit is exactly the case where keeping
     * the local clock would leave the player believing the shield is about to come back when it is
     * really four fresh seconds away. */
    {
        PlayerState cli;
        memset(&cli, 0, sizeof(cli));
        cli.active = 1; cli.state = STATE_ALIVE;
        shield_regen_adopt_clock(&cli, 5000, 55, 5000);
        ok(shield_regen_counter_tenths(&cli, 5000) == 0, "the stale clock had already started ticking");
        /* ... no packets ... a hit at 7800, and the first snapshot after the blackout at t=9000
         * still says only 1200 ms since that hit. */
        shield_regen_adopt_clock(&cli, 1200, 0, 9000);
        ok(shield_regen_since_ms(&cli, 9000) == 1200, "a hit hidden in a blackout snaps the anchor");
        ok(shield_regen_counter_tenths(&cli, 9000) == 28, "and the countdown restarts from the hit's own clock");
        ok(shield_regen_waiting(&cli, 9000) == 1, "the body is vulnerable again, as the authority says");
    }

    /* The mod seam: a body may bring its own curve; a mod with no opinion falls back to the rule. */
    p = fresh(0, 0);
    shield_regen_set_amount_hook(flat_five);
    shield_regen_tick(&p, 13000);
    ok(p.shield == 50, "a mod's flat five pays 5 a tick (10 ticks)");
    ok(shield_regen_next_amount(&p, 13000) == 5, "the HUD shows the mod's number, not the rule's");
    shield_regen_set_amount_hook(no_opinion);
    p = fresh(0, 0);
    shield_regen_tick(&p, 13000);
    ok(p.shield == 55, "a mod with no opinion falls back to the ramp");
    shield_regen_set_amount_hook(NULL);
    p = fresh(0, 0);
    shield_regen_tick(&p, 13000);
    ok(p.shield == 55, "and no mod at all is the same answer");

    /* Respawn: a body brought back at full shield must not inherit the last death's clock. */
    p = fresh(0, 0);
    shield_regen_note_damage(&p, 0);
    p.shield = 100;
    shield_regen_tick(&p, 100000);
    ok(p.shield == 100 && p.shield_regen_base == 100, "respawn's full bar re-bases the anchor");

    if (failures == 0) {
        printf("shield_regen host: all checks ok (%d)\n", checks);
        return 0;
    }
    printf("shield_regen host: %d of %d checks FAILED\n", failures, checks);
    return 1;
}
