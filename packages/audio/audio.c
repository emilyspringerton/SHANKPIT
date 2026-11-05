#include "audio.h"
#include "audio_wav.h"
#include "audio_chain.h"
#include "../simulation/siren_rules.h"   /* SIREN_KIND_SILENT: the kinds are PARENA's, not this file's */
#include "siren_dsp.h"                   /* the oscillator, header-only so it can be measured offline */
#include "wind_dsp.h"                    /* the bed's filter/smoothing/pan law, same reason */
#include "thunder_dsp.h"                 /* the rumble's five seconds, same reason */
#include "icecream_dsp.h"                /* the vendor's song, same reason (and it includes its rules) */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

/* ── Constants ──────────────────────────────────────────────────────────── */

#define SAMPLE_RATE    22050
#define NUM_CHANNELS   2        /* stereo */
#define BUFFER_FRAMES  512

#define MAX_SOUNDS     20       /* 0-5 weapons, 6-10 footstep pentatonic notes, 11-13 shield hit/break/pickup (#539-541), 14-15 the civic boss's verdict read/served, 16-19 thunder (one per variant) */
#define MAX_MIX        16       /* simultaneous effect voices */
#define MAX_CLIP_VOICES 4       /* spoken clips (TYLER VO) have their own pool, never stolen by effects */
#define NUM_VOICES     (MAX_MIX + MAX_CLIP_VOICES)
#define MAX_CLIPS      64
#define CLIP_PATH_LEN  160

/* ── Internal types ─────────────────────────────────────────────────────── */

typedef struct {
    int16_t *buf;   /* stereo interleaved frames */
    int      len;   /* number of frames (not bytes) */
} SoundBuf;

typedef struct {
    const SoundBuf *snd;
    int             pos;
    float           lgain;
    float           rgain;
    int             active;
} Voice;

/* ── Globals ────────────────────────────────────────────────────────────── */

static SoundBuf       g_snd[MAX_SOUNDS];
static Voice          g_voices[NUM_VOICES];
static SoundBuf       g_clips[MAX_CLIPS];
static char           g_clip_path[MAX_CLIPS][CLIP_PATH_LEN];
static int            g_clip_count;
static SDL_AudioDeviceID g_dev;
static SDL_mutex     *g_mutex;
static AudioChain     g_chain;        /* NOCK master chain (PARENA DSP), see audio_set_master_chain */
static int            g_chain_on;
#define CHAIN_MAX_FRAMES 8192
static float          g_mixf[CHAIN_MAX_FRAMES * NUM_CHANNELS];

/* Wind bed (founder: "add basic wind sound effects for storms"). One looping buffer of filtered noise,
 * mixed under the effects -- gain, brightness and pan are resolved by PARENA's weather rules and handed in
 * by the host through audio_set_wind, so this file never invents weather. */
#define WIND_LOOP_FRAMES (SAMPLE_RATE * 4)
#define WIND_FADE_FRAMES (SAMPLE_RATE / 2)   /* crossfaded so the 4 s loop joins without a click */
static SoundBuf g_wind;
static int      g_wind_pos;
/* The SHAPING lives in wind_dsp.h: gain, brightness and pan smoothing, the brightness one-pole and the
 * one-sided pan law. It is the part that can be measured with no device (wind_dsp_test.c), so it is the part
 * that is not in this file. What stays here is the buffer and its read position. */
static WindDsp  g_wind_dsp;

/* Thunder's whole character is that it ARRIVES LATE: a strike is SEEN first and HEARD later. A host
 * pushes one when SkyWeather counts a bolt, the delay comes from PARENA's travel-time curve, and this
 * queue holds it until it is due. Four deep -- a storm throws a bolt every 3.5-12.5 s and a rumble is 5 s
 * long, so a fifth simultaneous wait means something is wrong upstream, and dropping is better than
 * queueing a bang behind the flash that caused it. Host-thread only, like audio_clip_play's policy. */
#define THUNDER_QUEUE 4
/* frames_left, not a wall clock: the callback itself counts the wait down, so thunder is sample-accurate
 * and cannot drift with the frame rate or with a second clock the host happens to be using. */
/* Four PRE-RENDERED strikes, each with its own noise seed. A storm that plays the same waveform for every
 * bolt is the one thing a listener with eyes closed still notices, and regenerating a 5 s buffer per strike
 * would either hitch the host thread or free a buffer the mixer is still reading. So they are baked once at
 * init (4 x 441 kB) and a strike picks one -- per-bolt variation with no allocation, no free and no lock
 * ordering to get wrong. */
#define THUNDER_VARIANTS 4
static unsigned int g_thunder_seed = 0x7A11B00Bu;
static struct { int waiting; int frames_left; float gain, pan; int variant; } g_thunder[THUNDER_QUEUE];

/* The siren (founder: "add sound affordances for sirens ... classic ems and police sirens"). ONE voice,
 * because a listener hears the loudest siren near them: the host resolves which one that is with PARENA's
 * siren_rules.prn (frequency in millihertz, gain, pan) and this file only turns it into samples. The
 * frequency is integrated into a phase here, so a wail is a continuous glide and not a stepped sample. */
