/* LORA WIRE -- an RPG that fits through a 237-byte radio frame.
 *
 * THE CONSTRAINT. Meshtastic carries ~237 bytes of payload per frame with airtime measured in seconds.
 * The arena shooter's model (60Hz position replication over UDP) does not degrade into this; it stops
 * existing. So the model is the other one: clients send INTENTS, the host resolves them on a fixed
 * grid once per tick, and every client receives a DELTA of what changed. No positions streamed, no
 * assets streamed, no clock anywhere.
 *
 * THREE LAWS, same as the rest of this repo:
 *
 *   1. EXPLICIT BYTES, NOT BITFIELDS. The obvious way to write the packet in the design notes is a
 *      bitfield struct; in C the layout of `unsigned x : 4` is implementation-defined, so the same
 *      source emits different bytes on gcc and msvc and the radio peers disagree. Everything here is
 *      little-endian, shift-and-or, written by hand. A decoder rejects what it cannot understand
 *      instead of guessing.
 *   2. DETERMINISTIC, ALWAYS. Combat rolls are FNV-style hashes of (attacker, target, tick) -- no
 *      rand(), no clock -- so a replay grades identically and two peers can check each other.
 *   3. A TICK IS ALL-OR-NOTHING. If the delta would not fit in one frame, the tick is REFUSED (-1)
 *      before any world state changes, so a caller can split or defer instead of shipping half a tick
 *      that nobody can reconstruct.
 *
 * WHAT THIS IS NOT: it is not a Meshtastic client. There is no serial/Bluetooth handling, no
 * portnum registration, no radio in the loop at all -- the numbers say what a frame can hold, the
 * tests prove the bytes, and the transport is the next slice.
 */
#ifndef SHANKPIT_PROTOCOL_LORA_WIRE_H
#define SHANKPIT_PROTOCOL_LORA_WIRE_H

#include <stdint.h>
#include <string.h>

#include "lora_rules.h"

#define LORA_MAX_PAYLOAD        237   /* Meshtastic's practical payload ceiling, not a guess at more */
#define LORA_INTENT_BYTES       12
#define LORA_DELTA_VERSION      1u
#define LORA_DELTA_HEADER_BYTES 4     /* version:u8 count:u8 tick:u16 */
#define LORA_DELTA_RECORD_BYTES 6     /* entity:u16 kind:u8 value:u16 extra:u8 */
#define LORA_MAX_DELTA_RECORDS  ((LORA_MAX_PAYLOAD - LORA_DELTA_HEADER_BYTES) / LORA_DELTA_RECORD_BYTES)
#define LORA_GRID_MAX           64    /* a coordinate is a byte in practice; 64x64 is this first world */
#define LORA_MAX_ACTORS         16

enum {
    LORA_ACT_IDLE     = 0,
    LORA_ACT_MOVE     = 1,
    LORA_ACT_ATTACK   = 2,
    LORA_ACT_INTERACT = 3,
    LORA_ACT_SAY      = 4,
    LORA_ACT_COUNT    = 5
};

enum {
    LORA_DELTA_MOVE   = 1,   /* value = x, extra = y */
    LORA_DELTA_DAMAGE = 2,   /* value = damage dealt, extra = hp left (clamped to 255) */
    LORA_DELTA_XP     = 3,   /* value = xp gained */
    LORA_DELTA_ITEM   = 4,   /* value = item id, extra = quantity */
    LORA_DELTA_COUNT  = 5
};

typedef struct {
    uint16_t seq;      /* out-of-order / duplicate filtering on the client's send queue */
    uint16_t entity;
    uint16_t x;        /* target tile, always < LORA_GRID_MAX */
    uint16_t y;
    uint16_t target;   /* 0 = none */
    uint8_t  action;   /* LORA_ACT_* */
    uint8_t  arg;
} LoraIntent;

typedef struct {
    uint16_t entity;
    uint8_t  kind;     /* LORA_DELTA_* */
    uint16_t value;
    uint8_t  extra;
} LoraDeltaRecord;

typedef struct {
    uint8_t  buf[LORA_MAX_PAYLOAD];
    int      len;
    int      count;
    uint16_t tick;
} LoraDelta;

typedef struct {
    const uint8_t *buf;
    int            len;
    uint16_t       tick;
    int            count;
} LoraDeltaView;

/* ---- intents: 12 bytes, hand-packed little-endian -------------------------------------------- */

