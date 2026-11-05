/* icecream_dsp_test.c -- the vendor's song, rendered offline against PARENA's own rules.
 *
 * This is the test that makes "the truck you hear before you see it" checkable on a box with no speaker: it
 * walks PARENA's own step lengths, finds where each note starts, and measures the pitch that actually came out
 * by zero crossings. If the DSP disagreed with the rules about the tune -- a wrong note, a stuck note, a note
 * that never ends -- the frequencies would not line up.
 *
 * NOT proven: that the melody is pleasant, or that a music box is the right instrument. That is a judgement
 * about timbre, and it still needs ears.
 */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#include "../simulation/icecream_rules.h"
#include "icecream_dsp.h"

static int g_failed, g_checks;
#define CHECK(cond, ...) do { \
    g_checks++; \
    if (!(cond)) { g_failed++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

#define RATE ICECREAM_DSP_RATE

int main(void) {
    printf("== ice cream song ==\n");
    int loop_ms = icecream_loop_ms();
    int frames = (int)((double)loop_ms * 2.0 * RATE / 1000.0) + RATE / 10;
    float *l = (float *)calloc((size_t)frames, sizeof(float));
    float *r = (float *)calloc((size_t)frames, sizeof(float));
    if (!l || !r) { printf("FAIL: out of memory\n"); return 1; }

    printf("      %d notes, %d ms per loop, %d frames rendered\n",
           icecream_note_count(), loop_ms, frames);

    /* (1) silence until a truck is out selling */
    {
        IceCreamDsp c;
        icecream_dsp_init(&c);
        float peak = 0.0f;
        for (int i = 0; i < RATE; i++) {
            icecream_dsp_step(&c, &l[i], &r[i]);
            if (fabsf(l[i]) > peak) peak = fabsf(l[i]);
        }
        CHECK(peak == 0.0f, "the truck sings with nothing playing (peak %.6f)", peak);
    }

    /* (2) the whole loop, rendered */
    {
        IceCreamDsp c;
        icecream_dsp_init(&c);
        icecream_dsp_set(&c, 1, 1.0f, 0.0f);
        float peak = 0.0f;
        for (int i = 0; i < frames; i++) {
            icecream_dsp_step(&c, &l[i], &r[i]);
            if (fabsf(l[i]) > peak) peak = fabsf(l[i]);
            if (fabsf(r[i]) > peak) peak = fabsf(r[i]);
        }
        printf("      full-gain peak: %.3f\n", peak);
        CHECK(peak > 0.05f, "the song is effectively silent (peak %.3f)", peak);
        CHECK(peak <= 1.0f, "the song clips the rails (peak %.3f)", peak);
    }

    /* (3) every note is the note PARENA asked for, measured from the samples */
    {
        int bad = 0;
        printf("      step  asked Hz  measured Hz\n");
        for (int n = 0; n < icecream_note_count(); n++) {
            int start_ms = 0;
            for (int k = 0; k < n; k++) start_ms += icecream_note_ms(k);
            int asked = icecream_note_hz_milli(n);
            int dur = icecream_note_ms(n);
            /* measure the first 60% of the note: the tail of a music-box decay is too quiet to count crossings
               reliably, and the pitch is set by the strike anyway */
            int from = (int)((double)start_ms * RATE / 1000.0);
            int to = (int)((double)(start_ms + (dur * 6) / 10) * RATE / 1000.0);
            if (to > frames) to = frames;
            int crossings = 0;
            float prev = l[from];
            for (int i = from + 1; i < to; i++) {
                if ((l[i] >= 0.0f) != (prev >= 0.0f)) crossings++;
                prev = l[i];
            }
            double seconds = (double)(to - from) / RATE;
            int measured = (int)((double)crossings / (2.0 * seconds) + 0.5);
            printf("      %4d  %8.1f  %11.1f\n", n, asked / 1000.0, (double)measured);
            if (asked <= 0) { bad++; continue; }
            if (fabs((double)measured - (double)asked / 1000.0) > (double)asked / 1000.0 * 0.08) {
                bad++;
                printf("FAIL: note %d asked %.1f Hz, sounded %.1f Hz\n", n, asked / 1000.0, (double)measured);
            }
        }
        CHECK(bad == 0, "%d of %d notes did not sound at their rule's pitch", bad, icecream_note_count());
    }

    /* (4) the loop really loops: the first note sounds again one loop later */
    {
        int from = (int)((double)(loop_ms + 0) * RATE / 1000.0);
        int to = (int)((double)(loop_ms + (icecream_note_ms(0) * 6) / 10) * RATE / 1000.0);
        if (to > frames) to = frames;
        int crossings = 0;
        float prev = l[from];
        for (int i = from + 1; i < to; i++) { if ((l[i] >= 0.0f) != (prev >= 0.0f)) crossings++; prev = l[i]; }
        int measured = (int)((double)crossings / (2.0 * (double)(to - from) / RATE) + 0.5);
        int asked = icecream_note_hz_milli(0) / 1000;
        printf("      one loop later: measured %d Hz (first note is %d Hz)\n", measured, asked);
        CHECK(abs(measured - asked) <= asked / 12, "the loop does not return to its first note (%d vs %d)",
              measured, asked);
    }

    /* (5) the music-box decay is real: within a note, the strike is louder than the tail */
    {
        int dur = icecream_note_ms(0);
        int from = 0, mid = (int)((double)dur * 0.2 * RATE / 1000.0), end = (int)((double)dur * 0.9 * RATE / 1000.0);
        double early = 0.0, late = 0.0;
        for (int i = from; i < mid; i++) early += (double)l[i] * l[i];
        for (int i = end - (mid - from); i < end; i++) late += (double)l[i] * l[i];
        printf("      within the first note: strike energy %.4f, tail energy %.4f\n", early, late);
        CHECK(early > late * 2.0, "a note does not decay like a box (strike %.4f, tail %.4f)", early, late);
    }

    /* (6) pan: a song from the right is one-sided, the same law every other voice uses */
    {
        IceCreamDsp c;
        icecream_dsp_init(&c);
        icecream_dsp_set(&c, 1, 1.0f, 1.0f);
        float lpk = 0.0f, rpk = 0.0f, ll, rr;
        for (int i = 0; i < RATE * 3; i++) {
            icecream_dsp_step(&c, &ll, &rr);
            if (i > RATE * 2) { if (fabsf(ll) > lpk) lpk = fabsf(ll); if (fabsf(rr) > rpk) rpk = fabsf(rr); }
        }
        printf("      hard right after 2 s: left peak %.4f, right peak %.4f\n", lpk, rpk);
        CHECK(rpk > 0.02f, "hard right is silent on the right (%.4f)", rpk);
        CHECK(lpk < rpk * 0.05f, "hard right leaks into the left ear (left %.4f, right %.4f)", lpk, rpk);
    }

    free(l);
    free(r);
    printf("\n%s%d check(s) failed\n", g_failed ? "FAIL: " : "PASS: ", g_failed);
    return g_failed ? 1 : 0;
}
