/* audio_chain.c -- see audio_chain.h. Host loop only: the math is audio_dsp_gen.c (PARENA). */
#define _POSIX_C_SOURCE 200809L
#include "audio_chain.h"
#include "audio_dsp_gen.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32 /* mingw: the engine's Windows build */
#define popen _popen
#define pclose _pclose
#endif

/* ---- minimal JSON scanning for the chain format (flat objects of numbers/bools/strings) ---- */

static const char *skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

/* Finds the object value of `key` at the top level of the object starting at `obj` ('{').
 * Good enough for IDUNA's canonical output; not a general JSON parser. */
static const char *find_key(const char *obj, const char *key) {
    size_t klen = strlen(key);
    int depth = 0;
    for (const char *p = obj; *p; p++) {
        if (*p == '"') {
            const char *s = p + 1;
            const char *e = strchr(s, '"');
            if (!e) return NULL;
            if (depth == 1 && (size_t)(e - s) == klen && strncmp(s, key, klen) == 0) {
                const char *v = skip_ws(e + 1);
                if (*v == ':') return skip_ws(v + 1);
            }
            p = e;
        } else if (*p == '{' || *p == '[') {
            depth++;
        } else if (*p == '}' || *p == ']') {
            if (--depth == 0) return NULL;
        }
    }
    return NULL;
}

static double num_or(const char *obj, const char *key, double dflt) {
    const char *v = find_key(obj, key);
    if (!v) return dflt;
    char *end;
    double d = strtod(v, &end);
    return end == v ? dflt : d;
}

static int bool_of(const char *obj, const char *key) {
    const char *v = find_key(obj, key);
    return v && strncmp(v, "true", 4) == 0;
}

static int str_is(const char *obj, const char *key, const char *want) {
    const char *v = find_key(obj, key);
    if (!v || *v != '"') return 0;
    size_t n = strlen(want);
    return strncmp(v + 1, want, n) == 0 && v[1 + n] == '"';
}

static const char *match_close(const char *p) { /* p at '{' or '[' */
    int depth = 0;
    int in_str = 0;
    for (; *p; p++) {
        if (in_str) { if (*p == '\\' && p[1]) p++; else if (*p == '"') in_str = 0; continue; }
        if (*p == '"') in_str = 1;
        else if (*p == '{' || *p == '[') depth++;
        else if ((*p == '}' || *p == ']') && --depth == 0) return p;
    }
    return NULL;
}

int audio_chain_parse_json(const char *json, AudioChain *out) {
    memset(out, 0, sizeof(*out));
    const char *root = skip_ws(json);
    if (*root != '{') return 0;
    const char *inner = find_key(root, "chain");
    if (inner && *inner == '{') root = inner;
    if (num_or(root, "version", 0) != 1) return 0;
    const char *arr = find_key(root, "stages");
    if (!arr || *arr != '[') return 0;
    const char *end = match_close(arr);
    if (!end) return 0;
    const char *p = arr + 1;
    static const char *names[] = {"biquad", "compressor", "expander", "deesser", "limiter", "gain", "normalize"};
    while (p < end) {
        p = skip_ws(p);
        if (*p == ',') { p++; continue; }
        if (*p != '{') break;
        const char *oe = match_close(p);
        if (!oe || out->count >= AUDIO_CHAIN_MAX_STAGES) return 0;
        size_t len = (size_t)(oe - p + 1);
        char *obj = malloc(len + 1);
        if (!obj) return 0;
        memcpy(obj, p, len);
        obj[len] = 0;
        AudioChainStage *s = &out->st[out->count];
        int t = -1;
        for (int i = 0; i < 7; i++) if (str_is(obj, "type", names[i])) t = i;
        if (t < 0) { free(obj); return 0; }
        s->type = (AudioChainStageType)t;
        s->bypass = bool_of(obj, "bypass");
        s->kind = (int)num_or(obj, "kind", 1);
        s->freq = num_or(obj, "freq", t == ACS_DEESSER ? 6000 : 1000);
        s->q = num_or(obj, "q", 0.707);
        s->gain_db = num_or(obj, "gain_db", 0);
        s->threshold_db = num_or(obj, "threshold_db", t == ACS_EXPANDER ? -50 : -20);
        s->ratio = num_or(obj, "ratio", t == ACS_EXPANDER ? 2 : 3);
        s->knee_db = num_or(obj, "knee_db", 0);
        s->range_db = num_or(obj, "range_db", -40);
        s->attack_ms = num_or(obj, "attack_ms", t == ACS_DEESSER ? 1 : 10);
        s->release_ms = num_or(obj, "release_ms", t == ACS_LIMITER ? 50 : t == ACS_DEESSER ? 60 : t == ACS_EXPANDER ? 250 : 200);
        s->makeup_db = num_or(obj, "makeup_db", 0);
        s->intensity = num_or(obj, "intensity", 0);
        s->ceiling_db = num_or(obj, "ceiling_db", t == ACS_NORMALIZE ? -1.5 : -1);
        s->target_lufs = num_or(obj, "target_lufs", -18);
        s->mix = num_or(obj, "mix", 1);
        free(obj);
        out->count++;
        p = oe + 1;
    }
    return 1;
}

static void design(AudioChainStage *s, int kind, double f, double q, double g, double sr) {
    s->b0 = biquad_b0(kind, f, q, g, sr); s->b1 = biquad_b1(kind, f, q, g, sr);
    s->b2 = biquad_b2(kind, f, q, g, sr); s->a1 = biquad_a1(kind, f, q, g, sr);
    s->a2 = biquad_a2(kind, f, q, g, sr);
}

