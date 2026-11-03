#ifndef SHANKPIT_WOW_CLIENT_H
#define SHANKPIT_WOW_CLIENT_H

/* wow_client.h -- the CLIENT half of the macro/mod wire: what a client sends, and when it is worth
 * sending at all. The bandwidth rules live here rather than in each call site:
 *
 *   - a target intent is produced ONLY when this client's own view of a slot changes, so holding a
 *     target costs nothing on a bad link (the caller sends what it is given, and is given 0 otherwise);
 *   - the cast sequence only moves forward and never uses 0, and the receiver drops anything not
 *     strictly newer, so a duplicated or reordered datagram cannot shadow a real cast;
 *   - AUTH is the IDUNA ticket verbatim, once per connection: this layer does not mint or retry it.
 *
 * Nothing here opens a socket. Every function writes ONE frame into the caller's buffer, and that
 * buffer is the body of a PACKET_WOW_WIRE envelope. */

#include <stdint.h>
#include <string.h>
#include "wow_wire.h"

/* One frame waiting for its ACK. The bytes are kept verbatim, not rebuilt, so a resend is the SAME
   frame -- the receiver's "strictly newer" rule would reject a rebuilt one with a fresh sequence, which
   is exactly the bug retransmission is supposed to avoid. 38 bytes covers the largest frame (AUTH). */
#define WOW_CLIENT_PENDING 4
typedef struct {
    uint8_t  frame[WOW_WIRE_AUTH_BYTES];
    uint8_t  len;
    uint8_t  kind;     /* WOW_OP_* */
    uint8_t  used;
    uint8_t  tries;    /* resends already made */
    uint16_t seq;
    uint32_t due_ms;   /* when this becomes due for a resend */
} WowPending;

typedef struct {
    WowTargets targets;   /* mirrors what this client believes the server holds (change detection) */
    uint16_t   cast_seq;  /* last cast sequence sent; 0 means "none yet" */
    WowPending pending[WOW_CLIENT_PENDING];
    int        preset;    /* 0 LAN / 1 slow / 2 fast -- selects PARENA's ack period and retry cap */
    unsigned   tracked, acked, retried, abandoned;
} WowClient;

static inline void wow_client_begin(WowClient *c) {
    if (!c) return;
    memset(c, 0, sizeof *c);
    wow_targets_begin(&c->targets);
    c->preset = 0;
}

/* Switching transport preset changes only the retry policy, never what is on the wire. */
static inline void wow_client_set_preset(WowClient *c, int preset) {
    if (!c) return;
    c->preset = (preset < 0 || preset > 2) ? 0 : preset;
}

/* Which sequence a frame carries. AUTH and DEFINE_MACRO are session state rather than sequenced
   intents, so they are tracked under sequence 0 and matched against the server's own ACK(seq=0). */
static inline uint16_t wow_frame_seq(const uint8_t *frame, int len) {
    if (!frame || len < 2) return 0;
    switch (frame[1]) {
        case WOW_OP_SET_TARGET:
        case WOW_OP_CLEAR_TARGET: return len >= 4 ? (uint16_t)(frame[2] | (frame[3] << 8)) : 0;
        case WOW_OP_INVOKE_MACRO: return len >= 5 ? (uint16_t)(frame[3] | (frame[4] << 8)) : 0;
        case WOW_OP_CAST:         return len >= 6 ? (uint16_t)(frame[4] | (frame[5] << 8)) : 0;
        default:                  return 0;
    }
}

/* Remember a frame the caller has just sent, so it can be resent if the ACK never comes.
   1 tracked, 0 not an ackable frame (ACK itself, or unknown op), -1 bad argument / table full.
   The caller keeps sending; this only decides what happens if the ACK does not arrive. */
static inline int wow_client_track(WowClient *c, const uint8_t *frame, int len, uint32_t now_ms) {
    int i, slot = -1;
    if (!c || !frame || len < 2 || len > WOW_WIRE_AUTH_BYTES) return -1;
    if (frame[0] != WOW_WIRE_VERSION) return -1;
    if (frame[1] == WOW_OP_ACK) return 0;
    if (frame[1] < WOW_OP_SET_TARGET || frame[1] > WOW_OP_AUTH) return -1;
    for (i = 0; i < WOW_CLIENT_PENDING; i++) {
        if (!c->pending[i].used) { slot = i; break; }
    }
    if (slot < 0) return -1;              /* bounded on purpose: 4 is auth + 2 targets + a cast */
    c->pending[slot].len = (uint8_t)len;
    c->pending[slot].kind = frame[1];
    c->pending[slot].seq = wow_frame_seq(frame, len);
    c->pending[slot].used = 1;
    c->pending[slot].tries = 0;
    c->pending[slot].due_ms = now_ms + (uint32_t)wow_ack_ms(c->preset);
    memcpy(c->pending[slot].frame, frame, (size_t)len);
    c->tracked++;
    return 1;
}

