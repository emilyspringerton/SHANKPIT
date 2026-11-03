/* LORA WIRE -- an RPG that fits through a 237-byte radio frame.
 *
 * THE CONSTRAINT. Meshtastic carries ~237 bytes of payload per frame with airtime measured in seconds.
 * The arena shooter's model (60Hz position replication over UDP) does not degrade into this; it stops
 * existing. So the model is the other one: clients send INTENTS, the host resolves them on a fixed
 * grid once per tick, and every client receives a DELTA of what changed. No positions streamed, no
 * assets streamed, no clock anywhere.
 *
 * FOUR LAWS:
 *
 *   1. EXPLICIT BYTES, NOT BITFIELDS. `unsigned x : 4` is implementation-defined, so gcc and any other
 *      compiler emit different bytes and the peers disagree. Everything here is little-endian,
 *      shift-and-or, written by hand. A decoder REJECTS what it cannot understand.
 *   2. DETERMINISTIC, ALWAYS. Combat rolls are hashes of (attacker, target, tick) -- no rand(), no
 *      clock -- so a replay grades identically and two peers can check each other.
 *   3. A TICK IS ALL-OR-NOTHING. The resolver checks capacity BEFORE touching an actor, so a refused
 *      tick leaves the world byte-identical. Half a tick is a client that silently diverges.
 *   4. A TICK MAY SPAN FRAMES, AND SAYS SO. 64 actors can produce more than 38 records, so the delta
 *      header carries (chunk, chunks) and a receiver reassembles. Without that, the honest alternative
 *      would be a world that silently refuses to grow.
 *
 * WHAT THIS IS NOT: it is not a Meshtastic client. There is no serial/Bluetooth handling, no portnum
 * registration, no radio in the loop -- the numbers say what a frame can hold, the tests prove the
 * bytes, and the transport is the next slice.
 */
#ifndef SHANKPIT_PROTOCOL_LORA_WIRE_H
#define SHANKPIT_PROTOCOL_LORA_WIRE_H

#include <stdint.h>
#include <string.h>

#include "lora_rules.h"

#define LORA_MAX_PAYLOAD        237   /* Meshtastic's practical payload ceiling, not a guess at more */
#define LORA_INTENT_BYTES       12
#define LORA_DELTA_VERSION      2u    /* v2: the header carries chunk/chunks so a tick can span frames */
#define LORA_DELTA_HEADER_BYTES 6     /* version:u8 count:u8 tick:u16 chunk:u8 chunks:u8 */
#define LORA_DELTA_RECORD_BYTES 6     /* entity:u16 kind:u8 value:u16 extra:u8 */
#define LORA_MAX_DELTA_RECORDS  ((LORA_MAX_PAYLOAD - LORA_DELTA_HEADER_BYTES) / LORA_DELTA_RECORD_BYTES)
#define LORA_GRID_MAX           64    /* a coordinate is a byte in practice; 64x64 is this first world */
#define LORA_MAX_ACTORS         64
/* Worst case per intent is three records: damage + xp + ack. 64 actors therefore need 192. */
#define LORA_MAX_RECORDS        (3 * LORA_MAX_ACTORS)
#define LORA_MAX_FRAMES         ((LORA_MAX_RECORDS + LORA_MAX_DELTA_RECORDS - 1) / LORA_MAX_DELTA_RECORDS)

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
    LORA_DELTA_ACK    = 5,   /* value = the intent's sequence number: "your action was resolved" */
    LORA_DELTA_STATE  = 6,   /* a whole actor for a snapshot: value = packed x/y, extra = hp */
    LORA_DELTA_COUNT  = 7
};

/* x and y are < 64, so both fit one uint16 with room to spare; hp rides in extra. This is what makes a
   full-state frame cost ONE record per actor instead of two. */
#define LORA_STATE_PACK(x, y) ((uint16_t)(((x) & 0x3F) | (((y) & 0x3F) << 6)))
#define LORA_STATE_X(v)       ((uint16_t)((v) & 0x3F))
#define LORA_STATE_Y(v)       ((uint16_t)(((v) >> 6) & 0x3F))

