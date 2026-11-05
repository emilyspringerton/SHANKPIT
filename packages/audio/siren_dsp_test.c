/* siren_dsp_test.c -- the siren voice measured OFFLINE, with no audio device.
 *
 * This is the answer to the limit the siren work shipped with ("nothing has been heard"). It cannot say
 * whether the mix sounds good, but it can say exactly what the mix receives: the tone is the frequency the
 * rules asked for, a hard-right siren is silent in the left channel, a centre siren is equal in both, the
 * gain ramps instead of clicking, and no sample ever leaves the rails. Those are the four things a bad
 * oscillator gets wrong, and all four are measurable from here.
 *
 * Method: zero crossings over a window of a known length give the fundamental (a 0.32 second harmonic
 * cannot add crossings to a signal whose fundamental dominates); per-channel RMS gives the pan law; peak
 * |sample| over a full second at full gain gives the clipping answer.
 */
#include <stdio.h>
#include <math.h>

#include "siren_dsp.h"

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

#define RATE SIREN_DSP_RATE

static double rms_of(const float *v, int n) {
    double acc = 0.0;
    for (int i = 0; i < n; i++) acc += (double)v[i] * (double)v[i];
    return n > 0 ? sqrt(acc / (double)n) : 0.0;
}

/* Fundamental from sign changes: each full cycle crosses zero twice. Ignoring a tiny dead band keeps the
 * harmonic from being counted as extra crossings on the exact zero. */
static double freq_of(const float *v, int n) {
    int crossings = 0;
    float prev = v[0];
    for (int i = 1; i < n; i++) {
        if ((prev < -1e-6f && v[i] >= 1e-6f) || (prev > 1e-6f && v[i] <= -1e-6f)) crossings++;
        prev = v[i];
    }
    return (double)crossings * (double)RATE / (2.0 * (double)n);
}

