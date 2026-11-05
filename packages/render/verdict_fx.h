#ifndef SHANKPIT_VERDICT_FX_H
#define SHANKPIT_VERDICT_FX_H

/* verdict_fx.h -- the civic boss's verdict embers and sparks as STATE, not as drawing.
 *
 * Founder: "add full spacial sound and visual affordances with particle effects and shaders", then
 * "add verdict scortch detail; test particle pool". The pool half of that is only possible if the
 * particles are separated from the renderer: apps/lobby used to own the motes as file static state,
 * which meant the only way to know whether an ember expired, whether the ring wrapped instead of
 * growing, or whether an ember's arc was right was to look at a screen -- and this build has no display
 * to look at. So everything that DECIDES lives here, GL-free and headless (make test-verdict-fx), and
 * main.c keeps only the program, the VBO and the draw calls.
 *
 * The floor scorch is NOT here: it is a hazard as well as a mark, so its pool lives in
 * packages/simulation/verdict_scorch.h, owned and ticked by story_ai.c, and this renderer only reads it.
 * One pool means the fire on screen and the fire that burns you cannot be two different fires.
 *
 * Motes are a bounded ring buffer (a burst cycles slots instead of growing anything): 18 embers per
 * sentence read, 30 sparks per sentence served, VERDICT_MOTE_MAX slots total.
 *
 * Timing is wall-clock, not accumulated per frame: sampling a mote at now_ms is a pure function of
 * (spawn state, now_ms), so a dropped frame skips a frame of motion instead of permanently lagging the
 * effect -- and the same sample is reproducible in a test with no GL context at all.
 *
 * Positions use the same simple ballistic model the grenade sparks use (constant velocity plus a
 * gravity term), which is what makes an ember rise and fall back while a spark's outward speed carries
 * it away.
 */

#include <math.h>
#include <string.h>

#define VERDICT_RING_RADIUS 26.0f  /* the shader ring's world radius; embers spawn inside it */
#define VERDICT_MOTE_MAX    96
#define VERDICT_EMBER_COUNT 18
#define VERDICT_SPARK_COUNT 30
#define VERDICT_GRAVITY     4.5f   /* world units per second squared, downward */

typedef struct {
    int active;
    int spark;            /* 0 = ember lifting off the ring, 1 = spark thrown by the serve */
    float x, y, z, vx, vy, vz;
    unsigned int t0;
    float life;           /* seconds */
    float size;           /* world-unit half-size at birth */
} VerdictMote;

typedef struct {
    VerdictMote mote[VERDICT_MOTE_MAX];
    unsigned int mote_next;
    unsigned int rng;
} VerdictFx;

/* Deterministic xorshift: the same seed gives the same burst, so a test can assert on it and a replay
   of the same input does not produce a different-looking fight. */
static inline void verdict_fx_reset(VerdictFx *fx) {
    memset(fx, 0, sizeof(*fx));
    fx->rng = 0x51ED270Fu;
}

static inline float verdict_fx_rand(VerdictFx *fx) {
    fx->rng ^= fx->rng << 13;
    fx->rng ^= fx->rng >> 17;
    fx->rng ^= fx->rng << 5;
    return (float)(fx->rng & 0xFFFFFF) / 16777216.0f;
}

static inline int verdict_fx_live_motes(const VerdictFx *fx) {
    int n = 0;
    for (int i = 0; i < VERDICT_MOTE_MAX; i++) if (fx->mote[i].active) n++;
    return n;
}

static inline VerdictMote *verdict_fx_mote_push(VerdictFx *fx, float x, float y, float z,
                                                float vx, float vy, float vz,
                                                float life, float size, int spark,
                                                unsigned int now_ms) {
    VerdictMote *m = &fx->mote[fx->mote_next % VERDICT_MOTE_MAX];
    fx->mote_next = (fx->mote_next + 1u) % VERDICT_MOTE_MAX;
    m->active = 1;
    m->spark = spark;
    m->x = x; m->y = y; m->z = z;
    m->vx = vx; m->vy = vy; m->vz = vz;
    m->t0 = now_ms;
    m->life = life > 0.01f ? life : 0.01f;
    m->size = size;
    return m;
}

/* A sentence being read: embers lift off the same radius the shader's ring is closing on, so the two
   layers read as one object rather than two effects happening at once. */
static inline void verdict_fx_ring_embers(VerdictFx *fx, float x, float y, float z, unsigned int now_ms) {
    for (int i = 0; i < VERDICT_EMBER_COUNT; i++) {
        float a = (float)i * 0.3490659f;                  /* 18 / tau: evenly around the ring */
        float r = 5.0f + 15.0f * verdict_fx_rand(fx);
        verdict_fx_mote_push(fx,
                             x + cosf(a) * r, y + 0.35f, z + sinf(a) * r,
                             cosf(a) * 0.7f, 1.4f + 2.8f * verdict_fx_rand(fx), sinf(a) * 0.7f,
                             0.55f + 0.35f * verdict_fx_rand(fx),
                             0.34f + 0.34f * verdict_fx_rand(fx),
                             0, now_ms);
    }
}

/* The sentence served: everything the ring was holding goes outward at once. */
static inline void verdict_fx_serve_burst(VerdictFx *fx, float x, float y, float z, unsigned int now_ms) {
    for (int i = 0; i < VERDICT_SPARK_COUNT; i++) {
        float a = (float)i * 2.399963f;                   /* golden angle: even spray, no clustering */
        float speed = 7.0f + 13.0f * verdict_fx_rand(fx);
        verdict_fx_mote_push(fx,
                             x + cosf(a) * 1.5f, y + 1.0f, z + sinf(a) * 1.5f,
                             cosf(a) * speed, 2.0f + 5.0f * verdict_fx_rand(fx), sinf(a) * speed,
                             0.45f + 0.30f * verdict_fx_rand(fx),
                             0.30f + 0.28f * verdict_fx_rand(fx),
                             1, now_ms);
    }
}

/* Sampling one mote. Returns 0 once it is past its life (the caller may then let the pool age it
   out), 1 otherwise, and writes the world position, the 0..1 age fraction and the current size. */
static inline int verdict_mote_sample(const VerdictMote *m, unsigned int now_ms,
                                      float *out_x, float *out_y, float *out_z,
                                      float *out_age, float *out_size) {
    if (!m || !m->active) return 0;
    unsigned int dt = (now_ms > m->t0) ? (now_ms - m->t0) : 0u;
    float t = (float)dt * 0.001f;
    float age = t / m->life;
    if (age >= 1.0f) return 0;
    if (out_x) *out_x = m->x + m->vx * t;
    if (out_y) *out_y = m->y + m->vy * t - VERDICT_GRAVITY * t * t;
    if (out_z) *out_z = m->z + m->vz * t;
    if (out_age) *out_age = age;
    if (out_size) *out_size = m->size * (1.0f - 0.40f * age);
    return 1;
}

/* Ages the mote pool out, so a level that is left alone does not keep ninety-six dead embers around
   forever. (The scorches are aged by the simulation that owns them.) */
static inline void verdict_fx_age(VerdictFx *fx, unsigned int now_ms) {
    for (int i = 0; i < VERDICT_MOTE_MAX; i++) {
        VerdictMote *m = &fx->mote[i];
        if (!m->active) continue;
        unsigned int dt = (now_ms > m->t0) ? (now_ms - m->t0) : 0u;
        if ((float)dt * 0.001f >= m->life) m->active = 0;
    }
}

#endif /* SHANKPIT_VERDICT_FX_H */