/* The oscillator itself lives in siren_dsp.h: it is the one part of this file that can be verified without
 * an audio device (siren_dsp_test.c renders it offline and measures pitch, pan and clipping), and keeping
 * it here as four floats meant the only way to know it worked was to listen. */
static SirenDsp g_siren;
/* The ice-cream truck's song: one voice, the loudest truck near the listener, exactly like the siren. The
 * tune and its timings are PARENA's; icecream_dsp.h only turns them into samples. */
static IceCreamDsp g_icecream;

/* ── PCM synthesis helpers ───────────────────────────────────────────────── */

static float fclampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static SoundBuf buf_alloc(int frames) {
    SoundBuf b;
    b.len = frames;
    b.buf = (int16_t *)calloc(frames * NUM_CHANNELS, sizeof(int16_t));
    return b;
}

static void buf_write(SoundBuf *b, int i, float l, float r) {
    b->buf[i * 2]     = (int16_t)(fclampf(l, -1.0f, 1.0f) * 32767.0f);
    b->buf[i * 2 + 1] = (int16_t)(fclampf(r, -1.0f, 1.0f) * 32767.0f);
}

/* 808 kick: pitch-sweep sine + click transient */
static SoundBuf synth_kick(float start_hz, float end_hz, float dur, float amp) {
    int n = (int)(SAMPLE_RATE * dur);
    SoundBuf b = buf_alloc(n);
    float phase = 0.0f;
    const float pi2 = 6.2831853f;
    for (int i = 0; i < n; i++) {
        float t   = (float)i / n;
        float hz  = start_hz + (end_hz - start_hz) * t;
        phase    += (hz / SAMPLE_RATE) * pi2;
        if (phase > pi2) phase -= pi2;
        float env = amp * expf(-t * 7.0f);
        /* transient click at attack */
        if (i < 6) env += 0.45f * (1.0f - (float)i * 0.167f);
        float s = sinf(phase) * env;
        buf_write(&b, i, s, s);
    }
    return b;
}

/* Noise burst: hi-hat / snare style */
static SoundBuf synth_noise(float dur, float amp, float decay) {
    int n = (int)(SAMPLE_RATE * dur);
    SoundBuf b = buf_alloc(n);
    uint32_t rng = 0xABCD1234u;
    for (int i = 0; i < n; i++) {
        rng   = rng * 1664525u + 1013904223u;
        float noise = (float)(int32_t)rng / 2147483648.0f;
        float t   = (float)i / n;
        float env = amp * expf(-t * decay);
        buf_write(&b, i, noise * env, noise * env);
    }
    return b;
}

/* Pure sine tone with decay envelope */
static SoundBuf synth_tone(float hz, float dur, float amp, float decay) {
    int n = (int)(SAMPLE_RATE * dur);
    SoundBuf b = buf_alloc(n);
    float phase = 0.0f;
    float dp = (hz / SAMPLE_RATE) * 6.2831853f;
    for (int i = 0; i < n; i++) {
        float t   = (float)i / n;
        float env = amp * expf(-t * decay);
        /* soft attack */
        if (i < 32) env *= (float)i / 32.0f;
        float s = sinf(phase) * env;
        phase  += dp;
        if (phase > 6.2831853f) phase -= 6.2831853f;
        buf_write(&b, i, s, s);
    }
    return b;
}

/* Layer two sounds into a new buffer (additive mix, clipped). */
static SoundBuf synth_layer(SoundBuf *a, SoundBuf *b) {
    int n = a->len > b->len ? a->len : b->len;
    SoundBuf out = buf_alloc(n);
    for (int i = 0; i < n; i++) {
        float al = (i < a->len) ? a->buf[i*2]   / 32767.0f : 0.0f;
        float ar = (i < a->len) ? a->buf[i*2+1] / 32767.0f : 0.0f;
        float bl = (i < b->len) ? b->buf[i*2]   / 32767.0f : 0.0f;
        float br = (i < b->len) ? b->buf[i*2+1] / 32767.0f : 0.0f;
        buf_write(&out, i, al + bl, ar + br);
    }
    return out;
}

/* Looping wind bed: filtered noise with a low body and a hiss on top. The raw buffer is generated a
 * half-second long, then that half second is crossfaded onto the head of the loop, so frame N joins frame
 * 0 without the tick a naive loop makes. The brightness filter that shapes it lives in wind_add below,
 * because it has to move while the buffer plays. */
