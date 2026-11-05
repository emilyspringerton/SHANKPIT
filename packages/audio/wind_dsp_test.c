/* wind_dsp_test.c -- the wind bed's shaping law, measured offline.
 *
 * What this proves: that brightness really does open the filter (a gale has more zero crossings than a calm
 * night over the SAME noise), that the bed swells over about a second instead of clicking in, that hard-right
 * pan empties the left ear, that full gain stays under the rails, and that a host which never reports weather
 * gets silence rather than a hum. What it does NOT prove: that the noise recipe sounds like wind -- that is a
 * judgement about timbre, and it still needs ears.
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>

#include "wind_dsp.h"

static int g_failed, g_checks;
#define CHECK(cond, ...) do { \
    g_checks++; \
    if (!(cond)) { g_failed++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

#define RATE 22050

/* the same LCG the engine's noise uses: deterministic, so a failure is reproducible */
static float noise_at(uint32_t *rng) {
    *rng = *rng * 1664525u + 1013904223u;
    return (float)(int32_t)*rng / 2147483648.0f;
}

/* Render `frames` with the given targets pre-settled for `warmup` frames, and report the zero-crossing count
 * and peak over the measurement window (the last third). */
static void render(WindDsp *w, int frames, float *out_peak, int *out_crossings, float *out_left_energy) {
    uint32_t rng = 0x51ED270Bu;
    int crossings = 0, start = frames / 3;
    float peak = 0.0f, prev = 0.0f, left = 0.0f;
    for (int i = 0; i < frames; i++) {
        float l, r;
        wind_dsp_frame(w, noise_at(&rng), &l, &r);
        if (i >= start) {
            if ((l >= 0.0f) != (prev >= 0.0f)) crossings++;
            if (fabsf(l) > peak) peak = fabsf(l);
            left += fabsf(l);
        }
        prev = l;
    }
    if (out_peak) *out_peak = peak;
    if (out_crossings) *out_crossings = crossings;
    if (out_left_energy) *out_left_energy = left;
}

int main(void) {
    printf("== wind bed shaping ==\n");

    /* (1) silence at rest: a host that never reports weather gets no hum */
    {
        WindDsp w; wind_dsp_init(&w);
        float peak = 0.0f;
        render(&w, RATE, &peak, NULL, NULL);
        printf("      rest peak: %.6f\n", peak);
        CHECK(peak == 0.0f, "the bed makes noise with no weather reported (peak %.6f)", peak);
    }

    /* (2) brightness opens the filter: more crossings over identical noise */
    int calm_crossings = 0, gale_crossings = 0;
    {
        WindDsp calm, gale;
        wind_dsp_init(&calm); wind_dsp_set(&calm, 1.0f, 0.0f, 0.0f);
        wind_dsp_init(&gale); wind_dsp_set(&gale, 1.0f, 1.0f, 0.0f);
        render(&calm, RATE * 4, NULL, &calm_crossings, NULL);
        render(&gale, RATE * 4, NULL, &gale_crossings, NULL);
        printf("      zero crossings in the last third: calm %d, gale %d\n", calm_crossings, gale_crossings);
        CHECK(gale_crossings > calm_crossings * 2, "brightness does not open the filter (calm %d, gale %d)",
              calm_crossings, gale_crossings);
    }

    /* (3) the swell: the first frames are far quieter than the settled bed */
    {
        WindDsp w; wind_dsp_init(&w); wind_dsp_set(&w, 1.0f, 0.25f, 0.0f);
        uint32_t rng = 0x51ED270Bu;
        double early = 0.0, settled = 0.0;
        float l, r;
        for (int i = 0; i < RATE; i++) {
            wind_dsp_frame(&w, noise_at(&rng), &l, &r);
            if (i < 20) early += fabs((double)l);
            if (i > RATE - 20) settled += fabs((double)l);
        }
        printf("      swell: first 20 frames %.4f, settled 20 frames %.4f\n", early, settled);
        CHECK(settled > early * 3.0, "the bed clicks in instead of swelling (early %.4f, settled %.4f)", early, settled);
    }

    /* (4) pan: hard right is one-sided, and the far side stays under 5% once the smoothing has settled */
    {
        WindDsp w; wind_dsp_init(&w); wind_dsp_set(&w, 1.0f, 0.5f, 1.0f);
        uint32_t rng = 0x51ED270Bu;
        float l, r, lpk = 0.0f, rpk = 0.0f;
        for (int i = 0; i < RATE * 4; i++) {
            wind_dsp_frame(&w, noise_at(&rng), &l, &r);
            if (i > RATE * 3) {
                if (fabsf(l) > lpk) lpk = fabsf(l);
                if (fabsf(r) > rpk) rpk = fabsf(r);
            }
        }
        printf("      hard right after 3 s: left peak %.4f, right peak %.4f\n", lpk, rpk);
        CHECK(rpk > 0.05f, "hard right is silent on the right (%.4f)", rpk);
        CHECK(lpk < rpk * 0.05f, "hard right still leaks into the left ear (left %.4f, right %.4f)", lpk, rpk);
    }

    /* (5) rails: full gain, fully bright, under 1.0 */
    {
        WindDsp w; wind_dsp_init(&w); wind_dsp_set(&w, 1.0f, 1.0f, 0.0f);
        float peak = 0.0f;
        render(&w, RATE * 4, &peak, NULL, NULL);
        printf("      full-gain peak: %.3f\n", peak);
        CHECK(peak > 0.2f, "a gale at full gain barely registers (peak %.3f)", peak);
        CHECK(peak <= 0.95f, "a gale at full gain clips (peak %.3f)", peak);
    }

    printf("\n%s%d check(s) failed\n", g_failed ? "FAIL: " : "PASS: ", g_failed);
    return g_failed ? 1 : 0;
}
