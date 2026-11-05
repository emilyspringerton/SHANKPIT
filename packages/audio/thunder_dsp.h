#ifndef THUNDER_DSP_H
#define THUNDER_DSP_H

/* thunder_dsp.h -- the thunder rumble's five seconds, generated as a pure function of the frame index.
 *
 * Why this exists: synth_thunder in audio.c was deterministic but reachable only through an SDL buffer, so
 * the things worth knowing -- does a strike actually DECAY, does it start on the strike or a beat later, is
 * the peak inside the rails -- could only be guessed at. The generator moves here as a fill function: the
 * caller owns the buffer, this file owns the maths. audio.c hands the result to a SoundBuf exactly as before.
 *
 * The character is unchanged and deliberate: a crack at the near end and a long rolling rumble under it,
 * tumble from three overlapping swells of the SAME low-passed noise (at the strike, 1.5 s and 3.1 s) rather
 * than a reverb this engine does not have. Nothing about the level is baked in -- loudness and delay arrive
 * from PARENA's weather_audio_rules.prn -- so a distant strike is this same sound, quieter and late.
 *
 * Named limits: it is a 5 s one-shot with no variation between strikes (deterministic seed, same shape every
 * time -- a storm's bolts all sound like the same bolt); and it is mono, panned by the caller.
 */

#include <math.h>
#include <stdint.h>

#define THUNDER_DSP_SECONDS 5

/* Fills `frames` interleaved stereo float pairs at `rate` Hz with one strike. `frames` should be
 * rate * THUNDER_DSP_SECONDS; a shorter buffer gets the head of the strike, which still cracks and starts to
 * roll. Returns the peak absolute sample written, so a caller can check the rails without re-walking it. */
static inline float thunder_dsp_fill(float *out, int frames, int rate) {
    uint32_t rng = 0x7A11B00Bu;
    float lp = 0.0f, lp2 = 0.0f, peak = 0.0f;
    int i;
    if (!out || frames <= 0) return 0.0f;
    for (i = 0; i < frames; i++) {
        float t = (float)i / (float)frames;
        rng = rng * 1664525u + 1013904223u;
        float nz = (float)(int32_t)rng / 2147483648.0f;
        lp  += (nz - lp) * 0.055f;    /* the body of the roll */
        lp2 += (lp - lp2) * 0.004f;   /* the sub-bass under it */
        float d2 = t - 0.30f, d3 = t - 0.62f;
        float env = expf(-t * 22.0f)
                  + 0.80f * expf(-d2 * d2 * 220.0f)
                  + 0.45f * expf(-d3 * d3 * 150.0f);
        float hiss = (nz - lp) * expf(-t * 90.0f) * 0.5f;   /* the crack, gone in a fraction of a second */
        float s = ((lp2 * 1.7f + lp * 0.35f) * env + hiss) * 0.85f;
        if (s > peak) peak = s;
        if (-s > peak) peak = -s;
        out[i * 2] = s;
        out[i * 2 + 1] = s;
    }
    (void)rate;   /* the shape is in fractions of the buffer, so the rate only decides the length */
    return peak;
}

#endif /* THUNDER_DSP_H */