static SoundBuf synth_wind_loop(void) {
    SoundBuf b = buf_alloc(WIND_LOOP_FRAMES);
    int total = WIND_LOOP_FRAMES + WIND_FADE_FRAMES;
    float *raw = (float *)calloc((size_t)total * 2, sizeof(float));
    uint32_t rng = 0x51ED270Bu;
    float lp = 0.0f, lp2 = 0.0f;
    if (!raw) return b;
    for (int i = 0; i < total; i++) {
        rng = rng * 1664525u + 1013904223u;
        float n = (float)(int32_t)rng / 2147483648.0f;
        lp  += (n - lp) * 0.12f;     /* the body of the air */
        lp2 += (lp - lp2) * 0.05f;   /* the low rumble under it */
        float s = lp2 * 0.9f + (n - lp) * 0.35f;   /* body + hiss */
        raw[i * 2] = s; raw[i * 2 + 1] = s;
    }
    for (int i = 0; i < WIND_LOOP_FRAMES; i++) {
        float l = raw[i * 2], r = raw[i * 2 + 1];
        if (i < WIND_FADE_FRAMES) {
            float t = (float)i / (float)WIND_FADE_FRAMES;
            l = raw[i * 2] * t + raw[(WIND_LOOP_FRAMES + i) * 2] * (1.0f - t);
            r = raw[i * 2 + 1] * t + raw[(WIND_LOOP_FRAMES + i) * 2 + 1] * (1.0f - t);
        }
        buf_write(&b, i, l * 0.55f, r * 0.55f);
    }
    free(raw);
    return b;
}

/* Thunder: a crack at the near end and a long rolling rumble under it. The generator itself is
 * thunder_dsp.h -- a pure fill function, so its decay and its peak can be measured offline
 * (thunder_dsp_test.c) instead of only guessed at. Five seconds, so a close strike still has a tail when the
 * next one lands. Nothing about the level is baked in: loudness and delay arrive from
 * weather_audio_rules.prn, so a distant strike is the same sound, quieter and late. */
static SoundBuf synth_thunder(unsigned int seed) {
    int n = SAMPLE_RATE * THUNDER_DSP_SECONDS;
    SoundBuf b = buf_alloc(n);
    float *tmp = (float *)calloc((size_t)n * 2, sizeof(float));
    if (!tmp) return b;
    thunder_dsp_fill(tmp, n, SAMPLE_RATE, seed);
    for (int i = 0; i < n; i++) buf_write(&b, i, tmp[i * 2], tmp[i * 2 + 1]);
    free(tmp);
    return b;
}

/* One frame of the wind bed, added to the caller's stereo accumulator. The brightness knob IS this
 * one-pole's cutoff: dull air keeps only the body of the noise, a gale opens it to the hiss -- that is the
 * whole of "it sounds different when it gets windier", one continuous number instead of a second sample.
 * Targets are smoothed per frame so a gust stepping in the sim never clicks in the mix, and the pan uses
 * the same one-sided law as spatial_gains so wind and gunshots agree about left and right. */
static void wind_add(float *dl, float *dr) {
    if (!g_wind.buf || g_wind.len <= 0) return;
    float raw = (float)g_wind.buf[g_wind_pos * 2] / 32767.0f;
    if (++g_wind_pos >= g_wind.len) g_wind_pos = 0;
    float wl = 0.0f, wr = 0.0f;
    wind_dsp_frame(&g_wind_dsp, raw, &wl, &wr);
    *dl += wl;
    *dr += wr;
}

/* One frame of the siren, added to the caller's stereo accumulator. A siren is a TONE, not a buffer: the
 * phase is integrated from PARENA's millihertz every frame, so an EMS hi-lo steps between its two notes and
 * a police wail glides, both without a sample file. The second harmonic gives it the horn bite that a pure
 * sine lacks; the gain smoothing is faster than the wind bed's because a siren has to START when the driver
 * hits boost, not swell in over a second. Pan is the same one-sided law as everything else. */
static void siren_add(float *dl, float *dr) {
    float sl = 0.0f, sr = 0.0f;
    siren_dsp_step(&g_siren, &sl, &sr);
    *dl += sl;
    *dr += sr;
}

/* One frame of the vendor's song, added to the caller's accumulator. The song clock lives in the DSP and only
 * advances while something is playing, so a truck that stops keeps its phase and one that resumes restarts
 * its phrase from the top (a jingle that drops in mid-tune sounds broken). */
static void icecream_add(float *dl, float *dr) {
    float sl = 0.0f, sr = 0.0f;
    icecream_dsp_step(&g_icecream, &sl, &sr);
    *dl += sl;
    *dr += sr;
}

/* ── SDL audio callback (runs on audio thread) ───────────────────────────── */

static void thunder_service(int frames);   /* defined with the voice dispatch below */

