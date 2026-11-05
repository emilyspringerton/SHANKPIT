/* thunder_dsp_test.c -- the rumble's shape, measured offline.
 *
 * What this proves: the strike STARTS on the strike (its energy is front-loaded, not smeared), it decays --
 * the last tenth is far quieter than the first tenth -- it is deterministic and finite, and its peak is inside
 * the rails. What it does NOT prove: that five seconds of low-passed noise reads as thunder rather than as a
 * truck going past. That is timbre, and it needs ears.
 */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#include "thunder_dsp.h"

static int g_failed, g_checks;
#define CHECK(cond, ...) do { \
    g_checks++; \
    if (!(cond)) { g_failed++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

#define RATE 22050
#define FRAMES (RATE * THUNDER_DSP_SECONDS)

static double energy(const float *buf, int from, int to) {
    double e = 0.0;
    for (int i = from; i < to; i++) e += (double)buf[i * 2] * (double)buf[i * 2];
    return e;
}

int main(void) {
    printf("== thunder shape ==\n");
    float *buf = (float *)calloc((size_t)FRAMES * 2, sizeof(float));
    float *again = (float *)calloc((size_t)FRAMES * 2, sizeof(float));
    if (!buf || !again) { printf("FAIL: out of memory\n"); return 1; }

    float peak = thunder_dsp_fill(buf, FRAMES, RATE);
    float peak2 = thunder_dsp_fill(again, FRAMES, RATE);

    printf("      peak %.3f over %.1f s\n", peak, (double)FRAMES / RATE);
    CHECK(peak > 0.05f, "a strike is effectively silent (peak %.3f)", peak);
    CHECK(peak <= 1.0f, "a strike clips the rails (peak %.3f)", peak);

    /* front-loaded: the crack and the first roll hold far more energy than the tail */
    double head = energy(buf, 0, FRAMES / 10);
    double tail = energy(buf, FRAMES - FRAMES / 10, FRAMES);
    printf("      energy: first tenth %.2f, last tenth %.2f (%.1fx)\n", head, tail, tail > 0.0 ? head / tail : 0.0);
    CHECK(head > tail * 8.0, "the strike does not decay (head %.2f, tail %.2f)", head, tail);

    /* the strike begins immediately: a blast that fades in would leave the first 10 ms near silence */
    double first_10ms = energy(buf, 0, RATE / 100);
    double first_half_s = energy(buf, 0, RATE / 2);
    printf("      first 10 ms holds %.1f%% of the first half second\n",
           first_half_s > 0.0 ? 100.0 * first_10ms / first_half_s : 0.0);
    CHECK(first_half_s > 0.0 && first_10ms > first_half_s * 0.05,
          "the strike does not start on the strike (%.1f%% in the first 10 ms)",
          first_half_s > 0.0 ? 100.0 * first_10ms / first_half_s : 0.0);

    /* deterministic: two strikes are the same strike, and finite throughout */
    int differs = 0, bad = 0;
    for (int i = 0; i < FRAMES * 2; i++) {
        if (buf[i] != again[i]) differs++;
        if (!isfinite(buf[i])) bad++;
    }
    CHECK(differs == 0, "the generator is not deterministic (%d differing samples)", differs);
    CHECK(bad == 0, "%d non-finite sample(s) in a strike", bad);
    CHECK(fabsf(peak - peak2) < 1e-6f, "two strikes report different peaks (%.6f vs %.6f)", peak, peak2);

    free(buf);
    free(again);
    printf("\n%s%d check(s) failed\n", g_failed ? "FAIL: " : "PASS: ", g_failed);
    return g_failed ? 1 : 0;
}