typedef struct {
    uint16_t seq;      /* out-of-order / duplicate filtering, and the key the ACK echoes back */
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
    int      count;    /* records in THIS frame */
    uint16_t tick;
    uint8_t  chunk;    /* 0-based */
    uint8_t  chunks;   /* total frames this tick spans */
} LoraDelta;

typedef struct {
    const uint8_t *buf;
    int            len;
    uint16_t       tick;
    int            count;
    uint8_t        chunk;
    uint8_t        chunks;
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

/* ---- delta frames ---------------------------------------------------------------------------- */

/* One frame, chunk `chunk` of `chunks`, holding up to 38 records. */
static void lora_delta_begin(LoraDelta *d, uint16_t tick, uint8_t chunk, uint8_t chunks) {
    if (!d) return;
    memset(d, 0, sizeof *d);
    if (chunks == 0) chunks = 1;
    if (chunk >= chunks) chunk = 0;
    d->tick = tick;
    d->chunk = chunk;
    d->chunks = chunks;
    d->len = LORA_DELTA_HEADER_BYTES;
    d->buf[0] = (uint8_t)LORA_DELTA_VERSION;
    d->buf[1] = 0;                       /* count, filled by finish */
    d->buf[2] = (uint8_t)(tick & 0xFF);
    d->buf[3] = (uint8_t)(tick >> 8);
    d->buf[4] = chunk;
    d->buf[5] = chunks;
}

/* 0 = this frame is full. The caller treats that as "split the tick" -- never as "drop a record and
   carry on", because a delta missing a record is a client that diverges silently. */
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
    if (buf[5] == 0 || buf[4] >= buf[5]) return 0;   /* chunk must be inside chunks */
    need = LORA_DELTA_HEADER_BYTES + (int)buf[1] * LORA_DELTA_RECORD_BYTES;
    if (len < need) return 0;                        /* a truncated frame is a truncated frame */
    v->buf = buf;
    v->len = need;
    v->count = buf[1];
    v->tick = (uint16_t)(buf[2] | (buf[3] << 8));
    v->chunk = buf[4];
    v->chunks = buf[5];
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

/* Turn a resolved record list into whole frames. Returns the frame count, or -1 when max_frames is
   too small -- and writes nothing in that case, so a caller can size up and retry. */
static int lora_frames_build(const LoraDeltaRecord *rec, int count, uint16_t tick,
                             LoraDelta *frames, int max_frames) {
    int need, f, i;
    if (!frames || max_frames <= 0 || count < 0) return -1;
    if (count > LORA_MAX_RECORDS) return -1;
    need = (count + LORA_MAX_DELTA_RECORDS - 1) / LORA_MAX_DELTA_RECORDS;
    if (need == 0) need = 1;                     /* an empty tick still advances every client */
    if (need > max_frames) return -1;
    for (f = 0; f < need; f++) {
        lora_delta_begin(&frames[f], tick, (uint8_t)f, (uint8_t)need);
        for (i = f * LORA_MAX_DELTA_RECORDS; i < count && i < (f + 1) * LORA_MAX_DELTA_RECORDS; i++) {
            if (!lora_delta_push(&frames[f], rec[i].entity, rec[i].kind, rec[i].value, rec[i].extra)) return -1;
        }
        if (lora_delta_finish(&frames[f]) <= 0) return -1;
    }
    return need;
}

/* A receiver that lost a chunk must NOT see a partial tick. It stays incomplete (0) until every chunk
   has arrived; a duplicate, a foreign tick or a contradictory chunk count is a hard -1. */
typedef struct {
    LoraDeltaRecord rec[LORA_MAX_RECORDS];
    int      count;
    int      have_mask;      /* bit per chunk received; LORA_MAX_FRAMES <= 32 */
    uint8_t  chunks;
    uint16_t tick;
} LoraReassembly;

static void lora_reassembly_begin(LoraReassembly *r) {
    if (!r) return;
    memset(r, 0, sizeof *r);
}

static int lora_reassembly_put(LoraReassembly *r, const uint8_t *buf, int len) {
    LoraDeltaView v;
    LoraDeltaRecord tmp;
    int i, base;
    if (!r || !lora_delta_read(buf, len, &v)) return -1;
    if (r->chunks == 0) {
        r->chunks = v.chunks;
        r->tick = v.tick;
        r->have_mask = 0;
        r->count = 0;
    } else if (v.chunks != r->chunks || v.tick != r->tick) {
        return -1;                               /* two different ticks in one reassembly */
    }
    if (v.chunk >= 32) return -1;
    if (r->have_mask & (1 << v.chunk)) return -1;/* a duplicate chunk is not "already done" */
    base = (int)v.chunk * LORA_MAX_DELTA_RECORDS;
    if (base + v.count > LORA_MAX_RECORDS) return -1;
    for (i = 0; i < v.count; i++) {
        if (!lora_delta_get(&v, i, &tmp)) return -1;
        r->rec[base + i] = tmp;
    }
    r->have_mask |= (1 << v.chunk);
    if (r->count < base + v.count) r->count = base + v.count;
    {
        int want = (1 << r->chunks) - 1;
        return (r->have_mask & want) == want ? 1 : 0;
    }
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

/* Resolve ONE tick into a record list, mutating the world as it goes.
 *   returns  the record count (0 for an empty tick) or -1 if the list cannot hold the worst case, in
 *            which case NOTHING was changed.
 * Intent order is the resolution order, and that is part of the contract. Every ACCEPTED intent emits
 * an ACK carrying its sequence number, so a client knows whether to resend. */
static int lora_resolve(LoraWorld *w, const LoraIntent *in, int n, int preset,
                        LoraDeltaRecord *out, int cap) {
    int i, written = 0;
    int hit_permille, dmg, xp_hit;
    if (!w || !in || n < 0 || !out || cap <= 0) return -1;
    if (n > 0 && n > cap / 3) return -1;         /* three records per intent is the worst case */
    hit_permille = lora_hit_permille(preset);
    dmg = lora_damage(preset);
    xp_hit = lora_xp_hit(preset);

    for (i = 0; i < n; i++) {
        LoraActor *a = lora_world_find(w, in[i].entity);
        if (!a) continue;                        /* an entity that is not here is ignored */
        if (in[i].action == LORA_ACT_MOVE) {
            if (in[i].x >= LORA_GRID_MAX || in[i].y >= LORA_GRID_MAX) continue;
            if (in[i].x == a->x && in[i].y == a->y) {
                out[written].entity = a->id;
                out[written].kind = LORA_DELTA_ACK;
                out[written].value = in[i].seq;
                out[written].extra = 0;
                written++;
                continue;
            }
            a->x = in[i].x;
            a->y = in[i].y;
            out[written].entity = a->id;
            out[written].kind = LORA_DELTA_MOVE;
            out[written].value = a->x;
            out[written].extra = (uint8_t)a->y;
            written++;
        } else if (in[i].action == LORA_ACT_ATTACK) {
            LoraActor *t = lora_world_find(w, in[i].target);
            uint16_t left;
            if (t && t->id != a->id && t->hp != 0 && lora_chebyshev(a, t) <= 1 &&
                lora_roll(a->id, t->id, w->tick) < (uint32_t)hit_permille) {
                left = (uint16_t)(t->hp > (uint16_t)dmg ? t->hp - (uint16_t)dmg : 0);
                t->hp = left;
                out[written].entity = t->id;
                out[written].kind = LORA_DELTA_DAMAGE;
                out[written].value = (uint16_t)dmg;
                out[written].extra = (uint8_t)(left > 255 ? 255 : left);
                written++;
                out[written].entity = a->id;
                out[written].kind = LORA_DELTA_XP;
                out[written].value = (uint16_t)xp_hit;
                out[written].extra = 0;
                written++;
            }
        }
        out[written].entity = a->id;
        out[written].kind = LORA_DELTA_ACK;
        out[written].value = in[i].seq;
        out[written].extra = 0;
        written++;
    }
    w->tick++;
    return written;
}

/* One frame only. Returns records written, -1 if this tick needs more than one frame (use
   lora_world_tick_frames) or if it could not be resolved at all. */
static int lora_world_tick(LoraWorld *w, const LoraIntent *in, int n, int preset, LoraDelta *out) {
    LoraDeltaRecord rec[LORA_MAX_RECORDS];
    LoraWorld tmp;
    int count, frames;
    if (!w || !out) return -1;
    /* Resolve into a COPY and commit only once the frame exists: a tick that needs two frames must not
       move the world and then report that it could not be sent. */
    tmp = *w;
    count = lora_resolve(&tmp, in, n, preset, rec, LORA_MAX_RECORDS);
    if (count < 0) return -1;
    frames = lora_frames_build(rec, count, (uint16_t)(tmp.tick - 1), out, 1);
    if (frames < 0) return -1;
    *w = tmp;
    return count;
}

/* The general form: one tick, as many frames as it needs. Returns the frame count or -1 (nothing
   written, world untouched). */
static int lora_world_tick_frames(LoraWorld *w, const LoraIntent *in, int n, int preset,
                                  LoraDelta *frames, int max_frames) {
    LoraDeltaRecord rec[LORA_MAX_RECORDS];
    LoraWorld tmp;
    int count, built;
    if (!w || !frames || max_frames <= 0) return -1;
    if (n > 0 && n > LORA_MAX_RECORDS / 3) return -1;
    tmp = *w;                                   /* all-or-nothing, same as the single-frame path */
    count = lora_resolve(&tmp, in, n, preset, rec, LORA_MAX_RECORDS);
    if (count < 0) return -1;
    built = lora_frames_build(rec, count, (uint16_t)(tmp.tick - 1), frames, max_frames);
    if (built < 0) return -1;
    *w = tmp;
    return built;
}

/* ---- a snapshot: the whole world, for a client that lost everything ---------------------------- */

/* Same framing as a delta (chunked, reassembled by the same code), but every record is STATE: "this is
   the world at tick T, replace your copy". It does NOT advance the tick, because it resolves nothing. */
static int lora_world_snapshot(const LoraWorld *w, LoraDelta *frames, int max_frames) {
    LoraDeltaRecord rec[LORA_MAX_ACTORS];
    int i;
    if (!w || !frames || max_frames <= 0) return -1;
    if (w->count > LORA_MAX_ACTORS) return -1;
    for (i = 0; i < w->count; i++) {
        rec[i].entity = w->actors[i].id;
        rec[i].kind = LORA_DELTA_STATE;
        rec[i].value = LORA_STATE_PACK(w->actors[i].x, w->actors[i].y);
        rec[i].extra = (uint8_t)(w->actors[i].hp > 255 ? 255 : w->actors[i].hp);
    }
    return lora_frames_build(rec, w->count, w->tick, frames, max_frames);
}

/* ---- the client's resend queue ---------------------------------------------------------------- */

#define LORA_CLIENT_PENDING 16

typedef struct {
    LoraIntent in;
    uint32_t   last_ms;
    uint8_t    sends;
    uint8_t    used;
} LoraPending;

typedef struct {
    LoraPending p[LORA_CLIENT_PENDING];
    int preset;
} LoraClient;

static void lora_client_begin(LoraClient *c, int preset) {
    if (!c) return;
    memset(c, 0, sizeof *c);
    c->preset = preset;
}

static int lora_client_pending(const LoraClient *c) {
    int i, n = 0;
    if (!c) return 0;
    for (i = 0; i < LORA_CLIENT_PENDING; i++) if (c->p[i].used) n++;
    return n;
}

/* Queue a NEW intent. 0 = the queue is full, which the caller must surface, not hide. */
static int lora_client_send(LoraClient *c, const LoraIntent *in, uint32_t now_ms) {
    int i;
    if (!c || !in) return 0;
    for (i = 0; i < LORA_CLIENT_PENDING; i++) {
        if (!c->p[i].used) {
            c->p[i].in = *in;
            c->p[i].sends = 1;
            c->p[i].last_ms = now_ms;
            c->p[i].used = 1;
            return 1;
        }
    }
    return 0;
}

/* An ACK nails an intent by (entity, seq): two clients' sequence spaces may overlap, and an ACK for
   somebody else must never clear our queue. */
static int lora_client_ack(LoraClient *c, uint16_t entity, uint16_t seq) {
    int i;
    if (!c) return 0;
    for (i = 0; i < LORA_CLIENT_PENDING; i++) {
        if (c->p[i].used && c->p[i].in.entity == entity && c->p[i].in.seq == seq) {
            c->p[i].used = 0;
            return 1;
        }
    }
    return 0;
}

/* What is due to be (re)sent now. The caller supplies the time: this header owns no clock.
   An intent past the budget is DROPPED, so the player learns on the next tick instead of retrying into
   a link that is not carrying it. */
static int lora_client_due(LoraClient *c, uint32_t now_ms, LoraIntent *out, int cap) {
    int i, n = 0;
    uint32_t after;
    int max_sends;
    if (!c || !out || cap <= 0) return 0;
    after = (uint32_t)lora_resend_ms(c->preset);
    max_sends = lora_max_sends(c->preset);
    if (max_sends < 1) max_sends = 1;
    for (i = 0; i < LORA_CLIENT_PENDING; i++) {
        if (!c->p[i].used) continue;
        if (c->p[i].sends >= max_sends) { c->p[i].used = 0; continue; }
        if ((uint32_t)(now_ms - c->p[i].last_ms) < after) continue;
        if (n >= cap) break;
        out[n++] = c->p[i].in;
        c->p[i].sends++;
        c->p[i].last_ms = now_ms;
    }
    return n;
}

/* ---- a client asking for the world -------------------------------------------------------------- */

/* A request is NOT a delta and must never be parsed as one: its first byte is 3 while a delta's is 2, so
   a receiver can tell them apart before anything else. Six bytes total -- a request is the cheapest thing
   on the air and should stay that way. */
#define LORA_REQUEST_VERSION 3u
#define LORA_REQUEST_BYTES   6

enum {
    LORA_REQ_SNAPSHOT = 0,
    LORA_REQ_COUNT    = 1
};

typedef struct {
    uint8_t  kind;     /* LORA_REQ_* */
    uint16_t entity;   /* who is asking, so the answer can be addressed (and logged) */
    uint16_t seq;      /* the asker's own counter: two requests are not the same request */
} LoraRequest;

static int lora_request_encode(const LoraRequest *r, uint8_t *out) {
    if (!r || !out) return 0;
    if (r->kind >= LORA_REQ_COUNT) return 0;
    out[0] = (uint8_t)LORA_REQUEST_VERSION;
    out[1] = r->kind;
    out[2] = (uint8_t)(r->entity & 0xFF);
    out[3] = (uint8_t)(r->entity >> 8);
    out[4] = (uint8_t)(r->seq & 0xFF);
    out[5] = (uint8_t)(r->seq >> 8);
    return LORA_REQUEST_BYTES;
}

static int lora_request_decode(const uint8_t *in, int len, LoraRequest *r) {
    if (!in || !r || len < LORA_REQUEST_BYTES) return 0;
    if (in[0] != LORA_REQUEST_VERSION) return 0;   /* a delta frame is not a request */
    r->kind = in[1];
    r->entity = (uint16_t)(in[2] | (in[3] << 8));
    r->seq = (uint16_t)(in[4] | (in[5] << 8));
    return r->kind < LORA_REQ_COUNT;
}

/* The host's budget. ONE pump: the interval is a host-wide budget on full-state frames, because the air
   is shared by everyone and a per-client fairness table is a different (later) decision, named as such
   in the doc rather than half-built here. */
typedef struct {
    uint32_t last_ms;
    uint8_t  served;
} LoraSnapshotPump;

static void lora_snapshot_pump_begin(LoraSnapshotPump *p) {
    if (p) memset(p, 0, sizeof *p);
}

/* Serve a client's request.
     >0  frames written (the snapshot, chunked like any tick)
      0  a snapshot was just sent: rate-limited, the asker must wait
     -1  not a request this host can serve (wrong kind, null world, no frame slots) */
static int lora_host_request(const LoraRequest *req, const LoraWorld *w, int preset, uint32_t now_ms,
                             LoraSnapshotPump *pump, LoraDelta *frames, int max_frames) {
    int built;
    if (!req || !w || !pump || !frames || max_frames <= 0) return -1;
    if (req->kind != LORA_REQ_SNAPSHOT) return -1;
    if (pump->served && (uint32_t)(now_ms - pump->last_ms) < (uint32_t)lora_snapshot_interval_ms(preset))
        return 0;
    built = lora_world_snapshot(w, frames, max_frames);
    if (built < 0) return -1;
    pump->served = 1;
    pump->last_ms = now_ms;
    return built;
}

#endif /* SHANKPIT_PROTOCOL_LORA_WIRE_H */