static void audio_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    int frames = len / (NUM_CHANNELS * sizeof(int16_t));
    int16_t *out = (int16_t *)stream;
    memset(out, 0, len);

    SDL_LockMutex(g_mutex);
    /* service thunder here, INSIDE the lock and before the mix: a delayed strike starts on the exact
       buffer its travel time expires in, not on the next render frame. */
    thunder_service(frames);
    if (g_chain_on && frames <= CHAIN_MAX_FRAMES) {
        /* NOCK master chain: mix in float, run the PARENA chain, convert once at the end. */
        memset(g_mixf, 0, sizeof(float) * (size_t)frames * NUM_CHANNELS);
        for (int ch = 0; ch < NUM_VOICES; ch++) {
            Voice *v = &g_voices[ch];
            if (!v->active || !v->snd || !v->snd->buf) continue;
            for (int i = 0; i < frames && v->pos < v->snd->len; i++, v->pos++) {
                g_mixf[i * 2]     += v->snd->buf[v->pos * 2]     / 32767.0f * v->lgain;
                g_mixf[i * 2 + 1] += v->snd->buf[v->pos * 2 + 1] / 32767.0f * v->rgain;
            }
            if (v->pos >= v->snd->len) v->active = 0;
        }
        /* the wind bed goes through the same master chain as everything else, before it is processed */
        for (int i = 0; i < frames; i++) { wind_add(&g_mixf[i * 2], &g_mixf[i * 2 + 1]);
                                            siren_add(&g_mixf[i * 2], &g_mixf[i * 2 + 1]); }
        audio_chain_process(&g_chain, g_mixf, frames, NUM_CHANNELS);
        for (int i = 0; i < frames * NUM_CHANNELS; i++) out[i] = (int16_t)(fclampf(g_mixf[i], -1.0f, 1.0f) * 32767.0f);
        SDL_UnlockMutex(g_mutex);
        return;
    }
    for (int ch = 0; ch < NUM_VOICES; ch++) {
        Voice *v = &g_voices[ch];
        if (!v->active || !v->snd || !v->snd->buf) continue;
        for (int i = 0; i < frames && v->pos < v->snd->len; i++, v->pos++) {
            float sl = v->snd->buf[v->pos * 2]     / 32767.0f * v->lgain;
            float sr = v->snd->buf[v->pos * 2 + 1] / 32767.0f * v->rgain;
            float ol = out[i * 2]     / 32767.0f + sl;
            float or_ = out[i * 2 + 1] / 32767.0f + sr;
            out[i * 2]     = (int16_t)(fclampf(ol,  -1.0f, 1.0f) * 32767.0f);
            out[i * 2 + 1] = (int16_t)(fclampf(or_, -1.0f, 1.0f) * 32767.0f);
        }
        if (v->pos >= v->snd->len) v->active = 0;
    }
    /* dry path: the bed is summed into the same int16 frames the effects wrote */
    for (int i = 0; i < frames; i++) {
        float wl = 0.0f, wr = 0.0f;
        wind_add(&wl, &wr);
        siren_add(&wl, &wr);
        icecream_add(&wl, &wr);
        out[i * 2]     = (int16_t)(fclampf(out[i * 2]     / 32767.0f + wl, -1.0f, 1.0f) * 32767.0f);
        out[i * 2 + 1] = (int16_t)(fclampf(out[i * 2 + 1] / 32767.0f + wr, -1.0f, 1.0f) * 32767.0f);
    }
    SDL_UnlockMutex(g_mutex);
}

/* ── Spatial gain calculation ────────────────────────────────────────────── */

/* Compute per-channel gains from source→listener geometry.
 * lyaw = listener's yaw in radians (0 = facing +Z, π/2 = facing +X). */
static void spatial_gains(float sx, float sy, float sz,
                           float lx, float ly, float lz,
                           float lyaw,
                           float *out_l, float *out_r) {
    float dx = sx - lx;
    float dy = sy - ly;
    float dz = sz - lz;
    float dist2 = dx*dx + dy*dy + dz*dz;

    /* Soft distance rolloff: full volume at dist=0, ~50% at 100 units */
    float vol = 1.0f / (1.0f + dist2 * 0.0001f);
    if (vol > 1.0f) vol = 1.0f;

    /* Angle from listener forward to source in XZ plane */
    float angle_src = atan2f(dx, dz);
    float rel = angle_src - lyaw;
    /* pan: +1.0 = source is to listener's right */
    float pan = sinf(rel);

    /* Linear pan: left gain drops as pan increases; right drops as pan decreases */
    *out_l = vol * (1.0f - (pan > 0.0f ? pan : 0.0f));
    *out_r = vol * (1.0f - (pan < 0.0f ? -pan : 0.0f));
}

/* ── Voice dispatch ──────────────────────────────────────────────────────── */

/* start_voice -- the dispatch, assuming the caller already holds g_mutex. Split out of play_sound so the
 * audio callback (which holds the lock for the whole buffer) can start a thunder rumble without deadlocking
 * on a second lock. */
static void start_voice(int snd_id, float lgain, float rgain) {
    if (snd_id < 0 || snd_id >= MAX_SOUNDS) return;
    if (!g_snd[snd_id].buf) return;
    /* Find a free voice; steal the furthest-along if all busy */
    int slot = -1;
    int best_pos = -1;
    for (int i = 0; i < MAX_MIX; i++) {
        if (!g_voices[i].active) { slot = i; break; }
        if (g_voices[i].pos > best_pos) { best_pos = g_voices[i].pos; slot = i; }
    }
    g_voices[slot].snd    = &g_snd[snd_id];
    g_voices[slot].pos    = 0;
    g_voices[slot].lgain  = lgain;
    g_voices[slot].rgain  = rgain;
    g_voices[slot].active = 1;
}