static int lora_intent_encode(const LoraIntent *in, uint8_t *out) {
    if (!in || !out) return 0;
    if (in->action >= LORA_ACT_COUNT) return 0;
    if (in->x >= LORA_GRID_MAX || in->y >= LORA_GRID_MAX) return 0;
    out[0]  = (uint8_t)(in->seq & 0xFF);
    out[1]  = (uint8_t)(in->seq >> 8);
    out[2]  = (uint8_t)(in->entity & 0xFF);
    out[3]  = (uint8_t)(in->entity >> 8);
    out[4]  = in->action;
    out[5]  = in->arg;
    out[6]  = (uint8_t)(in->x & 0xFF);
    out[7]  = (uint8_t)(in->x >> 8);
    out[8]  = (uint8_t)(in->y & 0xFF);
    out[9]  = (uint8_t)(in->y >> 8);
    out[10] = (uint8_t)(in->target & 0xFF);
    out[11] = (uint8_t)(in->target >> 8);
    return LORA_INTENT_BYTES;
}

/* Untrusted input: a short buffer, an unknown action or an out-of-grid tile is a REJECT, not a clamp. */
static int lora_intent_decode(const uint8_t *in, int len, LoraIntent *out) {
    if (!in || !out || len < LORA_INTENT_BYTES) return 0;
    out->seq    = (uint16_t)(in[0] | (in[1] << 8));
    out->entity = (uint16_t)(in[2] | (in[3] << 8));
    out->action = in[4];
    out->arg    = in[5];
    out->x      = (uint16_t)(in[6] | (in[7] << 8));
    out->y      = (uint16_t)(in[8] | (in[9] << 8));
    out->target = (uint16_t)(in[10] | (in[11] << 8));
    if (out->action >= LORA_ACT_COUNT) return 0;
    if (out->x >= LORA_GRID_MAX || out->y >= LORA_GRID_MAX) return 0;
    return 1;
}

/* ---- deltas: one frame, header + up to 38 records -------------------------------------------- */

static void lora_delta_begin(LoraDelta *d, uint16_t tick) {
    if (!d) return;
    memset(d, 0, sizeof *d);
    d->tick = tick;
    d->len = LORA_DELTA_HEADER_BYTES;
    d->buf[0] = (uint8_t)LORA_DELTA_VERSION;
    d->buf[1] = 0;                       /* count, filled by finish */
    d->buf[2] = (uint8_t)(tick & 0xFF);
    d->buf[3] = (uint8_t)(tick >> 8);
}

/* 0 = the frame is full. The caller treats that as "this tick does not fit" -- never as "drop a
   record and carry on", because a delta missing a record is a client that diverges silently. */
static int lora_delta_push(LoraDelta *d, uint16_t entity, uint8_t kind, uint16_t value, uint8_t extra) {
    int at;
    if (!d || kind == 0 || kind >= LORA_DELTA_COUNT) return 0;
    if (d->count >= LORA_MAX_DELTA_RECORDS) return 0;
    at = d->len;
    d->buf[at + 0] = (uint8_t)(entity & 0xFF);
    d->buf[at + 1] = (uint8_t)(entity >> 8);
    d->buf[at + 2] = kind;
    d->buf[at + 3] = (uint8_t)(value & 0xFF);
    d->buf[at + 4] = (uint8_t)(value >> 8);
    d->buf[at + 5] = extra;
    d->len += LORA_DELTA_RECORD_BYTES;
    d->count++;
    return 1;
}

static int lora_delta_finish(LoraDelta *d) {
    if (!d) return 0;
    d->buf[1] = (uint8_t)d->count;
    return d->len;
}

static int lora_delta_read(const uint8_t *buf, int len, LoraDeltaView *v) {
    int need;
    if (!buf || !v || len < LORA_DELTA_HEADER_BYTES) return 0;
    if (buf[0] != LORA_DELTA_VERSION) return 0;
    if (buf[1] > LORA_MAX_DELTA_RECORDS) return 0;
    need = LORA_DELTA_HEADER_BYTES + (int)buf[1] * LORA_DELTA_RECORD_BYTES;
    if (len < need) return 0;                 /* a truncated frame is a truncated frame */
    v->buf = buf;
    v->len = need;
    v->count = buf[1];
    v->tick = (uint16_t)(buf[2] | (buf[3] << 8));
    return 1;
}

static int lora_delta_get(const LoraDeltaView *v, int index, LoraDeltaRecord *out) {
    const uint8_t *p;
    if (!v || !out || !v->buf || index < 0 || index >= v->count) return 0;
    p = v->buf + LORA_DELTA_HEADER_BYTES + index * LORA_DELTA_RECORD_BYTES;
    out->entity = (uint16_t)(p[0] | (p[1] << 8));
    out->kind = p[2];
    out->value = (uint16_t)(p[3] | (p[4] << 8));
    out->extra = p[5];
    if (out->kind == 0 || out->kind >= LORA_DELTA_COUNT) return 0;
    return 1;
}

