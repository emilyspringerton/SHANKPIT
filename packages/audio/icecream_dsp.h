#ifndef ICECREAM_DSP_H
#define ICECREAM_DSP_H

/* icecream_dsp.h -- the ice-cream truck's song voice, as a header, so it can be heard offline.
 *
 * Founder (2026-11-05): "add ice cream trucks similar affordances to police vehicles ice tream truck song
 * affordances real sound audio engineering parena mods focuse".
 *
 * Why this exists at all: an ice-cream truck is an AFFORDANCE, not a vehicle with a horn. The song has to
 * arrive before the truck does, come from the right side of the street, and loop without a seam, or the
 * mechanic (you hear it, you go looking) does not work. None of that can be verified by listening on this
 * box, so the voice is a header with no SDL, no buffer and no lock: the test renders it against PARENA's own
 * rules and measures the pitch of every note.
 *
 * The split, the same one the siren uses: PARENA says WHAT is sung -- the melody in millihertz, each step's
 * length, its accent, the decay inside a note and how far it carries (icecream_rules.prn). This file says how
 * a millihertz becomes a sample: integrate a phase at 22 kHz, add a bell's harmonics, apply the rule's
 * envelope, put it in the stereo field with the engine-wide one-sided pan law. It cannot disagree with the
 * tune because it does not know the tune -- it asks the rule for the note under its own clock.
 *
 * Named limits: one voice (the host plays the loudest truck, exactly like the siren), the song restarts from
 * the first note after a silence rather than resuming mid-phrase (a jingle that drops you into its middle
 * sounds broken), and the tone is two-partial -- a music box, not a bell choir. */

#include <math.h>
#include <string.h>

#include "../simulation/icecream_rules.h"

#define ICECREAM_DSP_RATE 22050

typedef struct {
    int    playing;
    double ms;               /* position in the loop, milliseconds (double so it cannot drift) */
    int    last_note_hz_milli;
    int    jingle;           /* which MELODY this truck sings (PARENA icecream-jingle-for-id) */
    double phase;            /* 0..1, reset on every note: a music box strikes from silence */
    float  gain, gain_target;
    float  pan, pan_target;
} IceCreamDsp;

static inline void icecream_dsp_init(IceCreamDsp *c) {
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->playing = 0;
    c->ms = 0.0;
    c->last_note_hz_milli = 0;
    c->jingle = 0;
    c->phase = 0.0;
    c->gain = c->gain_target = 0.0f;
    c->pan = c->pan_target = 0.0f;
}

/* Targets only: called from the render loop while the audio thread mixes. */
static inline void icecream_dsp_set(IceCreamDsp *c, int playing, float gain, float pan) {
    if (!c) return;
    c->playing = playing ? 1 : 0;
    c->gain_target = gain < 0.0f ? 0.0f : (gain > 1.0f ? 1.0f : gain);
    c->pan_target = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);
}

/* Which melody this truck sings -- PARENA's icecream-jingle-for-id decides the value, and jingle 0 is the
 * authored song note for note. Clamped to the rule's own count because a rule can be edited to something
 * absurd and a vendor must not index a table that is not there. */
static inline void icecream_dsp_set_jingle(IceCreamDsp *c, int jingle) {
    if (!c) return;
    int count = icecream_jingle_count();
    if (count <= 0) count = 1;
    if (jingle < 0) jingle = 0;
    if (jingle >= count) jingle = jingle % count;
    c->jingle = jingle;
}

/* Which step of the tune is sounding at song_ms, and where inside it. Walks the rule's own step lengths, so
 * a melody with a held last note still lands correctly -- no tempo constant is repeated here. */
static inline void icecream_dsp_note(int song_ms, int *out_index, int *out_pos_ms, int *out_note_ms) {
    int count = icecream_note_count();
    int loop = icecream_loop_ms();
    int t = (loop > 0) ? (song_ms % loop) : 0;
    if (t < 0) t += loop;
    int idx = 0, dur = 1;
    for (idx = 0; idx < count; idx++) {
        dur = icecream_note_ms(idx);
        if (dur <= 0) dur = 1;
        if (t < dur) break;
        t -= dur;
    }
    if (idx >= count) { idx = count > 0 ? count - 1 : 0; dur = 1; t = 0; }
    if (out_index) *out_index = idx;
    if (out_pos_ms) *out_pos_ms = t;
    if (out_note_ms) *out_note_ms = dur;
}

/* One stereo frame, added to the caller's accumulator by way of out_l/out_r. The song clock only advances
 * while something is playing, and a silenced truck restarts its phrase from the first note. */
