/* vendor_serve_test.c -- standing at the window until a cone is handed over, headless.
 *
 * The song draws you to the truck; this is the other half, and the half that is easy to get wrong: a hold
 * that banks progress after you walk away, a cooldown that never expires, a truck that serves you while it is
 * rolling past at arm's length, a cone that heals past the health ceiling. Every one of those is checked here
 * against PARENA's own numbers, so a change to the rules changes the expectations rather than breaking them.
 *
 * NOT proven: that standing at a truck window for 900 ms is the right amount of fun. That needs players.
 */
#include <stdio.h>
#include <string.h>

#include "vendor_serve.h"

static int g_failed, g_checks;
#define CHECK(cond, ...) do { \
    g_checks++; \
    if (!(cond)) { g_failed++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

int main(void) {
    printf("== vendor serve ==\n");
    printf("      window %d units, hold %d ms, heal %d, cooldown %d ms, %d jingles\n",
           icecream_window_units(), icecream_serve_ms(), icecream_serve_heal(),
           icecream_serve_cooldown_ms(), icecream_jingle_count());

    /* ---- the window has to be close, and shut while the truck is moving ---- */
    {
        CHECK(icecream_serve_open(0, 0) == 1, "a stopped truck at arm's length is not serving");
        CHECK(icecream_serve_open(icecream_window_units(), 0) == 1, "the edge of the window is not the window");
        CHECK(icecream_serve_open(icecream_window_units() + 1, 0) == 0, "a truck across the road served somebody");
        CHECK(icecream_serve_open(0, 400) == 0, "a truck rolling past at arm's length served somebody");
        CHECK(icecream_serve_open(0, 15) == 1, "a truck creeping at 15/1000 was treated as driving");
        printf("PASS: the window opens for a STOPPED truck inside %d units and nowhere else\n",
               icecream_window_units());
    }

    /* ---- progress is a hold, and walking away throws it away ---- */
    {
        VendorServe v;
        vendor_serve_init(&v);
        int dt = icecream_serve_ms() / 4;
        int served = 0;
        for (int i = 0; i < 3; i++) served += vendor_serve_tick(&v, 2, 1, 0, dt);
        int mid = vendor_serve_progress(&v);
        printf("      three quarters of the hold: %d/1000, served %d\n", mid, served);
        CHECK(served == 0, "a cone was handed over before the hold finished");
        CHECK(mid >= 700 && mid < 1000, "three quarters of the hold reads %d/1000", mid);

        vendor_serve_tick(&v, VENDOR_SERVE_NONE, 999, 0, dt);   /* walked away */
        CHECK(vendor_serve_progress(&v) == 0, "walking away left %d/1000 banked", vendor_serve_progress(&v));
        CHECK(v.held_ms == 0, "the hold was not reset (%d ms)", v.held_ms);
        printf("PASS: the hold never banks progress after the buyer leaves\n");
    }

    /* ---- exactly one cone per hold, then the cooldown ---- */
    {
        VendorServe v;
        vendor_serve_init(&v);
        int served = 0, ticks = 0;
        while (ticks < 4000 && served == 0) { served += vendor_serve_tick(&v, 0, 2, 0, 50); ticks += 50; }
        printf("      first cone after %d ms (hold is %d ms)\n", ticks, icecream_serve_ms());
        CHECK(served == 1, "no cone was handed over after the full hold");
        CHECK(v.served_count == 1, "the count says %d cones", v.served_count);
        CHECK(ticks >= icecream_serve_ms(), "a cone was handed over after only %d ms of the hold", ticks);

        /* standing there through the whole cooldown must hand over exactly nothing */
        int during = 0;
        for (int t = 0; t < icecream_serve_cooldown_ms() - 100; t += 100)
            during += vendor_serve_tick(&v, 0, 2, 0, 100);
        CHECK(during == 0, "%d cone(s) were sold inside the cooldown", during);
        CHECK(v.served_count == 1, "a cooldown sale changed the count to %d", v.served_count);

        /* and once it expires, the next full hold serves again */
        int after = 0;
        for (int t = 0; t < icecream_serve_ms() + 200; t += 100)
            after += vendor_serve_tick(&v, 0, 2, 0, 100);
        printf("      after the cooldown: %d more cone(s), count %d\n", after, v.served_count);
        CHECK(after == 1, "the window never reopened after its cooldown (%d cones)", after);
        CHECK(v.served_count == 2, "the second sale did not count (%d)", v.served_count);
        printf("PASS: one cone per hold, none during the cooldown, one more afterwards\n");
    }

    /* ---- a second truck is a second queue ---- */
    {
        VendorServe v;
        vendor_serve_init(&v);
        int dt = icecream_serve_ms() / 2 + 1;
        vendor_serve_tick(&v, 0, 2, 0, dt);
        int before = vendor_serve_progress(&v);
        vendor_serve_tick(&v, 1, 2, 0, 10);      /* a different truck */
        printf("      switching trucks: %d/1000 -> %d/1000\n", before, vendor_serve_progress(&v));
        CHECK(before > 0, "the first hold made no progress at all");
        CHECK(vendor_serve_progress(&v) < before, "the hold carried over to a different truck");
        printf("PASS: a buyer's progress belongs to one truck's window\n");
    }

    /* ---- a cone heals, and never past the ceiling ---- */
    {
        CHECK(icecream_serve_heal() > 0, "a cone is worth %d health", icecream_serve_heal());
        int healed = vendor_serve_heal(50, 100);
        CHECK(healed == 50 + icecream_serve_heal(), "a cone at half health gave %d, not %d", healed,
              50 + icecream_serve_heal());
        CHECK(vendor_serve_heal(100, 100) == 100, "a cone at full health pushed it to %d",
              vendor_serve_heal(100, 100));
        CHECK(vendor_serve_heal(-5, 100) == icecream_serve_heal(), "negative health healed to %d",
              vendor_serve_heal(-5, 100));
        CHECK(vendor_serve_heal(90, 100) == 100, "a cone near the ceiling overflowed to %d",
              vendor_serve_heal(90, 100));
        printf("PASS: a cone is %d health and the player's ceiling holds\n", icecream_serve_heal());
    }

    /* ---- the truck has to pull over, and pulling over is a rule on the town clock ---- */
    {
        int cycle = icecream_stop_cycle_ms(), dwell = icecream_dwell_ms();
        printf("      vendor parks %d ms in every %d ms cycle\n", dwell, cycle);
        CHECK(cycle > dwell, "the park window (%d ms) does not fit its cycle (%d ms)", dwell, cycle);
        CHECK(dwell > 0, "a vendor parks for %d ms", dwell);
        CHECK(icecream_parked(0) == 1, "a vendor was already driving at t=0");
        CHECK(icecream_parked(dwell - 1) == 1, "the vendor left a millisecond before its dwell was up");
        CHECK(icecream_parked(dwell) == 0, "the vendor was still parked past its dwell");
        CHECK(icecream_parked(cycle - 1) == 0, "the vendor parked through its whole cycle");
        CHECK(icecream_parked(cycle) == 1, "the stop window did not come round again");
        CHECK(icecream_parked(cycle + dwell) == 0, "the second cycle's window never closed");
        CHECK(icecream_parked(-5) == icecream_parked(0), "a negative clock time parked the truck differently");
        printf("PASS: the vendor pulls over for %d ms of every %d ms, on the clock every host shares\n",
               dwell, cycle);

        /* a PARKED truck inside the window really serves; the same truck rolling does not */
        VendorServe v;
        vendor_serve_init(&v);
        int served = 0;
        for (int t = 0; t < icecream_serve_ms() + 200; t += 100)
            served += vendor_serve_tick(&v, 0, 2, 0, 100);      /* stopped: speed_pm 0 */
        CHECK(served == 1, "a stopped vendor handed over %d cone(s), not one", served);
        printf("PASS: a parked vendor is the one that can sell\n");
    }

    printf("\n%s%d check(s) failed\n", g_failed ? "FAIL: " : "PASS: ", g_failed);
    return g_failed ? 1 : 0;
}