static void play_sound(int snd_id, float lgain, float rgain) {
    if (!g_dev || !g_mutex) return;
    SDL_LockMutex(g_mutex);
    start_voice(snd_id, lgain, rgain);
    SDL_UnlockMutex(g_mutex);
}

/* ── Sound bank indices ──────────────────────────────────────────────────── */
/* 0=WPN_KNIFE 1=WPN_MAGNUM 2=WPN_AR 3=WPN_SHOTGUN 4=WPN_SNIPER 5=WPN_KATANA */
/* 6-10 = pentatonic footstep notes (C4 D4 E4 G4 A4) */

/* Cymbal/crash: noise pushed through a double first-difference (a cheap high-pass, so only the fizz is left)
 * plus six inharmonic partials below Nyquist (11025 Hz at the 22050 Hz device rate), long exponential decay. */
static SoundBuf synth_cymbal(float dur, float amp) {
    int n = (int)(SAMPLE_RATE * dur);
    SoundBuf b = buf_alloc(n);
    static const float PARTIAL_HZ[6] = { 3140.0f, 4217.0f, 5333.0f, 6491.0f, 7757.0f, 9319.0f };
    float ph[6] = {0};
    uint32_t rng = 0x5EEDBEEFu;
    float n1 = 0.0f, n2 = 0.0f;
    for (int i = 0; i < n; i++) {
        rng = rng * 1664525u + 1013904223u;
        float noise = (float)(int32_t)rng / 2147483648.0f;
        float hp = noise - 2.0f * n1 + n2;      /* second difference */
        n2 = n1; n1 = noise;
        float t = (float)i / n;
        float env = amp * expf(-t * 5.5f);
        float tones = 0.0f;
        for (int k = 0; k < 6; k++) {
            tones += sinf(ph[k]) * 0.07f;
            ph[k] += (PARTIAL_HZ[k] / SAMPLE_RATE) * 6.2831853f;
            if (ph[k] > 6.2831853f) ph[k] -= 6.2831853f;
        }
        float s = (hp * 0.28f + tones) * env;
        if (i < 24) s *= (float)i / 24.0f;      /* soft attack, no click */
        buf_write(&b, i, s, s);
    }
    return b;
}

/* Rising three-note chime (shield pack pickup): E5, A5, E6, each ringing ~0.3 s, 70 ms apart. */
static SoundBuf synth_chime(float amp) {
    static const float NOTE_HZ[3] = { 659.25f, 880.0f, 1318.51f };
    int n = (int)(SAMPLE_RATE * 0.50f);
    SoundBuf b = buf_alloc(n);
    for (int k = 0; k < 3; k++) {
        int start = (int)(SAMPLE_RATE * 0.07f * k);
        float dp = (NOTE_HZ[k] / SAMPLE_RATE) * 6.2831853f, phase = 0.0f;
        for (int i = start; i < n; i++) {
            float t = (float)(i - start) / (float)(n - start);
            float env = amp * expf(-t * 7.0f);
            if (i - start < 24) env *= (float)(i - start) / 24.0f;
            float s = sinf(phase) * env + sinf(phase * 2.0f) * env * 0.25f;
            phase += dp;
            if (phase > 6.2831853f) phase -= 6.2831853f;
            float l = b.buf[i * 2] / 32767.0f;
            buf_write(&b, i, l + s, l + s);
        }
    }
    return b;
}

static const float PENTATONIC_HZ[5] = { 261.63f, 293.66f, 329.63f, 392.00f, 440.00f };

/* ── Public API ─────────────────────────────────────────────────────────── */

