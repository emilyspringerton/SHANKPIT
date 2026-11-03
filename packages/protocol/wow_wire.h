/* WOW WIRE -- the macro/mod surface on the air.
 *
 * The problem this solves: a text macro is ~40 bytes, a /cast is a string, and a target change is a
 * sentence. On a LAN that is free; on a 1k2 radio it is the whole budget. So the client compiles the
 * macro ONCE (packages/ui/wow_macro.h) and ships the CLAUSES as bytes, then invokes by id:
 *
 *     define: 5 + sum(6 + len(arg)) bytes, once per macro (28 bytes for the two-clause example below)
 *     invoke: 5 bytes per activation, no strings at all
 *     cast:   6 bytes, weapon id + unit token -- never a spell name
 *     target: 7 bytes, and ONLY when the value actually changes
 *
 * The target table is the "server can have multiple targets" part: four slots per client (target,
 * focus, and two a mod can name), each with its own entity and its own presence. Intents carry a
 * per-client sequence, and the receiver drops anything not strictly newer -- a duplicated or
 * reordered datagram must not resurrect a stale target.
 *
 * Paper, not magic: every number (slots, cast queue, clause cap, ack period, resend period) is in
 * PARENA/stdlib/shankpit/wow_wire.prn and generated into wow_wire_rules.c. The byte layouts here are
 * fixed and asserted by wow_wire_test.c, including that a macro survives compile -> bytes -> compile
 * unchanged.
 */
#ifndef SHANKPIT_PROTOCOL_WOW_WIRE_H
#define SHANKPIT_PROTOCOL_WOW_WIRE_H

#include <stdint.h>
#include <string.h>
#include "../ui/wow_macro.h"
#include "wow_wire_rules.h"

#define WOW_WIRE_VERSION      4u
#define WOW_TARGET_SLOTS      4
#define WOW_WIRE_MAX_CLAUSES  4

#define WOW_WIRE_INTENT_BYTES  7
#define WOW_WIRE_CAST_BYTES    6
#define WOW_WIRE_INVOKE_BYTES  5
#define WOW_WIRE_ACK_BYTES     5
#define WOW_WIRE_DEF_HEADER    5
#define WOW_WIRE_CLAUSE_HEADER 6
/* AUTH is deliberately 48 bytes: an IDUNA PLAYER TICKET, never the 4 KB SSO JWT. The ticket is the
   thing a game server is allowed to see; the token stays in the client. */
#define WOW_WIRE_AUTH_BYTES    48
#define WOW_WIRE_TICKET_BYTES  46

enum {
    WOW_OP_SET_TARGET   = 1,
    WOW_OP_CLEAR_TARGET = 2,
    WOW_OP_CAST         = 3,
    WOW_OP_DEFINE_MACRO = 4,
    WOW_OP_INVOKE_MACRO = 5,
    WOW_OP_ACK          = 6,
    WOW_OP_AUTH         = 7
};

/* ---- multiple targets, per client ------------------------------------------------------------- */

typedef struct {
    uint16_t entity[WOW_TARGET_SLOTS];
    uint8_t  used[WOW_TARGET_SLOTS];
    uint16_t seq;        /* last intent this side sent (client) or applied (server) */
    uint8_t  have_seq;
} WowTargets;

static inline void wow_targets_begin(WowTargets *t) { if (t) memset(t, 0, sizeof *t); }

static inline int wow_targets_get(const WowTargets *t, int slot) {
    if (!t || slot < 0 || slot >= WOW_TARGET_SLOTS || !t->used[slot]) return -1;
    return (int)t->entity[slot];
}

/* 1 changed, 0 already that value, -1 bad slot / bad entity. Entity 0 is not a unit. */
static inline int wow_targets_set(WowTargets *t, int slot, int entity) {
    if (!t || slot < 0 || slot >= WOW_TARGET_SLOTS) return -1;
    if (!wow_entity_valid(entity)) return -1;
    if (t->used[slot] && t->entity[slot] == (uint16_t)entity) return 0;
    t->entity[slot] = (uint16_t)entity;
    t->used[slot] = 1;
    return 1;
}

