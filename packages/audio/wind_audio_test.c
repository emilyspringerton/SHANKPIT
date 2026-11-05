/* wind_audio_test.c -- PARENA's wind rules, headless (no audio device, no display).
 *
 * The bed itself (a looping filtered noise buffer, a brightness low-pass and a pan) needs a sound card,
 * so it is not asserted here. What IS asserted is every decision that makes the bed mean something: the
 * gust stays in range and repeats, speed rises with the storm, gain never gets loud enough to bury a
 * gunshot, brightness rises with wind and falls with fog, the pan wraps the short way round the listener,
 * and the fog thinning falls (but never to zero) as the air moves. If these curves are wrong, the storm
 * sounds like a still morning no matter how good the DSP is.
 *
 * Build/run: make test-wind-audio */
#include <stdio.h>

#include "wind_audio_rules.h"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

int main(void) {
    printf("== weather audio rules (PARENA/stdlib/shankpit/weather_audio_rules.prn) ==\n");

    /* ---- the gust is bounded, deterministic and actually moves ---- */
    {
        int lo = 1001, hi = -1, changed = 0, prev = weather_wind_gust_permille(0);
        for (int t = 0; t <= 120000; t += 250) {
            int g = weather_wind_gust_permille(t);
            if (g < lo) lo = g;
            if (g > hi) hi = g;
            if (g != prev) changed++;
            CHECK(g >= 0 && g <= 1000, "gust out of range at %d ms: %d", t, g);
            CHECK(g == weather_wind_gust_permille(t), "gust is not deterministic at %d ms", t);
            prev = g;
        }
        CHECK(hi - lo > 300, "the gust barely moves over two minutes (%d..%d) -- a storm would hiss flat", lo, hi);
        CHECK(changed > 100, "the gust changed only %d times in two minutes", changed);
        printf("PASS: gust stays in 0..1000, deterministic, and breathes (%d..%d)\n", lo, hi);
    }

    /* ---- speed: a breeze at rest, a gale in a storm, and the gust always adds ---- */
    {
        int calm = weather_wind_speed_permille(0, 0);
        int storm = weather_wind_speed_permille(1000, 0);
        CHECK(calm > 0, "a calm day has literally no wind (%d) -- the bed would be silent", calm);
        CHECK(storm > calm + 400, "a full storm (%d) is not much windier than calm (%d)", storm, calm);
        for (int s = 0; s <= 1000; s += 100) {
            int g0 = weather_wind_speed_permille(s, 0);
            int g1 = weather_wind_speed_permille(s, 1000);
            CHECK(g1 >= g0, "a full gust did not raise the wind at storm %d (%d -> %d)", s, g0, g1);
            CHECK(g0 >= 0 && g1 <= 1000, "speed out of range at storm %d: %d..%d", s, g0, g1);
        }
        printf("PASS: wind speed %d calm -> %d storm, gust always additive\n", calm, storm);
    }

    /* ---- gain: audible, and never loud enough to swallow a gunshot ---- */
    {
        int quiet = weather_wind_gain_permille(0, 0, 0);
        int gale = weather_wind_gain_permille(1000, 300, 1000);
        CHECK(quiet > 0, "the calm bed has no gain at all");
        CHECK(gale <= 620, "the wind bed can reach %d permille -- it would bury combat audio", gale);
        CHECK(gale > quiet, "a gale is not louder than a calm day (%d vs %d)", gale, quiet);
        int foggy = weather_wind_gain_permille(1000, 1000, 1000);
        CHECK(foggy <= gale, "dense fog did not trade gain for brightness (%d vs %d)", foggy, gale);
        printf("PASS: gain %d calm -> %d gale, ceiling 620, fog trades it down\n", quiet, gale);
    }

    /* ---- brightness: wind opens it, fog closes it, and it never disappears ---- */
    {
        int dull = weather_wind_bright_permille(0, 0);
        int bright = weather_wind_bright_permille(1000, 0);
        int fogged = weather_wind_bright_permille(1000, 1000);
        CHECK(dull > 0, "a calm bed has no top end at all (%d)", dull);
        CHECK(bright > dull + 400, "wind did not brighten the bed (%d -> %d)", dull, bright);
        CHECK(fogged < bright, "fog did not dull the same wind (%d vs %d)", fogged, bright);
        CHECK(fogged >= 140, "fog drove brightness below the floor (%d)", fogged);
        int prev = -1;
        for (int v = 0; v <= 1000; v += 50) {
            int b = weather_wind_bright_permille(v, 0);
            CHECK(b >= prev, "brightness fell as the wind rose (at %d: %d after %d)", v, b, prev);
            prev = b;
        }
        printf("PASS: brightness %d calm -> %d wind, %d through fog, monotone\n", dull, bright, fogged);
    }

    /* ---- the pan wraps the short way, so 350 vs 10 is a nudge, not a spin ---- */
    {
        CHECK(weather_wind_pan_permille(90, 90) == 0, "wind from dead ahead did not pan centre");
        CHECK(weather_wind_pan_permille(180, 90) == 500, "wind 90 degrees right is not half-right (linear pan)");
        CHECK(weather_wind_pan_permille(0, 90) == -500, "wind 90 degrees left is not half-left (linear pan)");
        /* Exactly 180 degrees off is the one ambiguous case -- -180 and +180 are the same bearing, so the
           rule picks a side; what has to hold is that it is FULL pan and that the mirror image agrees. */
        int behind = weather_wind_pan_permille(270, 90);
        CHECK(behind == 1000 || behind == -1000, "wind 180 degrees off is not hard-panned (%d)", behind);
        /* Both ways round the circle land on the same side at exactly 180 (rel is -180 either way); what
           matters is that the answer is FULL pan both times, not a near-centre drift. */
        int behind_other = weather_wind_pan_permille(90, 270);
        CHECK(behind_other == behind, "the same 180-degree wind gives two answers (%d vs %d)",
              behind, behind_other);
        int wrap = weather_wind_pan_permille(10, 350);
        CHECK(wrap > -300 && wrap < 300, "a 20-degree wrap panned to %d -- it took the long way round", wrap);
        for (int d = 0; d < 360; d += 15) {
            int p = weather_wind_pan_permille(d, 90);
            CHECK(p >= -1000 && p <= 1000, "pan out of range at %d deg: %d", d, p);
        }
        printf("PASS: pan is relative, clamped, and wraps the short way (%d for 350 vs 10)\n", wrap);
    }

    /* ---- direction turns slowly and stays on the circle ---- */
    {
        int a = weather_wind_direction_deg(0), b = weather_wind_direction_deg(71000);
        CHECK(a == 0 && b == 0, "the wind direction did not complete one slow turn (%d, %d)", a, b);
        for (int t = 0; t < 200000; t += 1700) {
            int d = weather_wind_direction_deg(t);
            CHECK(d >= 0 && d < 360, "direction out of range at %d ms: %d", t, d);
        }
        printf("PASS: direction completes a 71 s turn and stays in 0..359\n");
    }

    /* ---- thunder: the sound arrives after the flash, and quieter the further the strike ---- */
    {
        int d0    = weather_thunder_delay_ms(0);
        int d300  = weather_thunder_delay_ms(300);
        int d1000 = weather_thunder_delay_ms(1000);
        CHECK(d0 == 0, "lightning on top of you still took %d ms to thunder", d0);
        CHECK(d300 >= 860 && d300 <= 890,
              "a strike 300 units out thundered after %d ms (sound is ~343 m/s)", d300);
        CHECK(d1000 > d300, "further lightning did not thunder later (%d vs %d)", d1000, d300);
        CHECK(weather_thunder_delay_ms(5000) == weather_thunder_delay_ms(2000),
              "the delay kept growing past the far clamp (%d)", weather_thunder_delay_ms(5000));
        int near = weather_thunder_gain_permille(200, 1000);
        int far  = weather_thunder_gain_permille(1200, 1000);
        CHECK(near > far, "a close strike is not louder than a distant one (%d vs %d)", near, far);
        CHECK(near <= 1000, "thunder is louder than full scale (%d)", near);
        CHECK(far >= 0, "distant thunder went negative (%d)", far);
        CHECK(weather_thunder_gain_permille(400, 1000) > weather_thunder_gain_permille(400, 100),
              "a storm's thunder is not louder than an overcast's");
        int prev = 1001;
        for (int d = 0; d <= 2000; d += 100) {
            int g = weather_thunder_gain_permille(d, 1000);
            CHECK(g <= prev, "thunder got louder further away (at %d: %d after %d)", d, g, prev);
            prev = g;
        }
        printf("PASS: a strike 1000 units out thunders %d ms later at %d permille\n",
               d1000, weather_thunder_gain_permille(1000, 1000));
    }

    /* ---- fog thinning: harder wind eats the scud, but weather is never deleted ---- */
    {
        int still = weather_wind_fog_thin_permille(0, 0);
        int gale = weather_wind_fog_thin_permille(1000, 1000);
        CHECK(still == 1000, "still air thinned the fog to %d", still);
        CHECK(gale < 700, "a gale only thinned the fog to %d -- the picture would disagree with the sound", gale);
        CHECK(gale >= 400, "a gale deleted the weather entirely (%d)", gale);
        int prev = 1001;
        for (int v = 0; v <= 1000; v += 100) {
            int th = weather_wind_fog_thin_permille(v, 1000);
            CHECK(th <= prev, "fog survived MORE as the wind rose (at %d: %d after %d)", v, th, prev);
            prev = th;
        }
        printf("PASS: fog thinning %d still -> %d gale, floored at 400\n", still, gale);
    }

    printf("%s: %d check(s) failed\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