void audio_init(void) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        SDL_Log("[audio] SDL_InitSubSystem(AUDIO) failed: %s", SDL_GetError());
        return;
    }

    g_mutex = SDL_CreateMutex();
    if (!g_mutex) { SDL_Log("[audio] mutex create failed"); return; }

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq     = SAMPLE_RATE;
    want.format   = AUDIO_S16SYS;
    want.channels = NUM_CHANNELS;
    want.samples  = BUFFER_FRAMES;
    want.callback = audio_callback;

    g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!g_dev) {
        SDL_Log("[audio] SDL_OpenAudioDevice failed: %s", SDL_GetError());
        return;
    }

    /* ── Synthesize weapon sounds ─── */

    /* 0: Knife — tight metallic tone + hi-hat */
    {
        SoundBuf tone  = synth_tone(1600.0f, 0.035f, 0.5f, 40.0f);
        SoundBuf hat   = synth_noise(0.028f, 0.4f, 60.0f);
        SoundBuf mix   = synth_layer(&tone, &hat);
        free(tone.buf); free(hat.buf);
        g_snd[0] = mix;
    }
    /* 1: Magnum — deep 808 kick */
    g_snd[1] = synth_kick(200.0f, 38.0f, 0.22f, 0.95f);

    /* 2: AR — rapid hi-hat burst */
    g_snd[2] = synth_noise(0.022f, 0.65f, 80.0f);

    /* 3: Shotgun — layered kick + heavy noise */
    {
        SoundBuf kick  = synth_kick(175.0f, 52.0f, 0.14f, 0.85f);
        SoundBuf blast = synth_noise(0.12f, 0.75f, 18.0f);
        SoundBuf mix   = synth_layer(&kick, &blast);
        free(kick.buf); free(blast.buf);
        g_snd[3] = mix;
    }
    /* 4: Sniper — sub bass boom (very low sweep) */
    g_snd[4] = synth_kick(85.0f, 16.0f, 0.50f, 0.92f);

    /* 5: Katana — mid metallic tone + hi-hat slice */
    {
        SoundBuf tone  = synth_tone(880.0f, 0.07f, 0.55f, 30.0f);
        SoundBuf hat   = synth_noise(0.065f, 0.5f, 35.0f);
        SoundBuf mix   = synth_layer(&tone, &hat);
        free(tone.buf); free(hat.buf);
        g_snd[5] = mix;
    }

    /* ── Synthesize pentatonic footstep notes (6-10) ─── */
    for (int n = 0; n < 5; n++) {
        g_snd[6 + n] = synth_tone(PENTATONIC_HZ[n], 0.10f, 0.38f, 22.0f);
    }

    /* 11: shield hit -- a short downward electric zap + a crackle of noise */
    {
        SoundBuf zap = synth_kick(1500.0f, 380.0f, 0.10f, 0.50f);
        SoundBuf crk = synth_noise(0.06f, 0.30f, 45.0f);
        g_snd[11] = synth_layer(&zap, &crk);
        free(zap.buf); free(crk.buf);
    }
    /* 12: shield break -- snare (noise crack + 190 Hz body) over a crash cymbal, ~0.8 s */
    {
        SoundBuf crack = synth_noise(0.20f, 0.50f, 13.0f);
        SoundBuf body  = synth_tone(190.0f, 0.14f, 0.45f, 20.0f);
        SoundBuf snare = synth_layer(&crack, &body);
        SoundBuf crash = synth_cymbal(0.80f, 0.60f);
        g_snd[12] = synth_layer(&snare, &crash);
        free(crack.buf); free(body.buf); free(snare.buf); free(crash.buf);
    }
    /* 13: shield pack pickup -- rising chime */
    g_snd[13] = synth_chime(0.42f);

    /* ── The civic boss's verdict (AI_ROLE_MAGISTRATE) ───────────
       14: the sentence being READ -- a gavel rap on wood: a short high-to-mid pitch sweep (the block,
           not a gunshot) plus a tight woody crack, deliberately quieter than any weapon so it reads as
           "something is about to happen" and never as "you are being shot at". Its whole job is to be
           audible through the magnum fire that is still going on around it. */
    {
        SoundBuf block = synth_kick(430.0f, 165.0f, 0.060f, 0.52f);
        SoundBuf crack = synth_noise(0.038f, 0.26f, 62.0f);
        g_snd[14] = synth_layer(&block, &crack);
        free(block.buf); free(crack.buf);
    }
    /* 15: the sentence SERVED -- the same gavel, but heavy: a deep 110->26 Hz body sweep, a cymbal's
       fizz on top for the room, so the two cues are obviously the same object hit twice, once to call
       order and once to end the argument. */
    {
        SoundBuf body  = synth_kick(110.0f, 26.0f, 0.40f, 0.92f);
        SoundBuf fizz  = synth_cymbal(0.55f, 0.30f);
        SoundBuf stamp = synth_layer(&body, &fizz);
        SoundBuf knock = synth_noise(0.05f, 0.30f, 40.0f);
        g_snd[15] = synth_layer(&stamp, &knock);
        free(body.buf); free(fizz.buf); free(stamp.buf); free(knock.buf);
    }

    /* 16-19: thunder, one buffer per variant. Held until its travel time is up -- see
       audio_play_thunder/thunder_service -- and the strike that lands picks a variant, so a storm's bolts
       are not the same waveform over and over. */
    for (int tv = 0; tv < THUNDER_VARIANTS; tv++)
        g_snd[16 + tv] = synth_thunder(0x9E3779B9u * (unsigned int)(tv + 1) + 0x7A11B00Bu);

    {
        const char *chain = getenv("SHANKPIT_SOUND_CHAIN");
        if (chain && *chain) {
            if (audio_set_master_chain(chain)) SDL_Log("[audio] NOCK master chain '%s' live", chain);
            else SDL_Log("[audio] NOCK master chain '%s' could not be loaded; playing dry", chain);
        }
    }

    /* the wind bed is silent until a host calls audio_set_wind -- a host that knows nothing about
       weather gets exactly the mix it had before (this is the only ambient voice in the engine) */
    g_wind = synth_wind_loop();
    g_wind_pos = 0;
    wind_dsp_init(&g_wind_dsp);

    /* the siren is silent until a host calls audio_set_siren -- a host with no sirens gets the mix it had */
    siren_dsp_init(&g_siren);
    /* and the vendor's song is silent until a host reports a truck out selling */
    icecream_dsp_init(&g_icecream);
    memset(g_thunder, 0, sizeof(g_thunder));

    SDL_PauseAudioDevice(g_dev, 0); /* start playback */
    SDL_Log("[audio] initialized: %d Hz stereo, %d sound buffers", SAMPLE_RATE, MAX_SOUNDS);
}

