#ifndef SIREN_DSP_H
#define SIREN_DSP_H

/* siren_dsp.h -- the siren voice's oscillator, as a header, so it can be measured without a device.
 *
 * Why this exists: the engine's siren lived inside packages/audio/audio.c as four file-static floats, so
 * the only way to know whether an EMS two-tone really alternates or whether a hard-right wail actually
 * stopped the left channel was to listen -- and this box has no audio device. That is not verification, it
 * is hope. The oscillator is now a tiny struct with no SDL, no mutex and no clock, used by audio.c exactly
 * as before, and rendered offline by siren_dsp_test.c: frequency by zero crossings, pan by per-channel
 * energy, and a hard ceiling every sample must respect.
 *
 * What this is NOT: the rules. Which siren a car sounds, what its frequency is at a moment and how loud it
 * is at a distance all live in PARENA (stdlib/shankpit/siren_rules.prn). This file receives those resolved
 * numbers and only integrates a phase and applies gains. It deliberately does not know what an EMS hi-lo
 * is, so it cannot disagree with the rule about one. */

#include <math.h>
#include <string.h>

#include "../simulation/siren_rules.h"   /* SIREN_KIND_SILENT */

#define SIREN_DSP_RATE 22050

typedef struct {
    int    kind;            /* SIREN_KIND_*, 0 = silent */
    float  hz, hz_target;   /* real hertz, smoothed: the host passes PARENA's millihertz / 1000 */
    float  gain, gain_target;
    float  pan, pan_target;
    double phase;           /* 0..1, integrated from hz */
} SirenDsp;

static inline void siren_dsp_init(SirenDsp *s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->kind = SIREN_KIND_SILENT;
    s->hz = s->hz_target = 690.0f;   /* never starts at 0 Hz: a phase step of 0 is still a DC click */
    s->gain = s->gain_target = 0.0f;
    s->pan = s->pan_target = 0.0f;
    s->phase = 0.0;
}

/* Store targets only, never the live values: this is called from a render loop while another thread mixes,
 * which is why audio.c's public setter takes the same shape under its own lock. */
static inline void siren_dsp_set(SirenDsp *s, int kind, float hz, float gain, float pan) {
    if (!s) return;
    s->kind = kind;
    if (hz > 0.0f) s->hz_target = hz;
    s->gain_target = gain < 0.0f ? 0.0f : (gain > 1.0f ? 1.0f : gain);
    s->pan_target = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);
}

/* One stereo frame. Returns the summed level so a caller (or a test) can watch for clipping without
 * re-deriving it. A siren starts faster than the wind bed swells -- 1% per frame, about 4.5 ms at 22 kHz --
 * because the founder's trigger is a key press, not a weather change. */
static inline void siren_dsp_step(SirenDsp *s, float *out_l, float *out_r) {
    float l = 0.0f, r = 0.0f;
    if (s) {
        s->gain += (s->gain_target - s->gain) * 0.010f;
        s->pan  += (s->pan_target  - s->pan)  * 0.010f;
        s->hz   += (s->hz_target   - s->hz)   * 0.020f;
        if (!(s->kind == SIREN_KIND_SILENT && s->gain < 0.0005f)) {
            s->phase += (double)s->hz / (double)SIREN_DSP_RATE;
            while (s->phase >= 1.0) s->phase -= 1.0;
            while (s->phase < 0.0)  s->phase += 1.0;
            const float pi2 = 6.2831853f;
            float v = sinf((float)s->phase * pi2);
            v += 0.32f * sinf((float)s->phase * pi2 * 2.0f);   /* horn bite: a pure sine reads as a test tone */
            v *= 0.45f;
            float p = s->pan;
            float amp = s->gain;
            l = v * amp * (p > 0.0f ? 1.0f - p : 1.0f);
            r = v * amp * (p < 0.0f ? 1.0f + p : 1.0f);
        }
    }
    if (out_l) *out_l = l;
    if (out_r) *out_r = r;
}

#endif /* SIREN_DSP_H */
