/* WOW SERVER -- the server end of the macro/mod wire, as a headless state machine.
 *
 * This is the piece that makes "the server can have multiple targets" true: every connected client has
 * its own target table, its own macro definitions (by id) and its own bounded cast queue. The UDP
 * handler in apps/server/src/main.c only has to hand a validated frame to wow_server_rx() and send the
 * ACK it produces; all the decisions live here, where they can be tested without a socket.
 *
 * IDENTITIES ARE IDUNA'S. The AUTH frame carries a PLAYER TICKET (48 bytes), never the SSO JWT: the
 * token stays in the client, and the ticket is the thing a game server is allowed to see. Verification
 * goes through WowAuthFn, so the real implementation is a call to IDUNA, not a hash invented here.
 * With no verifier installed every wire op is REFUSED (fail closed) -- a server that silently accepts
 * unauthenticated casts is worse than one that does nothing until it is wired to SSO.
 *
 * Truth about a unit (hostile? dead?) belongs to the simulation, not to this file: install
 * WowServer.unit_truth and the evaluator sees it. With no hook, a target is "exists, unknown otherwise",
 * so [harm] does not hold -- an honest no rather than a guess.
 */
#ifndef SHANKPIT_PROTOCOL_WOW_SERVER_H
#define SHANKPIT_PROTOCOL_WOW_SERVER_H

#include <stdlib.h>
#include <string.h>
#include "../ui/wow_weapons.h"   /* the generated WPN_* names a /cast may use */
#include "wow_wire.h"

#define WOW_SERVER_CLIENTS   8
#define WOW_SERVER_MACROS    8
#define WOW_SERVER_CASTS     3
#define WOW_SERVER_ID_MAX   48

typedef struct {
    WowTargets    targets;
    WowMacroDef   macros[WOW_SERVER_MACROS];
    int           macro_count;
    WowCastIntent casts[WOW_SERVER_CASTS];
    int           cast_count;
    uint16_t      last_cast_seq;
    unsigned char authed;
    char          player_id[WOW_SERVER_ID_MAX];
} WowServerClient;

/* 1 accepted (fill player_id), 0 refused, -1 error. THE IDUNA HOOK. */
typedef int (*WowAuthFn)(void *ctx, const unsigned char *ticket, int len, char *player_id, int cap);

/* 1 handled and an ACK was written, 0 handled without an ACK (stale target / refused action),
   -2 unauthenticated (or no verifier), -1 malformed. */
typedef struct {
    WowServerClient c[WOW_SERVER_CLIENTS];
    WowAuthFn auth;
    void *auth_ctx;
    int preset;
    int require_auth;
    void (*unit_truth)(void *ctx, int client, uint16_t entity, WowUnit *out);
    void *unit_ctx;
} WowServer;

static inline void wow_server_begin(WowServer *s) {
    if (!s) return;
    memset(s, 0, sizeof *s);
    s->preset = 0;
    s->require_auth = 1;      /* fail closed until IDUNA verification is installed */
}

static inline WowServerClient *wow_server_client(WowServer *s, int client) {
    if (!s || client < 0 || client >= WOW_SERVER_CLIENTS) return NULL;
    return &s->c[client];
}

/* Resolve a macro's unit token to the entity this client's own target table holds. */
static inline uint16_t wow_server_entity_of(const WowServerClient *c, int token) {
    int slot, e;
    if (!c) return 0;
    slot = (token == WOW_TOKEN_FOCUS) ? 1 : (token == WOW_TOKEN_TARGET ? 0 : -1);
    if (slot < 0) return 0;
    e = wow_targets_get(&c->targets, slot);
    return e > 0 ? (uint16_t)e : 0;
}

/* Bounded by PARENA's cast queue for the preset. Duplicates (same seq) are dropped, not queued twice. */
static inline int wow_server_queue_cast(WowServer *s, int client, unsigned char weapon,
                                        unsigned char token, uint16_t seq) {
    WowServerClient *c = wow_server_client(s, client);
    int cap;
    if (!c) return -1;
    if (seq == c->last_cast_seq) return 0;
    cap = wow_cast_queue(s->preset);
    if (cap > WOW_SERVER_CASTS) cap = WOW_SERVER_CASTS;
    if (c->cast_count >= cap) return 0;
    c->casts[c->cast_count].weapon = weapon;
    c->casts[c->cast_count].token = token;
    c->casts[c->cast_count].seq = seq;
    c->cast_count++;
    c->last_cast_seq = seq;
    return 1;
}

/* FIFO. The simulation calls this once per tick per client and applies what it gets. */
static inline int wow_server_take_cast(WowServer *s, int client, WowCastIntent *out) {
    WowServerClient *c = wow_server_client(s, client);
    int i;
    if (!c || !out || c->cast_count <= 0) return 0;
    *out = c->casts[0];
    for (i = 1; i < c->cast_count; i++) c->casts[i - 1] = c->casts[i];
    c->cast_count--;
    return 1;
}

typedef struct { WowServer *s; int client; } WowServerUnitCtx;

static inline WowUnit wow_server_unit(void *p, int token) {
    WowUnit u;
    WowServerUnitCtx *c = (WowServerUnitCtx *)p;
    uint16_t entity;
    memset(&u, 0, sizeof u);
    if (!c || !c->s) return u;
    entity = wow_server_entity_of(wow_server_client(c->s, c->client), token);
    if (!entity) return u;
    u.exists = 1;
    if (c->s->unit_truth) c->s->unit_truth(c->s->unit_ctx, c->client, entity, &u);
    u.exists = 1;             /* the table says it is there; the hook may only refine the rest */
    return u;
}