void audio_shutdown(void) {
    if (g_dev) {
        SDL_CloseAudioDevice(g_dev);
        g_dev = 0;
    }
    if (g_mutex) {
        SDL_DestroyMutex(g_mutex);
        g_mutex = NULL;
    }
    for (int i = 0; i < g_clip_count; i++) { free(g_clips[i].buf); g_clips[i].buf = NULL; }
    g_clip_count = 0;
    for (int i = 0; i < MAX_SOUNDS; i++) {
        if (g_snd[i].buf) {
            free(g_snd[i].buf);
            g_snd[i].buf = NULL;
        }
    }
    free(g_wind.buf);
    g_wind.buf = NULL;
    g_wind.len = 0;
    memset(g_thunder, 0, sizeof(g_thunder));   /* a stale queue must not fire into a new device */
}

void audio_play_weapon(int weapon_idx,
                       float sx, float sy, float sz,
                       float lx, float ly, float lz,
                       float lyaw) {
    if (!g_dev || !g_mutex) return;
    if (weapon_idx < 0 || weapon_idx > 5) return;
    float lgain, rgain;
    spatial_gains(sx, sy, sz, lx, ly, lz, lyaw, &lgain, &rgain);
    play_sound(weapon_idx, lgain, rgain);
}

void audio_play_footstep(float sx, float sy, float sz,
                         float lx, float ly, float lz,
                         float lyaw, int step_index) {
    if (!g_dev || !g_mutex) return;
    int note = ((step_index % 5) + 5) % 5;  /* handle negatives */
    float lgain, rgain;
    spatial_gains(sx, sy, sz, lx, ly, lz, lyaw, &lgain, &rgain);
    /* Footsteps are quieter than weapons */
    play_sound(6 + note, lgain * 0.35f, rgain * 0.35f);
}

/* Shield sounds (cards #539-541): kind 0 = hit zap, 1 = break (snare + cymbal), 2 = pack pickup chime. */
void audio_play_shield(int kind, float sx, float sy, float sz,
                       float lx, float ly, float lz, float lyaw) {
    if (!g_dev || !g_mutex) return;
    if (kind < 0 || kind > 2) return;
    float lgain, rgain;
    spatial_gains(sx, sy, sz, lx, ly, lz, lyaw, &lgain, &rgain);
    play_sound(11 + kind, lgain, rgain);
}

/* The civic boss's verdict: kind 0 = read (the telegraph), 1 = served. Same spatial_gains call as a
 * weapon shot, so panning and distance falloff are shared with every other sound rather than a
 * second, differently-wrong pan law living in the boss code. */
void audio_play_magistrate(int kind, float sx, float sy, float sz,
                           float lx, float ly, float lz, float lyaw) {
    if (!g_dev || !g_mutex) return;
    if (kind < 0 || kind > 1) return;
    float lgain, rgain;
    spatial_gains(sx, sy, sz, lx, ly, lz, lyaw, &lgain, &rgain);
    play_sound(14 + kind, lgain, rgain);
}

/* The wind bed. Called every frame with numbers PARENA's weather rules already resolved (gain 0..1,
 * brightness 0..1, pan -1..1); this function only stores smoothed targets, so it is safe to call from the
 * render loop while the audio thread is mixing. */
void audio_set_wind(float gain, float bright, float pan) {
    if (!g_dev || !g_mutex) return;
    SDL_LockMutex(g_mutex);
    wind_dsp_set(&g_wind_dsp, gain, bright, pan);
    SDL_UnlockMutex(g_mutex);
}

/* The ice-cream truck's song. Called every frame with numbers PARENA's icecream rules already resolved
 * (whether a vendor is out and its speed, then gain and pan at the listener); this only stores smoothed
 * targets, so it is safe from the render loop while the audio thread mixes. */
void audio_set_icecream(int playing, float gain, float pan) {
    if (!g_dev || !g_mutex) return;
    SDL_LockMutex(g_mutex);
    icecream_dsp_set(&g_icecream, playing, gain, pan);
    SDL_UnlockMutex(g_mutex);
}

/* Thunder: see g_thunder at the top of this file for the queue's own policy. */
void audio_play_thunder(int delay_ms, float gain, float pan) {
    if (!g_dev || !g_mutex) return;
    if (delay_ms < 0) delay_ms = 0;
    if (delay_ms > 60000) delay_ms = 60000;
    int slot = -1;
    for (int i = 0; i < THUNDER_QUEUE; i++) {
        if (!g_thunder[i].waiting) { slot = i; break; }
    }
    if (slot < 0) {   /* all four already waiting: retire the oldest, keep the newest */
        for (int i = 1; i < THUNDER_QUEUE; i++) g_thunder[i - 1] = g_thunder[i];
        slot = THUNDER_QUEUE - 1;
    }
    g_thunder[slot].waiting = 1;
    g_thunder[slot].frames_left = (delay_ms * SAMPLE_RATE) / 1000;
    g_thunder[slot].gain = fclampf(gain, 0.0f, 1.0f);
    g_thunder[slot].pan = fclampf(pan, -1.0f, 1.0f);
    /* this bolt's own identity: a fresh seed, so the next one does not roll the same dice */
    g_thunder_seed = g_thunder_seed * 1664525u + 1013904223u;
    g_thunder[slot].variant = (int)((g_thunder_seed >> 16) % (unsigned int)THUNDER_VARIANTS);
}