static inline int wow_targets_clear(WowTargets *t, int slot) {
    if (!t || slot < 0 || slot >= WOW_TARGET_SLOTS) return -1;
    if (!t->used[slot]) return 0;
    t->used[slot] = 0;
    t->entity[slot] = 0;
    return 1;
}

/* Client side. Returns WOW_WIRE_INTENT_BYTES when there is something to send, 0 when the target did not
   change (send NOTHING -- this is where the bandwidth is saved), -1 on a malformed call. */
static inline int wow_targets_intent(WowTargets *t, int slot, int entity, uint8_t *out, int cap) {
    int changed;
    if (!t || !out || cap < WOW_WIRE_INTENT_BYTES) return -1;
    if (slot < 0 || slot >= WOW_TARGET_SLOTS) return -1;
    changed = (entity < 0) ? wow_targets_clear(t, slot) : wow_targets_set(t, slot, entity);
    if (changed < 0) return -1;
    if (changed == 0) return 0;
    t->seq = (uint16_t)wow_seq_next((int)t->seq);   /* PARENA: skip 0, wrap at 16 bits */
    t->have_seq = 1;
    out[0] = (uint8_t)WOW_WIRE_VERSION;
    out[1] = (uint8_t)(entity < 0 ? WOW_OP_CLEAR_TARGET : WOW_OP_SET_TARGET);
    out[2] = (uint8_t)(t->seq & 0xFF);
    out[3] = (uint8_t)(t->seq >> 8);
    out[4] = (uint8_t)slot;
    out[5] = (uint8_t)wow_entity_lo(entity < 0 ? 0 : entity);
    out[6] = (uint8_t)wow_entity_hi(entity < 0 ? 0 : entity);
    return WOW_WIRE_INTENT_BYTES;
}

/* Server side. 2 applied, 0 stale/duplicate (dropped on purpose), -1 malformed. */
static inline int wow_targets_apply(WowTargets *t, const uint8_t *in, int len) {
    int slot;
    uint16_t seq;
    int entity;
    if (!t || !in || len != WOW_WIRE_INTENT_BYTES) return -1;
    if (in[0] != WOW_WIRE_VERSION) return -1;
    if (in[1] != WOW_OP_SET_TARGET && in[1] != WOW_OP_CLEAR_TARGET) return -1;
    seq = (uint16_t)(in[2] | (in[3] << 8));   /* byte order is the frame's, PARENA checks the sequence */
    slot = in[4];
    if (slot >= WOW_TARGET_SLOTS || seq == 0) return -1;
    if (t->have_seq && (int16_t)(seq - t->seq) <= 0) return 0;   /* wrap-safe: strictly newer or drop */
    entity = in[5] | (in[6] << 8);
    if (in[1] == WOW_OP_CLEAR_TARGET) {
        t->used[slot] = 0;
        t->entity[slot] = 0;
    } else {
        if (entity == 0) return -1;
        t->used[slot] = 1;
        t->entity[slot] = (uint16_t)entity;
    }
    t->seq = seq;
    t->have_seq = 1;
    return 2;
}

/* ---- cast: a weapon id and a token, never a name ---------------------------------------------- */

typedef struct {
    uint8_t  weapon;   /* WPN_* id, the engine's own number */
    uint8_t  token;    /* WOW_TOKEN_*, so the server resolves the target, not the client's string */
    uint16_t seq;
} WowCastIntent;

static inline int wow_cast_encode(const WowCastIntent *c, uint8_t *out, int cap) {
    if (!c || !out || cap < WOW_WIRE_CAST_BYTES) return -1;
    out[0] = (uint8_t)WOW_WIRE_VERSION;
    out[1] = (uint8_t)WOW_OP_CAST;
    out[2] = c->weapon;
    out[3] = c->token;
    out[4] = (uint8_t)(c->seq & 0xFF);
    out[5] = (uint8_t)(c->seq >> 8);
    return WOW_WIRE_CAST_BYTES;
}

