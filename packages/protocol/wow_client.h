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

typedef struct {
    WowTargets targets;   /* mirrors what this client believes the server holds (change detection) */
    uint16_t   cast_seq;  /* last cast sequence sent; 0 means "none yet" */
} WowClient;

static inline void wow_client_begin(WowClient *c) {
    if (!c) return;
    memset(c, 0, sizeof *c);
    wow_targets_begin(&c->targets);
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