void audio_chain_prepare(AudioChain *c, double sr) {
    c->sample_rate = sr;
    for (int i = 0; i < c->count; i++) {
        AudioChainStage *s = &c->st[i];
        memset(s->s1, 0, sizeof s->s1);
        memset(s->s2, 0, sizeof s->s2);
        s->env = s->env2 = s->g = 0;
        s->att = time_coef(s->attack_ms, sr);
        s->rel = time_coef(s->release_ms, sr);
        if (s->type == ACS_BIQUAD) design(s, s->kind, fmin(s->freq, sr * 0.49), s->q, s->gain_db, sr);
        if (s->type == ACS_DEESSER) design(s, 1, fmin(s->freq, sr * 0.45), 0.707, 0, sr);
    }
}

void audio_chain_set_measured_lufs(AudioChain *c, double lufs) { c->measured_lufs = lufs; }

static double tdf2(AudioChainStage *s, int ch, double x) {
    double y = biquad_tdf2_y(s->b0, x, s->s1[ch]);
    double ns1 = biquad_tdf2_s1(s->b1, s->a1, x, y, s->s2[ch]);
    s->s2[ch] = biquad_tdf2_s2(s->b2, s->a2, x, y);
    s->s1[ch] = ns1;
    return y;
}

void audio_chain_process(AudioChain *c, float *buf, int frames, int channels) {
    if (channels < 1) return;
    if (channels > AUDIO_CHAIN_MAX_CH) channels = AUDIO_CHAIN_MAX_CH;
    for (int i = 0; i < c->count; i++) {
        AudioChainStage *s = &c->st[i];
        if (s->bypass) continue;
        for (int f = 0; f < frames; f++) {
            float *fr = buf + (size_t)f * (size_t)channels;
            double peak = 0, sib = 0;
            switch (s->type) {
            case ACS_BIQUAD:
                for (int ch = 0; ch < channels; ch++) fr[ch] = (float)mix_dry_wet(fr[ch], tdf2(s, ch, fr[ch]), s->mix);
                break;
            case ACS_COMPRESSOR:
            case ACS_EXPANDER: {
                for (int ch = 0; ch < channels; ch++) peak = fmax(peak, fabs(fr[ch]));
                s->env = env_follow(s->env, peak, s->att, s->rel);
                double l = linear_to_db(s->env);
                double gdb = s->type == ACS_COMPRESSOR ? comp_gain_db(l, s->threshold_db, s->ratio, s->knee_db) + s->makeup_db
                                                       : expander_gain_db(l, s->threshold_db, s->ratio, s->knee_db, s->range_db);
                double g = db_to_linear(gdb), m = s->type == ACS_COMPRESSOR ? s->mix : 1.0;
                for (int ch = 0; ch < channels; ch++) fr[ch] = (float)mix_dry_wet(fr[ch], fr[ch] * g, m);
                break;
            }
            case ACS_DEESSER: {
                for (int ch = 0; ch < channels; ch++) {
                    sib = fmax(sib, fabs(tdf2(s, ch, fr[ch])));
                    peak = fmax(peak, fabs(fr[ch]));
                }
                s->env = env_follow(s->env, sib, s->att, s->rel);
                s->env2 = env_follow(s->env2, peak, s->att, s->rel);
                s->g = gain_smooth(s->g, deess_gain_db(linear_to_db(s->env), linear_to_db(s->env2), s->intensity), s->att, s->rel);
                double g = db_to_linear(s->g);
                for (int ch = 0; ch < channels; ch++) fr[ch] = (float)(fr[ch] * g);
                break;
            }
            case ACS_LIMITER: {
                for (int ch = 0; ch < channels; ch++) peak = fmax(peak, fabs(fr[ch]));
                double target = limiter_gain_db(linear_to_db(peak), s->ceiling_db);
                s->g = gain_smooth(s->g, target, 1.0, s->rel); /* instant attack: never over ceiling */
                double g = db_to_linear(fmin(s->g, target));
                for (int ch = 0; ch < channels; ch++) fr[ch] = (float)(fr[ch] * g);
                break;
            }
            case ACS_GAIN: {
                double g = db_to_linear(s->gain_db);
                for (int ch = 0; ch < channels; ch++) fr[ch] = (float)(fr[ch] * g);
                break;
            }
            case ACS_NORMALIZE:
                if (c->measured_lufs < 0) {
                    double g = db_to_linear(s->target_lufs - c->measured_lufs);
                    for (int ch = 0; ch < channels; ch++) fr[ch] = (float)(fr[ch] * g);
                }
                break;
            }
        }
    }
}

static int valid_name(const char *n) {
    if (!n || !isalnum((unsigned char)n[0])) return 0;
    size_t len = strlen(n);
    if (len > 64) return 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)n[i];
        if (!isalnum(ch) && ch != '_' && ch != '-') return 0;
    }
    return 1;
}

int audio_chain_fetch(const char *base_url, const char *name, double sample_rate, AudioChain *out) {
    if (!valid_name(name) || !base_url || strchr(base_url, '"') || strchr(base_url, '`') || strchr(base_url, '$')) return 0;
    char cmd[1024];
    snprintf(cmd, sizeof cmd, "curl -s -f --max-time 5 \"%s/%s\"", base_url, name);
    FILE *fp = popen(cmd, "r");
    if (!fp) return 0;
    size_t cap = 65536, n = 0;
    char *buf = malloc(cap + 1);
    if (!buf) { pclose(fp); return 0; }
    size_t r;
    while ((r = fread(buf + n, 1, cap - n, fp)) > 0) { n += r; if (n == cap) break; }
    int status = pclose(fp);
    buf[n] = 0;
    int ok = status == 0 && n > 0 && n < cap && audio_chain_parse_json(buf, out);
    free(buf);
    if (ok) audio_chain_prepare(out, sample_rate);
    return ok;
}