/* Retire the frame an ACK names. 1 retired, 0 nothing matched (an ACK for a frame we dropped, or a
   duplicate ACK), -1 malformed. Matching on kind+seq means a late ACK cannot retire a NEWER frame
   that reused nothing but the slot. */
static inline int wow_client_ack(WowClient *c, const uint8_t *frame, int len) {
    WowAck a;
    int i;
    if (!c || wow_ack_decode(frame, len, &a) < 0) return -1;
    for (i = 0; i < WOW_CLIENT_PENDING; i++) {
        if (c->pending[i].used && c->pending[i].kind == a.kind && c->pending[i].seq == a.seq) {
            c->pending[i].used = 0;
            c->acked++;
            return 1;
        }
    }
    return 0;
}

/* The next frame that is due for a resend, copied into `out` unchanged.
   >0 = bytes to resend, 0 = nothing due, -2 = this frame exhausted its retries and was abandoned,
   -1 = bad argument. The resend period and cap are PARENA's (wow_ack_ms / wow_retry_max), so a slow
   link waits longer instead of flooding itself. */
static inline int wow_client_retry(WowClient *c, uint32_t now_ms, uint8_t *out, int cap) {
    int i;
    if (!c || !out) return -1;
    for (i = 0; i < WOW_CLIENT_PENDING; i++) {
        WowPending *p = &c->pending[i];
        if (!p->used) continue;
        if ((int32_t)(now_ms - p->due_ms) < 0) continue;
        if (p->tries >= wow_retry_max(c->preset)) {
            p->used = 0;
            c->abandoned++;
            return -2;
        }
        if (cap < (int)p->len) return -1;
        memcpy(out, p->frame, p->len);
        p->tries++;
        p->due_ms = now_ms + (uint32_t)wow_ack_ms(c->preset);
        c->retried++;
        return (int)p->len;
    }
    return 0;
}

static inline int wow_client_pending(const WowClient *c) {
    int i, n = 0;
    if (!c) return 0;
    for (i = 0; i < WOW_CLIENT_PENDING; i++) if (c->pending[i].used) n++;
    return n;
}

/* 7 bytes when the slot changed, 0 when it did not (send NOTHING), -1 on a bad argument.
 * `entity` < 0 clears the slot, which is how /cleartarget and /clearfocus reach the wire. */
static inline int wow_client_target(WowClient *c, int slot, int entity, uint8_t *out, int cap) {
    if (!c || !out) return -1;
    return wow_targets_intent(&c->targets, slot, entity, out, cap);
}

/* 6 bytes, always: a cast is an event, not a state, so it is never suppressed as a duplicate.
 * `weapon` must be a WPN_* id and `token` a WOW_TOKEN_*; the server re-checks the weapon, and this
 * layer deliberately does not own a weapon table. */
static inline int wow_client_cast(WowClient *c, int weapon, int token, uint8_t *out, int cap) {
    WowCastIntent ci;
    if (!c || !out || cap < WOW_WIRE_CAST_BYTES) return -1;
    if (weapon < 0 || weapon > 255 || token < 0 || token > 255) return -1;
    c->cast_seq = (uint16_t)wow_seq_next((int)c->cast_seq);   /* PARENA: skips 0, wraps at 16 bits */
    ci.weapon = (uint8_t)weapon;
    ci.token = (uint8_t)token;
    ci.seq = c->cast_seq;
    return wow_cast_encode(&ci, out, cap);
}

/* 38 bytes: the 36-byte IDUNA ticket verbatim (see wow_ticket.h), not its hex text. */
static inline int wow_client_auth(const uint8_t ticket[WOW_WIRE_TICKET_BYTES], uint8_t *out, int cap) {
    WowAuthFrame af;
    if (!ticket || !out) return -1;
    memcpy(af.ticket, ticket, WOW_WIRE_TICKET_BYTES);
    return wow_auth_encode(&af, out, cap);
}

#endif /* SHANKPIT_WOW_CLIENT_H */
