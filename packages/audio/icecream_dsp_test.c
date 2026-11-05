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

    /* (7) FOUR TRUCKS, FOUR TUNES. One tune transposed four ways is still one truck to an ear, so each
       jingle is its own melody (PARENA icecream-note-hz-milli-for): jingle 0 IS the authored song, the rest
       are generated pentatonic walks. EVERY step of EVERY jingle is rendered and its pitch measured -- "it
       is in the table" and "it sounds" are different claims, and this test only makes the second one. */
    {
        int bad = 0, silent = 0;
        for (int v = 0; v < icecream_jingle_count(); v++) {
          for (int step = 0; step < icecream_note_count(); step++) {
            int want = icecream_note_hz_milli_for(step, v);
            IceCreamDsp c;
            icecream_dsp_init(&c);
            icecream_dsp_set(&c, 1, 1.0f, 0.0f);
            icecream_dsp_set_jingle(&c, v);
            double at = 0.0;
            for (int j = 0; j < step; j++) at += (double)icecream_note_ms(j);
            c.ms = at;                             /* put the song clock on this step */
            int dur = icecream_note_ms(step);
            int to = (int)((double)((dur * 6) / 10) * RATE / 1000.0);
            int crossings = 0;
            float prev = 0.0f;
            for (int i = 0; i < to; i++) {
                float ll, rr;
                icecream_dsp_step(&c, &ll, &rr);
                if ((ll >= 0.0f) != (prev >= 0.0f)) crossings++;
                prev = ll;
            }
            int measured = (int)((double)crossings / (2.0 * (double)to / RATE) + 0.5);
            if (want <= 0) { silent++; continue; }
            if (fabs((double)measured - (double)want / 1000.0) > (double)want / 1000.0 * 0.08) {
                bad++;
                printf("FAIL: jingle %d step %d asked %.1f Hz, sounded %.1f Hz\n",
                       v, step, want / 1000.0, (double)measured);
            }
          }
        }
        CHECK(bad == 0, "%d note(s) did not sound in their own jingle's melody", bad);
        CHECK(silent == 0, "%d step(s) of some jingle are silent", silent);
        for (int id = 0; id < 8; id++) {
            int j = icecream_jingle_for_id(id);
            CHECK(j >= 0 && j < icecream_jingle_count(), "truck %d got jingle %d, outside the table", id, j);
        }
        CHECK(icecream_jingle_for_id(-3) >= 0 && icecream_jingle_for_id(-3) < icecream_jingle_count(),
              "a negative truck id was handed jingle %d", icecream_jingle_for_id(-3));
        /* jingle 0 is the authored song, and the generated ones come home at the wrap */
        for (int i = 0; i < icecream_note_count(); i++)
            CHECK(icecream_note_hz_milli_for(i, 0) == icecream_note_hz_milli(i),
                  "jingle 0 is not the authored song at step %d (%d vs %d)", i,
                  icecream_note_hz_milli_for(i, 0), icecream_note_hz_milli(i));
        for (int v = 1; v < icecream_jingle_count(); v++) {
            CHECK(icecream_note_hz_milli_for(0, v) == icecream_note_hz_milli_for(icecream_note_count() - 1, v),
                  "jingle %d does not end on the note it began on (%.1f vs %.1f Hz)", v,
                  icecream_note_hz_milli_for(0, v) / 1000.0,
                  icecream_note_hz_milli_for(icecream_note_count() - 1, v) / 1000.0);
            int in_scale = 1;
            for (int i = 0; i < icecream_note_count(); i++) {
                int deg = icecream_motif_degree(i, v);
                if (deg < 0 || deg > 7) in_scale = 0;
                if (icecream_note_hz_milli_for(i, v) != icecream_scale_hz_milli(deg)) in_scale = 0;
            }
            CHECK(in_scale, "jingle %d left its own scale", v);
        }

        /* and no two trucks are the same truck: every pair differs in at least half its steps */
        for (int a = 0; a < icecream_jingle_count(); a++)
            for (int b = a + 1; b < icecream_jingle_count(); b++) {
                int diff = 0;
                for (int i = 0; i < icecream_note_count(); i++)
                    if (icecream_note_hz_milli_for(i, a) != icecream_note_hz_milli_for(i, b)) diff++;
                printf("      jingles %d and %d differ on %d of %d steps\n", a, b, diff, icecream_note_count());
                CHECK(diff >= icecream_note_count() / 2,
                      "jingles %d and %d are the same tune (%d of %d steps differ)", a, b, diff,
                      icecream_note_count());
            }
    }

    /* ---- the CHIME of a stopped truck: two struck notes, a breath, then silence until the next one ---- */
    {
        IceCreamChimeDsp ch;
        icecream_chime_dsp_init(&ch);
        icecream_chime_dsp_set(&ch, 1, 0.8f, 0.0f);
        int note_ms = icecream_chime_note_ms();
        int gap_ms = icecream_chime_gap_ms();
        int period = icecream_chime_period_ms();
        int to = (int)((double)RATE * (double)note_ms / 1000.0);
        printf("      chime: %d note(s) of %d ms, %d ms breath, every %d ms (%d ms sounding)\n",
               icecream_chime_note_count(), note_ms, gap_ms, period, icecream_chime_ms());
        CHECK(icecream_chime_ms() == icecream_chime_note_count() * (note_ms + gap_ms),
              "the chime's own length (%d) is not its notes and breaths (%d)", icecream_chime_ms(),
              icecream_chime_note_count() * (note_ms + gap_ms));
        CHECK(icecream_chime_ms() < period, "the chime (%d ms) leaves no silence in its %d ms period",
              icecream_chime_ms(), period);

        int bad = 0;
        for (int n = 0; n < icecream_chime_note_count(); n++) {
            float prev = 0.0f;
            int crossings = 0;
            float peak = 0.0f;
            for (int i = 0; i < to; i++) {
                float ll, rr;
                icecream_chime_dsp_step(&ch, &ll, &rr);
                if ((ll >= 0.0f) != (prev >= 0.0f)) crossings++;
                prev = ll;
                if (fabsf(ll) > peak) peak = fabsf(ll);
            }
            int measured = (int)((double)crossings / (2.0 * (double)to / (double)RATE) + 0.5);
            int want = icecream_chime_hz_milli(n);
            printf("      chime note %d: asked %.1f Hz, sounded %.1f Hz\n", n, want / 1000.0, (double)measured);
            if (want <= 0 || fabs((double)measured - (double)want / 1000.0) > (double)want / 1000.0 * 0.08) bad++;
            /* and the breath after this note really is silent. The note's last frame lands a hair inside the
               breath (the clock is a double, the boundary is compared as a whole millisecond), so let the
               boundary pass and measure the MIDDLE of the gap -- tail frames would measure the note's own
               decay, and the frames after the gap would measure the next strike. */
            int tg = (int)((double)RATE * (double)gap_ms / 1000.0);
            int skip = tg / 4, probe = tg / 2;
            for (int i = 0; i < skip; i++) { float ll, rr; icecream_chime_dsp_step(&ch, &ll, &rr); }
            float gp = 0.0f;
            for (int i = 0; i < probe; i++) {
                float ll, rr;
                icecream_chime_dsp_step(&ch, &ll, &rr);
                if (fabsf(ll) > gp) gp = fabsf(ll);
            }
            for (int i = 0; i < tg - skip - probe; i++) { float ll, rr; icecream_chime_dsp_step(&ch, &ll, &rr); }
            if (gp > 0.01f) {
                bad++;
                printf("FAIL: the breath after chime note %d sounds (peak %.4f)\n", n, (double)gp);
            }
        }
        CHECK(bad == 0, "%d of the chime's note(s) were wrong or not followed by silence", bad);

        /* the clock runs THROUGH the silence: a bell that only counted while struck would ring once and
           freeze inside its own gap, which is exactly the bug this voice was written not to have */
        int rest = (int)((double)RATE * (double)(period - icecream_chime_ms()) / 1000.0);
        for (int i = 0; i < rest; i++) { float ll, rr; icecream_chime_dsp_step(&ch, &ll, &rr); }
        float again = 0.0f;
        for (int i = 0; i < to; i++) {
            float ll, rr;
            icecream_chime_dsp_step(&ch, &ll, &rr);
            if (fabsf(ll) > again) again = fabsf(ll);
        }
        printf("      after %d ms of silence the next chime peaked %.4f\n", period - icecream_chime_ms(),
               (double)again);
        CHECK(again > 0.01f, "the chime never rang a second time (%d ms period)", period);

        /* one-sided pan, like every other voice in the engine */
        IceCreamChimeDsp ch2;
        icecream_chime_dsp_init(&ch2);
        icecream_chime_dsp_set(&ch2, 1, 0.9f, 1.0f);
        /* let the gain and pan smoothing arrive first: the ramp owns the first frames and would show up on
           the left channel for the same reason it does in the song's own test */
        for (int i = 0; i < 20000; i++) { float ll, rr; icecream_chime_dsp_step(&ch2, &ll, &rr); }
        float lp = 0.0f, rp = 0.0f;
        for (int i = 0; i < 4000; i++) {
            float ll, rr;
            icecream_chime_dsp_step(&ch2, &ll, &rr);
            if (fabsf(ll) > lp) lp = fabsf(ll);
            if (fabsf(rr) > rp) rp = fabsf(rr);
        }
        printf("      chime hard right: left peak %.4f, right peak %.4f\n", (double)lp, (double)rp);
        CHECK(lp < 0.02f && rp > 0.01f, "hard right is not one-sided (left %.4f right %.4f)",
              (double)lp, (double)rp);

        /* the RULES behind it: quiet, short-carrying, and the exact complement of the song */
        CHECK(icecream_chime_gain_permille(0) > 0 && icecream_chime_gain_permille(0) <= 600,
              "a chime on top of the truck is not quiet (%d permille)", icecream_chime_gain_permille(0));
        CHECK(icecream_chime_gain_permille(0) < icecream_gain_permille(0),
              "the chime is as loud as the song (%d vs %d)", icecream_chime_gain_permille(0),
              icecream_gain_permille(0));
        CHECK(icecream_chime_gain_permille(icecream_chime_carry_units() + 1) == 0,
              "the chime carries past its own reach");
        CHECK(icecream_chime_carry_units() < icecream_carry_units(),
              "the chime carries as far as the song (%d vs %d)", icecream_chime_carry_units(),
              icecream_carry_units());
        CHECK(icecream_chime_trigger(0) == 1 && icecream_chime_trigger(400) == 0,
              "the chime is not tied to being stopped");
        CHECK(icecream_chime_trigger(0) == icecream_serve_open(0, 0),
              "a truck whose window is open is not chiming");
        CHECK(icecream_trigger(1, 400) == 1 && icecream_chime_trigger(400) == 0,
              "a rolling truck chimed instead of singing");
        CHECK(icecream_trigger(1, 0) == 0 && icecream_chime_trigger(0) == 1,
              "a parked truck sang instead of chiming");
        printf("PASS: a stopped truck rings two notes, quietly, and never at the same time as the song\n");
    }

    free(l);
    free(r);
    printf("\n%s%d check(s) failed\n", g_failed ? "FAIL: " : "PASS: ", g_failed);
    return g_failed ? 1 : 0;
}
