#ifndef WIND_DSP_H
#define WIND_DSP_H

/* wind_dsp.h -- the wind bed's shaping, as a header, so it can be measured without a device.
 *
 * Why this exists: the siren's oscillator got this treatment first (siren_dsp.h) and it immediately found
 * things listening had not -- a clipped ceiling, a click on start. The wind bed had the same problem and the
 * same answer: the part that SHAPES the noise (gain/brightness/pan smoothing, the brightness one-pole, the
 * one-sided pan law) is now a small struct with no SDL, no buffer and no clock, and a test renders it against
 * a known noise source. The loop buffer and its crossfade stay in audio.c, because those are a buffer, not a
 * law; what lives here is the law that makes a calm night and a gale out of the same four seconds of noise.
 *
 * What this is NOT: the weather. How loud the wind is, how bright, and from which side all come from PARENA
 * (stdlib/shankpit/weather_audio_rules.prn) and arrive through audio_set_wind; this file only chases those
 * targets smoothly enough not to click. It also does not generate noise -- the caller hands it one sample per
 * frame, so a test can feed it a ramp or a tone and see exactly what the shaping does.
 *
 * Named limits: the brightness knob is one one-pole, so "brighter" is a continuous tilt of the same noise
 * rather than an added layer; and the pan law is the engine-wide one-sided law (the far side is attenuated,
 * never fully removed before the smoothing settles), kept identical to spatial_gains so wind and gunshots
 * agree about which way is left.
 */

#include <string.h>

#define WIND_DSP_SMOOTH 0.0012f   /* per frame: about a second to settle, because weather does not click */

typedef struct {
    float gain,   gain_target;
    float bright, bright_target;
    float pan,    pan_target;
    float lp;                  /* the brightness one-pole's state */
} WindDsp;

static inline void wind_dsp_init(WindDsp *w) {
    if (!w) return;
    memset(w, 0, sizeof(*w));
    w->gain = w->gain_target = 0.0f;      /* silent until a host says there is weather at all */
    w->bright = w->bright_target = 0.25f; /* a dull default: the body of the air, not a gale */
    w->pan = w->pan_target = 0.0f;
}

/* Targets only. Called from the render loop while the audio thread mixes, so it must not touch lp or the
 * live values. */
static inline void wind_dsp_set(WindDsp *w, float gain, float bright, float pan) {
    if (!w) return;
    w->gain_target   = gain   < 0.0f ? 0.0f : (gain   > 1.0f ? 1.0f : gain);
    w->bright_target = bright < 0.0f ? 0.0f : (bright > 1.0f ? 1.0f : bright);
    w->pan_target    = pan    < -1.0f ? -1.0f : (pan   > 1.0f ? 1.0f : pan);
}

/* One frame: `raw` is the loop's next sample (-1..1). The brightness knob IS the one-pole's cutoff -- dull
 * air keeps only the body, a gale opens it to the hiss -- which is the whole of "it sounds different when it
 * gets windier", one continuous number instead of a second sample. */
static inline void wind_dsp_frame(WindDsp *w, float raw, float *out_l, float *out_r) {
    float l = 0.0f, r = 0.0f;
    if (w) {
        float coeff = 0.05f + 0.40f * w->bright;
        w->lp += (raw - w->lp) * coeff;
        w->gain   += (w->gain_target   - w->gain)   * WIND_DSP_SMOOTH;
        w->bright += (w->bright_target - w->bright) * WIND_DSP_SMOOTH;
        w->pan    += (w->pan_target    - w->pan)    * WIND_DSP_SMOOTH;
        float p = w->pan;
        float amp = w->gain * 0.9f;
        l = w->lp * amp * (p > 0.0f ? 1.0f - p : 1.0f);
        r = w->lp * amp * (p < 0.0f ? 1.0f + p : 1.0f);
    }
    if (out_l) *out_l = l;
    if (out_r) *out_r = r;
}

#endif /* WIND_DSP_H */