/* ---- the world: a grid of actors, resolved once per tick -------------------------------------- */

typedef struct {
    uint16_t id;
    uint16_t x, y;
    uint16_t hp;
} LoraActor;

typedef struct {
    LoraActor actors[LORA_MAX_ACTORS];
    int count;
    uint16_t tick;
} LoraWorld;

static int lora_world_add(LoraWorld *w, uint16_t id, uint16_t x, uint16_t y, uint16_t hp) {
    if (!w || w->count >= LORA_MAX_ACTORS) return 0;
    if (x >= LORA_GRID_MAX || y >= LORA_GRID_MAX) return 0;
    w->actors[w->count].id = id;
    w->actors[w->count].x = x;
    w->actors[w->count].y = y;
    w->actors[w->count].hp = hp;
    w->count++;
    return 1;
}

static LoraActor *lora_world_find(LoraWorld *w, uint16_t id) {
    int i;
    if (!w) return NULL;
    for (i = 0; i < w->count; i++)
        if (w->actors[i].id == id) return &w->actors[i];
    return NULL;
}

/* Deterministic "dice": a pure function of who, whom and which tick. Never a clock, never rand(). */
static uint32_t lora_roll(uint16_t attacker, uint16_t target, uint16_t tick) {
    uint32_t h = 2166136261u;
    h = (h ^ attacker) * 16777619u;
    h = (h ^ target) * 16777619u;
    h = (h ^ tick) * 16777619u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return h % 1000u;
}

static int lora_chebyshev(const LoraActor *a, const LoraActor *b) {
    int dx = (int)a->x - (int)b->x;
    int dy = (int)a->y - (int)b->y;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return dx > dy ? dx : dy;
}

/* Resolve ONE tick.
 *   returns  the number of delta records written, 0 for an empty tick, or
 *   -1       if the tick would not fit in a single frame. In that case NOTHING was changed: the caller
 *            splits the intent list or defers to the next tick.
 * Intent order is the resolution order, and that is part of the contract. */
static int lora_world_tick(LoraWorld *w, const LoraIntent *in, int n, int preset, LoraDelta *out) {
    int i, written = 0;
    int hit_permille, dmg, xp_hit;
    if (!w || !in || n < 0 || !out) return -1;

    /* ALL-OR-NOTHING, decided UP FRONT. The worst case for any intent is two records (damage + xp),
       so if even that cannot fit, refuse before touching a single actor. A tick that half-applied
       would leave every client with a different world and no way to know. */
    if (n * 2 > LORA_MAX_DELTA_RECORDS) return -1;

    hit_permille = lora_hit_permille(preset);
    dmg = lora_damage(preset);
    xp_hit = lora_xp_hit(preset);
    lora_delta_begin(out, w->tick);

    for (i = 0; i < n; i++) {
        LoraActor *a = lora_world_find(w, in[i].entity);
        if (!a) continue;                                  /* an entity that is not here is ignored */
        if (in[i].action == LORA_ACT_MOVE) {
            if (in[i].x >= LORA_GRID_MAX || in[i].y >= LORA_GRID_MAX) continue;
            if (in[i].x == a->x && in[i].y == a->y) continue;
            a->x = in[i].x;
            a->y = in[i].y;
            if (!lora_delta_push(out, a->id, LORA_DELTA_MOVE, a->x, (uint8_t)a->y)) return -1;
            written++;
        } else if (in[i].action == LORA_ACT_ATTACK) {
            LoraActor *t = lora_world_find(w, in[i].target);
            uint16_t left;
            if (!t || t->id == a->id || t->hp == 0) continue;
            if (lora_chebyshev(a, t) > 1) continue;        /* melee is adjacent, like the old games */
            if (lora_roll(a->id, t->id, w->tick) >= (uint32_t)hit_permille) continue;
            left = (uint16_t)(t->hp > (uint16_t)dmg ? t->hp - (uint16_t)dmg : 0);
            t->hp = left;
            if (!lora_delta_push(out, t->id, LORA_DELTA_DAMAGE, (uint16_t)dmg, (uint8_t)(left > 255 ? 255 : left))) return -1;
            if (!lora_delta_push(out, a->id, LORA_DELTA_XP, (uint16_t)xp_hit, 0)) return -1;
            written += 2;
        }
    }
    lora_delta_finish(out);
    w->tick++;
    return written;
}

#endif /* SHANKPIT_PROTOCOL_LORA_WIRE_H */