static inline int wow_cast_decode(const uint8_t *in, int len, WowCastIntent *c) {
    if (!in || !c || len != WOW_WIRE_CAST_BYTES) return -1;
    if (in[0] != WOW_WIRE_VERSION || in[1] != WOW_OP_CAST) return -1;
    c->weapon = in[2];
    c->token = in[3];
    c->seq = (uint16_t)(in[4] | (in[5] << 8));
    return WOW_WIRE_CAST_BYTES;
}

/* ---- macro as bytecode: upload once, invoke by id --------------------------------------------- */

typedef struct {
    unsigned char id;
    WowMacroLine  line;
} WowMacroDef;

/* Returns bytes written, or -1 when the macro cannot be represented (bad clause count, no room).
   Layout: [ver, op, id, cmd, clause_count] then per clause
           [require lo, require hi, forbid lo, forbid hi, token, arg_len, arg...]. */
static inline int wow_macro_encode_def(unsigned char id, const WowMacroLine *m, uint8_t *out, int cap) {
    int i, used, argbytes = 0;
    if (!m || !out || id == 0) return -1;
    if (m->clause_count < 1 || m->clause_count > WOW_WIRE_MAX_CLAUSES) return -1;
    if (m->cmd == 0) return -1;
    for (i = 0; i < m->clause_count; i++)
        argbytes += (int)strlen(m->clause[i].arg);
    used = wow_def_bytes(m->clause_count, argbytes);   /* PARENA owns the size formula */
    if (used > cap) return -1;
    out[0] = (uint8_t)WOW_WIRE_VERSION;
    out[1] = (uint8_t)WOW_OP_DEFINE_MACRO;
    out[2] = id;
    out[3] = m->cmd;
    out[4] = (uint8_t)m->clause_count;
    used = WOW_WIRE_DEF_HEADER;
    for (i = 0; i < m->clause_count; i++) {
        const WowClause *c = &m->clause[i];
        size_t alen = strlen(c->arg);
        out[used + 0] = (uint8_t)(c->require_bits & 0xFF);
        out[used + 1] = (uint8_t)(c->require_bits >> 8);
        out[used + 2] = (uint8_t)(c->forbid_bits & 0xFF);
        out[used + 3] = (uint8_t)(c->forbid_bits >> 8);
        out[used + 4] = c->token;
        out[used + 5] = (uint8_t)alen;
        if (alen) memcpy(out + used + WOW_WIRE_CLAUSE_HEADER, c->arg, alen);
        used += WOW_WIRE_CLAUSE_HEADER + (int)alen;
    }
    return used;
}

/* Returns bytes consumed (== len, trailing bytes are a REFUSAL, not something to ignore). */
static inline int wow_macro_decode_def(const uint8_t *in, int len, WowMacroDef *out) {
    int i, used = WOW_WIRE_DEF_HEADER, count, cmd;
    if (!in || !out || len < WOW_WIRE_DEF_HEADER) return -1;
    if (in[0] != WOW_WIRE_VERSION || in[1] != WOW_OP_DEFINE_MACRO) return -1;
    if (in[2] == 0) return -1;
    cmd = in[3];
    count = in[4];
    if (cmd <= 0 || cmd > 255) return -1;
    if (count < 1 || count > WOW_WIRE_MAX_CLAUSES || count > WOW_MACRO_CLAUSE_MAX) return -1;
    memset(out, 0, sizeof *out);
    out->id = in[2];
    out->line.cmd = (unsigned char)cmd;
    out->line.clause_count = count;
    for (i = 0; i < count; i++) {
        int alen;
        if (used + WOW_WIRE_CLAUSE_HEADER > len) return -1;
        alen = in[used + 5];
        if (alen >= WOW_MACRO_ARG_MAX) return -1;
        if (used + WOW_WIRE_CLAUSE_HEADER + alen > len) return -1;
        out->line.clause[i].require_bits = (uint32_t)(in[used + 0] | (in[used + 1] << 8));
        out->line.clause[i].forbid_bits = (uint32_t)(in[used + 2] | (in[used + 3] << 8));
        out->line.clause[i].token = in[used + 4];
        if (alen) memcpy(out->line.clause[i].arg, in + used + WOW_WIRE_CLAUSE_HEADER, (size_t)alen);
        out->line.clause[i].arg[alen] = '\0';
        used += WOW_WIRE_CLAUSE_HEADER + alen;
    }
    if (used != len) return -1;
    return used;
}