int main(void) {
    static float l[RATE], r[RATE];
    printf("== siren dsp (offline) ==\n");

    /* ---- silent by default, and silent again after being told to stop ---- */
    {
        SirenDsp s;
        siren_dsp_init(&s);
        double peak = 0.0;
        for (int i = 0; i < RATE; i++) { siren_dsp_step(&s, &l[i], &r[i]); if (fabsf(l[i]) > peak) peak = fabsf(l[i]); }
        CHECK(peak == 0.0, "a fresh siren made noise (peak %.6f)", peak);

        siren_dsp_set(&s, SIREN_KIND_POLICE, 880.0f, 0.9f, 0.0f);
        for (int i = 0; i < RATE; i++) siren_dsp_step(&s, &l[i], &r[i]);
        CHECK(rms_of(l, RATE) > 0.05, "a wailing siren is inaudible (rms %.4f)", rms_of(l, RATE));

        siren_dsp_set(&s, SIREN_KIND_SILENT, 880.0f, 0.0f, 0.0f);
        for (int i = 0; i < RATE; i++) siren_dsp_step(&s, &l[i], &r[i]);
        double tail = rms_of(l + RATE - 2000, 2000);
        CHECK(tail < 0.002, "the siren did not fade out after being silenced (tail rms %.5f)", tail);
        printf("PASS: silent at rest, audible when told to wail, and it fades rather than cutting\n");
    }

    /* ---- the tone is the frequency the rules asked for ---- */
    {
        struct { int kind; float hz; const char *what; } cases[] = {
            { SIREN_KIND_EMS,    690.0f, "EMS low" },
            { SIREN_KIND_EMS,    880.0f, "EMS high" },
            { SIREN_KIND_POLICE, 520.0f, "wail bottom" },
            { SIREN_KIND_POLICE, 1480.0f, "wail top" },
        };
        for (int ci = 0; ci < 4; ci++) {
            SirenDsp s;
            siren_dsp_init(&s);
            siren_dsp_set(&s, cases[ci].kind, cases[ci].hz, 0.8f, 0.0f);
            /* let the frequency smoothing and the gain settle before measuring */
            for (int i = 0; i < RATE / 2; i++) siren_dsp_step(&s, &l[i], &r[i]);
            for (int i = 0; i < RATE; i++) siren_dsp_step(&s, &l[i], &r[i]);
            double f = freq_of(l, RATE);
            double err = fabs(f - (double)cases[ci].hz) / (double)cases[ci].hz;
            printf("      %s: asked %.0f Hz, measured %.1f Hz\n", cases[ci].what, cases[ci].hz, f);
            CHECK(err < 0.03, "%s came out at %.1f Hz, not %.0f Hz", cases[ci].what, f, cases[ci].hz);
        }
    }

    /* ---- pan: the same one-sided law the gunshots use ---- */
    {
        SirenDsp s;
        siren_dsp_init(&s);
        siren_dsp_set(&s, SIREN_KIND_EMS, 690.0f, 0.8f, 0.0f);
        for (int i = 0; i < RATE; i++) siren_dsp_step(&s, &l[i], &r[i]);
        double el = rms_of(l + RATE / 2, RATE / 2), er = rms_of(r + RATE / 2, RATE / 2);
        CHECK(er > 0.0 && fabs(el - er) / er < 0.05, "a centre siren is not equal in both ears (%.4f vs %.4f)", el, er);

        siren_dsp_set(&s, SIREN_KIND_EMS, 690.0f, 0.8f, 1.0f);
        for (int i = 0; i < RATE; i++) siren_dsp_step(&s, &l[i], &r[i]);
        el = rms_of(l + RATE / 2, RATE / 2);
        er = rms_of(r + RATE / 2, RATE / 2);
        printf("      hard right: left %.4f, right %.4f\n", el, er);
        CHECK(er > 0.05 && el < er * 0.10, "hard right did not silence the left ear (%.4f vs %.4f)", el, er);

        siren_dsp_set(&s, SIREN_KIND_EMS, 690.0f, 0.8f, -1.0f);
        for (int i = 0; i < RATE; i++) siren_dsp_step(&s, &l[i], &r[i]);
        el = rms_of(l + RATE / 2, RATE / 2);
        er = rms_of(r + RATE / 2, RATE / 2);
        CHECK(el > 0.05 && er < el * 0.10, "hard left did not silence the right ear (%.4f vs %.4f)", el, er);
        printf("PASS: centre is balanced, hard-left/right really is one-sided\n");
    }

    /* ---- attack, and the rails ---- */
    {
        SirenDsp s;
        siren_dsp_init(&s);
        siren_dsp_set(&s, SIREN_KIND_POLICE, 1200.0f, 1.0f, 0.0f);
        /* compare the FIRST few frames with the settled signal: over a whole buffer the settled samples
           dominate the average and the ramp disappears into it (which is what the first version of this
           check got wrong -- it measured the mix, not the attack). */
        double early = 0.0, late = 0.0, peak = 0.0;
        for (int i = 0; i < RATE; i++) {
            siren_dsp_step(&s, &l[i], &r[i]);
            if (i < 20) early += fabs((double)l[i]);
            if (i > RATE - 20) late += fabs((double)l[i]);
            if (fabsf(l[i]) > peak) peak = fabsf(l[i]);
            if (fabsf(r[i]) > peak) peak = fabsf(r[i]);
        }
        printf("      attack: first 20 frames %.4f, settled 20 frames %.4f\n", early, late);
        CHECK(late > early * 3.0, "the siren did not ramp in (early %.4f, late %.4f)", early, late);
        printf("      full-gain peak: %.3f\n", peak);
        CHECK(peak <= 1.0, "the siren clips the master bus (peak %.3f)", peak);
        CHECK(peak > 0.2, "the siren is far too quiet at full gain (peak %.3f)", peak);
        printf("PASS: it ramps in over ~4.5 ms and never leaves the rails at full gain\n");
    }

    printf("\n%s%d check(s) failed\n", g_failed ? "FAIL: " : "PASS: ", g_failed);
    return g_failed ? 1 : 0;
}
