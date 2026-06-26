#include "audio.h"
#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

/* ── Constants ──────────────────────────────────────────────────────────── */

#define SAMPLE_RATE    22050
#define NUM_CHANNELS   2        /* stereo */
#define BUFFER_FRAMES  512

#define MAX_SOUNDS     12       /* 0-5 weapons, 6-10 footstep pentatonic notes */
#define MAX_MIX        16       /* simultaneous voices */

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
static Voice          g_voices[MAX_MIX];
static SDL_AudioDeviceID g_dev;
static SDL_mutex     *g_mutex;

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

/* ── SDL audio callback (runs on audio thread) ───────────────────────────── */

static void audio_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    int frames = len / (NUM_CHANNELS * sizeof(int16_t));
    int16_t *out = (int16_t *)stream;
    memset(out, 0, len);

    SDL_LockMutex(g_mutex);
    for (int ch = 0; ch < MAX_MIX; ch++) {
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

static void play_sound(int snd_id, float lgain, float rgain) {
    if (snd_id < 0 || snd_id >= MAX_SOUNDS) return;
    if (!g_snd[snd_id].buf) return;

    SDL_LockMutex(g_mutex);
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
    SDL_UnlockMutex(g_mutex);
}

/* ── Sound bank indices ──────────────────────────────────────────────────── */
/* 0=WPN_KNIFE 1=WPN_MAGNUM 2=WPN_AR 3=WPN_SHOTGUN 4=WPN_SNIPER 5=WPN_KATANA */
/* 6-10 = pentatonic footstep notes (C4 D4 E4 G4 A4) */

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
    for (int i = 0; i < MAX_SOUNDS; i++) {
        if (g_snd[i].buf) {
            free(g_snd[i].buf);
            g_snd[i].buf = NULL;
        }
    }
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
