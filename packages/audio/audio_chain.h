#ifndef AUDIO_CHAIN_H
#define AUDIO_CHAIN_H

/* audio_chain -- runs a NOCK filter chain inside the engine (founder real-time, 2026-09-27: "nock
 * and shankpit engine need sound engineering primatives ... pass filters around").
 *
 * A chain is authored and auditioned in IDUNA's NOCK "Sounds" tab, saved by name, and fetched here
 * from IDUNA's public read-only route GET /api/v1/nock-sound-filters/<name>. Its JSON is the
 * version-1 format IDUNA validates (internal/nock/sound_store.go FilterChain). Every coefficient
 * and gain decision comes from audio_dsp_gen.c -- C compiled from PARENA stdlib/audio/dsp.prn,
 * the same source NOCK compiles to TypeScript -- so a chain sounds the same in both places.
 *
 * Realtime semantics (the engine processes a live stream, not a whole file):
 *   biquad / compressor / expander / deesser / limiter / gain  -- identical to NOCK
 *   normalize -- needs whole-file loudness, so in realtime it applies the static gain
 *                (target_lufs - measured_lufs) only if audio_chain_set_measured_lufs() was
 *                called; otherwise it is a no-op. (NOCK applies it offline, jivetalking-style.)
 */

#define AUDIO_CHAIN_MAX_STAGES 32
#define AUDIO_CHAIN_MAX_CH 2

typedef enum {
    ACS_BIQUAD, ACS_COMPRESSOR, ACS_EXPANDER, ACS_DEESSER, ACS_LIMITER, ACS_GAIN, ACS_NORMALIZE
} AudioChainStageType;

typedef struct {
    AudioChainStageType type;
    int bypass;
    int kind;
    double freq, q, gain_db, threshold_db, ratio, knee_db, range_db, attack_ms, release_ms,
           makeup_db, intensity, ceiling_db, target_lufs, mix;
    /* prepared state (audio_chain_prepare) */
    double b0, b1, b2, a1, a2, att, rel;
    double s1[AUDIO_CHAIN_MAX_CH], s2[AUDIO_CHAIN_MAX_CH];
    double env, env2, g;
} AudioChainStage;

typedef struct {
    int count;
    AudioChainStage st[AUDIO_CHAIN_MAX_STAGES];
    double sample_rate;
    double measured_lufs; /* for realtime normalize; 0 = unknown */
} AudioChain;

/* Parses a chain JSON document -- either the bare {"version":1,"stages":[...]} or IDUNA's
 * {"name":..,"chain":{...}} envelope. Returns 1 on success, 0 on malformed/unsupported input
 * (unknown stage type, wrong version, too many stages). */
int audio_chain_parse_json(const char *json, AudioChain *out);

/* Computes coefficients/time constants for `sample_rate` and resets filter state. */
void audio_chain_prepare(AudioChain *c, double sample_rate);

void audio_chain_set_measured_lufs(AudioChain *c, double lufs);

/* Processes `frames` interleaved frames of `channels` (1 or 2) float samples in place. */
void audio_chain_process(AudioChain *c, float *interleaved, int frames, int channels);

/* Fetches chain `name` from `base_url` (e.g. "https://okemily.com/api/v1/nock-sound-filters")
 * via curl, parses and prepares it. Name must match [A-Za-z0-9][A-Za-z0-9_-]{0,63} (IDUNA's own
 * name rule), which also keeps it safe to pass to the shell. Returns 1 on success. */
int audio_chain_fetch(const char *base_url, const char *name, double sample_rate, AudioChain *out);

#endif /* AUDIO_CHAIN_H */
