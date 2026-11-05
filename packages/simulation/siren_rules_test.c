/* siren_rules_test.c -- the siren curves, headless (no audio device, no GL, no clock).
 *
 * What this proves: the two sirens are the sirens they claim to be (an EMS hi-lo really alternates two
 * tones at 0.9 s each; a police wail really sweeps 520..1480 Hz and comes back), they cannot be asked for
 * a frequency when there is nothing to sound, the gain falls off slowly enough to carry and fast enough to
 * have a direction, the pan wraps the short way, and a siren only sounds on the three triggers the founder
 * named (boost, lift, and a moving unmanned cruiser).
 *
 * NOT proven here: what any of it sounds like. This box has no audio device; the mix is unverified.
 */
#include <stdio.h>
#include <math.h>

#include "siren_rules.h"

static int g_failed;
static int g_checks;

#define CHECK(cond, ...) do { \
    g_checks++; \
    if (!(cond)) { \
        g_failed++; \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

int main(void) {
    printf("== siren rules ==\n");

    /* ---- the kind numbers the C header promises match the generated module ---- */
    CHECK(siren_kind_silent() == SIREN_KIND_SILENT, "module says silent=%d, header says %d",
          siren_kind_silent(), SIREN_KIND_SILENT);
    CHECK(siren_kind_ems() == SIREN_KIND_EMS, "module says ems=%d, header says %d",
          siren_kind_ems(), SIREN_KIND_EMS);
    CHECK(siren_kind_police() == SIREN_KIND_POLICE, "module says police=%d, header says %d",
          siren_kind_police(), SIREN_KIND_POLICE);

    /* ---- EMS: two tones, 0.9 s each, and nothing else, ever ---- */
    {
        int lo = siren_hz_milli(SIREN_KIND_EMS, 0);
        int hi = siren_hz_milli(SIREN_KIND_EMS, 900);
        int again = siren_hz_milli(SIREN_KIND_EMS, 1800);
        CHECK(lo == siren_ems_low_milli(), "EMS tone A is %d mHz, the rule says %d", lo, siren_ems_low_milli());
        CHECK(hi == siren_ems_high_milli(), "EMS tone B is %d mHz, the rule says %d", hi, siren_ems_high_milli());
        CHECK(hi > lo, "the EMS hi-lo does not actually go hi then lo (%d -> %d)", lo, hi);
        CHECK(again == lo, "the EMS two-tone does not repeat every 1.8 s (%d)", again);
        int flips = 0, prev = lo;
        for (int t = 0; t < 18000; t += 50) {
            int hz = siren_hz_milli(SIREN_KIND_EMS, t);
            CHECK(hz == lo || hz == hi, "EMS produced a third tone (%d mHz at %d ms)", hz, t);
            if (hz != prev) flips++;
            prev = hz;
        }
        /* ten seconds of a 1.8 s two-tone is 19 boundaries (the one at 18 000 ms is the first of the next
           ten, and the loop stops before it) */
        CHECK(flips == 19, "the EMS siren flipped %d times in ten seconds, not 19", flips);
        printf("PASS: EMS is a %d/%d mHz two-tone, 0.9 s a side, 10 s of it\n", lo, hi);
    }

    /* ---- police: a wail that sweeps up, comes back, and repeats after 4.2 s ---- */
    {
        int low = siren_police_low_milli(), high = siren_police_high_milli();
        CHECK(siren_hz_milli(SIREN_KIND_POLICE, 0) == low, "the wail does not start at its low note");
        int mid_up = siren_hz_milli(SIREN_KIND_POLICE, 1050);
        CHECK(mid_up > low && mid_up < high, "the wail does not sweep between its notes at 1.05 s (%d)", mid_up);
        CHECK(siren_hz_milli(SIREN_KIND_POLICE, 4200) == low, "the wail does not repeat every 4.2 s");
        int prev = siren_hz_milli(SIREN_KIND_POLICE, 0), peak = prev, trough = prev;
        int rises = 0, falls = 0;
        for (int t = 1; t <= 4200; t++) {
            int hz = siren_hz_milli(SIREN_KIND_POLICE, t);
            if (hz > prev) rises++;
            if (hz < prev) falls++;
            if (hz > peak) peak = hz;
            if (hz < trough) trough = hz;
            CHECK(hz >= low && hz <= high, "the wail left its band: %d mHz at %d ms", hz, t);
            prev = hz;
        }
        CHECK(rises > 0 && falls > 0, "the wail only moved one way (%d up, %d down)", rises, falls);
        CHECK(peak == high, "the wail never reached its top note (%d vs %d)", peak, high);
        CHECK(trough == low, "the wail never reached its bottom note (%d vs %d)", trough, low);
        printf("PASS: police wail sweeps %d..%d mHz and back within 4.2 s\n", low, high);
    }

    /* ---- no kind, no tone (an unknown siren must not make a noise) ---- */
    {
        CHECK(siren_hz_milli(SIREN_KIND_SILENT, 1234) == 0, "the silent kind produced a tone");
        CHECK(siren_hz_milli(77, 1234) == 0, "an unknown siren kind produced a tone");
        CHECK(siren_hz_milli(SIREN_KIND_EMS, -5000) == siren_hz_milli(SIREN_KIND_EMS, 0),
              "a negative clock changed the EMS phase");
    }

    /* ---- distance: carries far, but still has a direction ---- */
    {
        int at0 = siren_gain_permille(0, 1000);
        int at600 = siren_gain_permille(600, 1000);
        int at2000 = siren_gain_permille(2000, 1000);
        int atfar = siren_gain_permille(9000, 1000);
        CHECK(at0 == 1000, "a siren on top of you is only %d permille", at0);
        CHECK(at600 > 600, "600 units away the siren has already dropped to %d -- it would not carry", at600);
        CHECK(at2000 < at600 && at2000 > 300, "2 km out the siren is %d (expected a quiet but real 300-600)",
              at2000);
        CHECK(atfar >= 250, "at the clamp the siren is inaudible (%d)", atfar);
        int prev = 1001;
        for (int d = 0; d <= 4000; d += 100) {
            int g = siren_gain_permille(d, 1000);
            CHECK(g <= prev, "the siren got LOUDER with distance at %d (%d after %d)", d, g, prev);
            prev = g;
        }
        CHECK(siren_gain_permille(300, 1000) > siren_gain_permille(300, 0),
              "a parked cruiser's siren is as loud as a moving one");
        for (int s = 0; s <= 1000; s += 250) {
            int g = siren_gain_permille(500, s);
            CHECK(g >= 0 && g <= 1000, "speed %d produced gain %d", s, g);
        }
        printf("PASS: siren gain %d near -> %d at 600 -> %d at 2 km, slower falloff than a gunshot\n",
               at0, at600, at2000);
    }

    /* ---- pan: relative, clamped, shortest way round ---- */
    {
        CHECK(siren_pan_permille(90, 90) == 0, "a siren dead ahead did not pan centre");
        CHECK(siren_pan_permille(180, 90) == 500, "a siren 90 deg right is not half-right");
        CHECK(siren_pan_permille(0, 90) == -500, "a siren 90 deg left is not half-left");
        int wrap = siren_pan_permille(10, 350);
        CHECK(wrap > -300 && wrap < 300, "a 20-degree wrap panned to %d -- it took the long way", wrap);
        for (int b = 0; b < 360; b += 15)
            CHECK(siren_pan_permille(b, 90) >= -1000 && siren_pan_permille(b, 90) <= 1000,
                  "pan out of range at bearing %d", b);
        printf("PASS: siren pan is relative, clamped and wraps (%d for 350 vs 10)\n", wrap);
    }

    /* ---- who sounds one: the founder's two keys, plus an unmanned cruiser that is moving ---- */
    {
        CHECK(siren_trigger(0, 1, 1, 1000) == 0, "a CIVILIAN car sounds a police siren");
        CHECK(siren_trigger(1, 1, 0, 0) == SIREN_KIND_POLICE, "boost did not trigger the police siren");
        CHECK(siren_trigger(1, 0, 1, 0) == SIREN_KIND_POLICE, "the hover key did not trigger the police siren");
        CHECK(siren_trigger(1, 0, 0, 0) == 0, "a parked police car wails with nobody touching it");
        CHECK(siren_trigger(1, 0, 0, 900) == SIREN_KIND_POLICE,
              "a moving unmanned cruiser does not announce itself");
        CHECK(siren_trigger(1, 0, 0, 40) == 0, "a cruiser creeping at 40 permille is wailing");
        CHECK(siren_trigger(1, 0, 0, 4000) == SIREN_KIND_POLICE,
              "an out-of-range speed reading silenced the siren instead of clamping");
        printf("PASS: boost, hover and a moving unmanned cruiser sound the siren; a civilian never does\n");
    }

    printf("\n%s%d check(s) failed\n", g_failed ? "FAIL: " : "PASS: ", g_failed);
    return g_failed ? 1 : 0;
}