static inline void wow_server_ack(uint8_t *ack, uint16_t seq, uint8_t kind) {
    WowAck a;
    a.seq = seq;
    a.kind = kind;
    wow_ack_encode(&a, ack, WOW_WIRE_ACK_BYTES);
}

static inline WowMacroDef *wow_server_macro(WowServerClient *c, unsigned char id) {
    int i;
    if (!c) return NULL;
    for (i = 0; i < c->macro_count; i++)
        if (c->macros[i].id == id) return &c->macros[i];
    return NULL;
}

static inline int wow_server_rx(WowServer *s, int client, const uint8_t *frame, int len,
                                uint8_t *ack, int cap) {
    WowServerClient *c = wow_server_client(s, client);
    if (!c || !frame || len < 2 || !ack || cap < WOW_WIRE_ACK_BYTES) return -1;
    if (frame[0] != WOW_WIRE_VERSION) return -1;

    if (frame[1] == WOW_OP_AUTH) {
        WowAuthFrame a;
        int ok;
        if (wow_auth_decode(frame, len, &a) < 0) return -1;
        if (!s->auth) return -2;                       /* no IDUNA verifier: refuse, do not guess */
        ok = s->auth(s->auth_ctx, a.ticket, WOW_WIRE_TICKET_BYTES, c->player_id, WOW_SERVER_ID_MAX);
        if (ok != 1) { c->authed = 0; c->player_id[0] = '\0'; return -2; }
        c->authed = 1;
        wow_server_ack(ack, 0, (uint8_t)WOW_OP_AUTH);
        return 1;
    }
    if (s->require_auth && !c->authed) return -2;

    if (frame[1] == WOW_OP_SET_TARGET || frame[1] == WOW_OP_CLEAR_TARGET) {
        int r = wow_targets_apply(&c->targets, frame, len);
        if (r == 2) { wow_server_ack(ack, c->targets.seq, frame[1]); return 1; }
        return r == 0 ? 0 : -1;
    }
    if (frame[1] == WOW_OP_CAST) {
        WowCastIntent ci;
        if (wow_cast_decode(frame, len, &ci) < 0) return -1;
        if (strcmp(wow_weapon_name(ci.weapon), "?") == 0) return -1;   /* no such weapon: refuse */
        if (!wow_server_queue_cast(s, client, ci.weapon, ci.token, ci.seq)) return 0;
        wow_server_ack(ack, ci.seq, (uint8_t)WOW_OP_CAST);
        return 1;
    }
    if (frame[1] == WOW_OP_DEFINE_MACRO) {
        WowMacroDef d;
        WowMacroDef *slot;
        if (wow_macro_decode_def(frame, len, &d) < 0) return -1;
        slot = wow_server_macro(c, d.id);
        if (!slot) {
            if (c->macro_count >= WOW_SERVER_MACROS) return 0;
            slot = &c->macros[c->macro_count++];
        }
        *slot = d;
        wow_server_ack(ack, 0, (uint8_t)WOW_OP_DEFINE_MACRO);
        return 1;
    }
    if (frame[1] == WOW_OP_INVOKE_MACRO) {
        WowInvoke v;
        WowMacroDef *def;
        WowServerUnitCtx uc;
        int token = 0;
        const char *arg = NULL;
        if (wow_invoke_decode(frame, len, &v) < 0) return -1;
        def = wow_server_macro(c, v.macro_id);
        if (!def) return 0;
        uc.s = s;
        uc.client = client;
        if (!wow_macro_resolve(&def->line, &uc, wow_server_unit, &token, &arg)) return 0;
        switch (def->line.cmd) {
            case WOW_CMD_CAST: {
                int w = wow_weapon_from_name(arg ? arg : "");
                if (w < 0) return 0;
                if (!wow_server_queue_cast(s, client, (unsigned char)w, (unsigned char)token, v.seq)) return 0;
                break;
            }
            case WOW_CMD_USE: {
                int w = wow_weapon_from_slot(arg ? atoi(arg) : 0);
                if (w < 0) return 0;
                if (!wow_server_queue_cast(s, client, (unsigned char)w, (unsigned char)token, v.seq)) return 0;
                break;
            }
            case WOW_CMD_TARGET: {
                uint16_t e = wow_server_entity_of(c, token);
                if (!e || wow_targets_set(&c->targets, 0, (int)e) < 0) return 0;
                break;
            }
            case WOW_CMD_FOCUS: {
                uint16_t e = wow_server_entity_of(c, token);
                if (!e || wow_targets_set(&c->targets, 1, (int)e) < 0) return 0;
                break;
            }
            case WOW_CMD_CLEARTARGET: wow_targets_clear(&c->targets, 0); break;
            case WOW_CMD_CLEARFOCUS:  wow_targets_clear(&c->targets, 1); break;
            default: return 0;
        }
        wow_server_ack(ack, v.seq, (uint8_t)WOW_OP_INVOKE_MACRO);
        return 1;
    }
    return -1;
}

#endif /* SHANKPIT_PROTOCOL_WOW_SERVER_H */