static inline void icecream_dsp_step(IceCreamDsp *c, float *out_l, float *out_r) {
    float l = 0.0f, r = 0.0f;
    if (c) {
        c->gain += (c->gain_target - c->gain) * 0.010f;
        c->pan  += (c->pan_target  - c->pan)  * 0.010f;
        if (!c->playing) c->ms = 0.0;
        int idx = 0, pos = 0, dur = 1;
        icecream_dsp_note((int)c->ms, &idx, &pos, &dur);
        int hz_milli = icecream_note_hz_milli_for(idx, c->jingle);
        if (c->playing && hz_milli > 0 && c->gain > 0.0005f) {
            if (hz_milli != c->last_note_hz_milli) {
                c->phase = 0.0;                     /* strike the new note from silence */
                c->last_note_hz_milli = hz_milli;
            }
            float hz = (float)hz_milli * 0.001f;
            c->phase += (double)hz / (double)ICECREAM_DSP_RATE;
            while (c->phase >= 1.0) c->phase -= 1.0;
            const float pi2 = 6.2831853f;
            float v = sinf((float)c->phase * pi2);
            v += 0.35f * sinf((float)c->phase * pi2 * 2.0f);   /* the box's brightness */
            float env = (float)icecream_env_permille(pos, dur) * 0.001f;
            float accent = (float)icecream_note_gain_permille(idx) * 0.001f;
            float amp = c->gain * env * accent * 0.42f;
            float p = c->pan;
            l = v * amp * (p > 0.0f ? 1.0f - p : 1.0f);
            r = v * amp * (p < 0.0f ? 1.0f + p : 1.0f);
            c->ms += 1000.0 / (double)ICECREAM_DSP_RATE;
            if (c->ms >= (double)icecream_loop_ms()) c->ms -= (double)icecream_loop_ms();
        }
    }
    if (out_l) *out_l = l;
    if (out_r) *out_r = r;
}

/* ---- THE CHIME OF A STOPPED TRUCK (PARENA icecream-chime-*) --------------------------------------
 *
 * A second, much smaller voice, deliberately not the song turned down: two struck notes with a gap between
 * them and a long silence after, because that is what tells a listener "the window is HERE" rather than "a
 * truck is coming". It shares the envelope rule and the pan law; it owns no numbers of its own -- the
 * pitches, the note and gap lengths, the period and the fall-off are all icecream_rules.prn's.
 *
 * Its clock advances for as long as it is playing, INCLUDING through the silence between chimes: a bell whose
 * clock only ran while it was struck would freeze inside its own gap and never ring twice. */
typedef struct {
    int    playing;
    double ms;               /* position in the chime period, milliseconds */
    double phase;            /* 0..1, reset on every struck note */
    int    last_note_hz_milli;
    float  gain, gain_target;
    float  pan, pan_target;
} IceCreamChimeDsp;

static inline void icecream_chime_dsp_init(IceCreamChimeDsp *c) {
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->playing = 0;
    c->last_note_hz_milli = 0;
}

static inline void icecream_chime_dsp_set(IceCreamChimeDsp *c, int playing, float gain, float pan) {
    if (!c) return;
    c->playing = playing ? 1 : 0;
    c->gain_target = gain < 0.0f ? 0.0f : (gain > 1.0f ? 1.0f : gain);
    c->pan_target = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);
}

/* Which note is sounding at chime_ms, and where inside it. Note and gap lengths come from the rules, so a
 * chime with three notes or a longer breath needs no change here. */
static inline void icecream_chime_dsp_note(int chime_ms, int *out_index, int *out_pos_ms) {
    int note_ms = icecream_chime_note_ms();
    int gap_ms = icecream_chime_gap_ms();
    int step = note_ms + gap_ms;
    if (note_ms < 1) note_ms = 1;
    if (step < 1) step = 1;
    int t = chime_ms % icecream_chime_ms();
    if (t < 0) t += icecream_chime_ms();
    if (out_index) *out_index = t / step;
    if (out_pos_ms) *out_pos_ms = t % step;
}

static inline void icecream_chime_dsp_step(IceCreamChimeDsp *c, float *out_l, float *out_r) {
    float l = 0.0f, r = 0.0f;
    if (c) {
        c->gain += (c->gain_target - c->gain) * 0.010f;
        c->pan  += (c->pan_target  - c->pan)  * 0.010f;
        if (!c->playing) c->ms = 0.0;
        int idx = 0, pos = 0;
        icecream_chime_dsp_note((int)c->ms, &idx, &pos);
        int note_ms = icecream_chime_note_ms();
        int hz_milli = (idx < icecream_chime_note_count()) ? icecream_chime_hz_milli(idx) : 0;
        if (pos >= note_ms) hz_milli = 0;        /* the breath between the two strikes */
        if (c->playing && hz_milli > 0 && c->gain > 0.0005f) {
            if (hz_milli != c->last_note_hz_milli) {
                c->phase = 0.0;
                c->last_note_hz_milli = hz_milli;
            }
            float hz = (float)hz_milli * 0.001f;
            c->phase += (double)hz / (double)ICECREAM_DSP_RATE;
            while (c->phase >= 1.0) c->phase -= 1.0;
            const float pi2 = 6.2831853f;
            float v = sinf((float)c->phase * pi2);
            v += 0.25f * sinf((float)c->phase * pi2 * 3.0f);  /* a struck bell, brighter than the box */
            float env = (float)icecream_env_permille(pos, note_ms) * 0.001f;
            float amp = c->gain * env * 0.35f;
            float p = c->pan;
            l = v * amp * (p > 0.0f ? 1.0f - p : 1.0f);
            r = v * amp * (p < 0.0f ? 1.0f + p : 1.0f);
        }
        /* the clock runs while playing, silence or not -- otherwise it freezes in its own gap */
        if (c->playing) {
            int period = icecream_chime_period_ms();
            if (period < 1) period = 1;
            c->ms += 1000.0 / (double)ICECREAM_DSP_RATE;
            if (c->ms >= (double)period) c->ms -= (double)period;
        }
    }
    if (out_l) *out_l = l;
    if (out_r) *out_r = r;
}

#endif /* ICECREAM_DSP_H */