/* ---- invoke / ack ----------------------------------------------------------------------------- */

typedef struct { unsigned char macro_id; uint16_t seq; } WowInvoke;
typedef struct { uint16_t seq; uint8_t kind; } WowAck;   /* kind = the WOW_OP_* being acknowledged */

static inline int wow_invoke_encode(const WowInvoke *v, uint8_t *out, int cap) {
    if (!v || !out || cap < WOW_WIRE_INVOKE_BYTES || v->macro_id == 0) return -1;
    out[0] = (uint8_t)WOW_WIRE_VERSION;
    out[1] = (uint8_t)WOW_OP_INVOKE_MACRO;
    out[2] = v->macro_id;
    out[3] = (uint8_t)(v->seq & 0xFF);
    out[4] = (uint8_t)(v->seq >> 8);
    return WOW_WIRE_INVOKE_BYTES;
}

static inline int wow_invoke_decode(const uint8_t *in, int len, WowInvoke *v) {
    if (!in || !v || len != WOW_WIRE_INVOKE_BYTES) return -1;
    if (in[0] != WOW_WIRE_VERSION || in[1] != WOW_OP_INVOKE_MACRO || in[2] == 0) return -1;
    v->macro_id = in[2];
    v->seq = (uint16_t)(in[3] | (in[4] << 8));
    return WOW_WIRE_INVOKE_BYTES;
}

static inline int wow_ack_encode(const WowAck *a, uint8_t *out, int cap) {
    if (!a || !out || cap < WOW_WIRE_ACK_BYTES) return -1;
    out[0] = (uint8_t)WOW_WIRE_VERSION;
    out[1] = (uint8_t)WOW_OP_ACK;
    out[2] = (uint8_t)(a->seq & 0xFF);
    out[3] = (uint8_t)(a->seq >> 8);
    out[4] = a->kind;
    return WOW_WIRE_ACK_BYTES;
}

static inline int wow_ack_decode(const uint8_t *in, int len, WowAck *a) {
    if (!in || !a || len != WOW_WIRE_ACK_BYTES) return -1;
    if (in[0] != WOW_WIRE_VERSION || in[1] != WOW_OP_ACK) return -1;
    a->seq = (uint16_t)(in[2] | (in[3] << 8));
    a->kind = in[4];
    return WOW_WIRE_ACK_BYTES;
}

/* ---- auth: an IDUNA ticket, not a token ------------------------------------------------------- */

typedef struct { char ticket[WOW_WIRE_TICKET_BYTES + 1]; } WowAuthFrame;

static inline int wow_auth_encode(const WowAuthFrame *a, uint8_t *out, int cap) {
    int i;
    if (!a || !out || cap < WOW_WIRE_AUTH_BYTES) return -1;
    out[0] = (uint8_t)WOW_WIRE_VERSION;
    out[1] = (uint8_t)WOW_OP_AUTH;
    for (i = 0; i < WOW_WIRE_TICKET_BYTES; i++) out[2 + i] = (uint8_t)a->ticket[i];
    return WOW_WIRE_AUTH_BYTES;
}

/* The ticket is copied out NUL-terminated so the verifier gets a C string and a length. */
static inline int wow_auth_decode(const uint8_t *in, int len, WowAuthFrame *a) {
    int i, n = 0;
    if (!in || !a || len != WOW_WIRE_AUTH_BYTES) return -1;
    if (in[0] != WOW_WIRE_VERSION || in[1] != WOW_OP_AUTH) return -1;
    memset(a, 0, sizeof *a);
    for (i = 0; i < WOW_WIRE_TICKET_BYTES; i++) {
        a->ticket[i] = (char)in[2 + i];
        if (a->ticket[i]) n = i + 1;
    }
    a->ticket[n] = '\0';
    if (n == 0) return -1;
    return WOW_WIRE_AUTH_BYTES;
}

#endif /* SHANKPIT_PROTOCOL_WOW_WIRE_H */