/* Called from the audio callback with the lock held: count each waiting strike down by this buffer and start
 * the ones whose time is up. Sample-accurate by construction -- no clock to read, nothing to drift. */
static void thunder_service(int frames) {
    for (int i = 0; i < THUNDER_QUEUE; i++) {
        if (!g_thunder[i].waiting) continue;
        g_thunder[i].frames_left -= frames;
        if (g_thunder[i].frames_left > 0) continue;
        g_thunder[i].waiting = 0;
        float p = g_thunder[i].pan;
        start_voice(16 + g_thunder[i].variant, g_thunder[i].gain * (p > 0.0f ? 1.0f - p : 1.0f),
                        g_thunder[i].gain * (p < 0.0f ? 1.0f + p : 1.0f));
    }
}

/* The siren voice. kind is a SIREN_KIND_* (0 silences it, with the gain ramping down rather than cutting),
 * hz_milli the frequency PARENA's siren rules resolved for this frame, gain 0..1 and pan -1..1. Stores
 * targets only, so it is safe to call from a render loop while the audio thread is mixing. */
void audio_set_siren(int kind, int hz_milli, float gain, float pan) {
    if (!g_dev || !g_mutex) return;
    SDL_LockMutex(g_mutex);
    siren_dsp_set(&g_siren, kind, (hz_milli > 0) ? (float)hz_milli * 0.001f : -1.0f, gain, pan);
    SDL_UnlockMutex(g_mutex);
}

int audio_clip_load(const char *wav_path) {
    if (!wav_path || !g_dev || !g_mutex) return -1;
    for (int i = 0; i < g_clip_count; i++)
        if (strcmp(g_clip_path[i], wav_path) == 0) return g_clips[i].buf ? i : -1;
    if (g_clip_count >= MAX_CLIPS || strlen(wav_path) >= CLIP_PATH_LEN) return -1;
    int16_t *pcm = NULL;
    int frames = audio_wav_load_stereo16(wav_path, &pcm);
    int id = g_clip_count++;
    snprintf(g_clip_path[id], CLIP_PATH_LEN, "%s", wav_path);   /* remember failures too: no per-frame disk retry */
    g_clips[id].buf = frames > 0 ? pcm : NULL;
    g_clips[id].len = frames > 0 ? frames : 0;
    if (frames <= 0) SDL_Log("[audio] clip not loaded (missing or not 22050 Hz PCM16): %s", wav_path);
    return g_clips[id].buf ? id : -1;
}

void audio_clip_play(int clip_id, unsigned int seek_ms,
                     float sx, float sy, float sz,
                     float lx, float ly, float lz, float lyaw) {
    if (!g_dev || !g_mutex || clip_id < 0 || clip_id >= g_clip_count || !g_clips[clip_id].buf) return;
    int start = audio_wav_seek_frame(seek_ms, g_clips[clip_id].len);
    if (start >= g_clips[clip_id].len) return;
    float lgain, rgain;
    spatial_gains(sx, sy, sz, lx, ly, lz, lyaw, &lgain, &rgain);
    SDL_LockMutex(g_mutex);
    int slot = MAX_MIX;
    for (int i = MAX_MIX; i < NUM_VOICES; i++) if (!g_voices[i].active) { slot = i; break; }
    g_voices[slot].snd = &g_clips[clip_id];
    g_voices[slot].pos = start;
    g_voices[slot].lgain = lgain;
    g_voices[slot].rgain = rgain;
    g_voices[slot].active = 1;
    SDL_UnlockMutex(g_mutex);
}

void audio_clip_stop_all(void) {
    if (!g_mutex) return;
    SDL_LockMutex(g_mutex);
    for (int i = MAX_MIX; i < NUM_VOICES; i++) g_voices[i].active = 0;
    SDL_UnlockMutex(g_mutex);
}

int audio_set_master_chain(const char *name) {
    if (!g_mutex) return 0;
    if (!name) {
        SDL_LockMutex(g_mutex);
        g_chain_on = 0;
        SDL_UnlockMutex(g_mutex);
        return 1;
    }
    const char *base = getenv("SHANKPIT_SOUND_CHAIN_URL");
    AudioChain c;
    /* fetch outside the lock: it's a network call */
    if (!audio_chain_fetch(base && *base ? base : "https://okemily.com/api/v1/nock-sound-filters", name, SAMPLE_RATE, &c)) return 0;
    SDL_LockMutex(g_mutex);
    g_chain = c;
    g_chain_on = 1;
    SDL_UnlockMutex(g_mutex);
    return 1;
}
