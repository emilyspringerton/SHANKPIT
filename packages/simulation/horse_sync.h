/* horse_sync.h -- how horses reach the network, and how their REFLUX events reach other mods on
 * the other side of it.
 *
 * The problem this solves, stated plainly: before this, a horse was simulated by whichever host
 * loaded the level and drawn only there. A second player standing in the same paddock saw an empty
 * farm, and no mod anywhere could react to a horse because nothing about a horse ever left the
 * process that owned it.
 *
 * Two packets, for two genuinely different kinds of fact:
 *
 *   PACKET_HORSE_STATE  -- the STATE of every horse in the client's scene. Absolute, whole-scene,
 *                          idempotent: apply it twice, late, or out of order and the client still
 *                          agrees with the server, because a horse the packet does not mention is
 *                          simply not there any more. A dropped datagram heals on the next one and
 *                          a late joiner is correct on arrival. State goes on the wire because it
 *                          must not be missed.
 *
 *   PACKET_HORSE_EVENTS -- the horse family of REFLUX actions, relayed. Events go on the wire
 *                          because they are the mod-interaction channel: "this horse was mounted",
 *                          "it jumped", "it is spent", "it bolted". The server's own tick dispatches
 *                          at the moment each happens; horse_sync_collect_events drains the family
 *                          past a cursor; horse_sync_apply_events re-dispatches each row into the
 *                          receiving peer's own REFLUX log, so a mod that polls there sees the same
 *                          thing a mod polling on the server does -- without either mod knowing the
 *                          other exists.
 *
 * Trust boundary, stated rather than assumed: a relayed event is untrusted input. apply_events
 * refuses any type outside reflux_action_is_horse's range, so a peer can never make a client's mod
 * believe a door button was pressed or a shadow war resolved. The state applier clips every field
 * the same way (slot bounds, stamina 0..1000, goal range, occupant range), so a malformed or hostile
 * packet can at worst move a horse somewhere silly, never index out of bounds.
 *
 * No acknowledgement traffic exists, by construction: state is a repeat, and events are telemetry.
 * Losing an event costs a mod one line of activity; losing the state costs one tick of a position,
 * healed by the next packet.
 *
 * Batch size is a datagram budget, not a limit on what gets delivered: collect_events stops at
 * `max` and leaves the cursor ON the first undelivered row, so the caller loops until it returns 0
 * (the server loops four times a snapshot, i.e. up to 32 rows). The cursor is an ABSOLUTE dispatch
 * number, not a retained-log index, because reflux_host_log_size() saturates at
 * REFLUX_LOG_CAPACITY -- the earlier version of this file used the size as its high-water mark and
 * would have gone permanently silent after 256 dispatches, which is a real bug this pass found.
 *
 * The one bound that remains is REFLUX's own ring (256 actions of ANY family between two drains),
 * and horse_sync_missed() is what makes it visible rather than silent: a cursor overtaken by the
 * ring can say how many rows it lost, and the server logs it instead of pretending the stream was
 * whole. State is carried separately and cannot be missed, so a loss costs a mod activity, not truth.
 */
#ifndef HORSE_SYNC_H
#define HORSE_SYNC_H

#include <string.h>

#include "protocol.h"
#include "../reflux/reflux_runtime.h"

/* Fill `out` with every horse in `scene_id` (the same per-scene filter PACKET_VEHICLE_STATE uses,
 * so a horse in another scene is never described to someone who cannot see it). Returns the count
 * written, at most `max` and at most NET_HORSE_MAX_ENTRIES. */
static inline int horse_sync_fill(const ServerState *st, int scene_id, NetHorseEntry *out, int max) {
    if (!st || !out) return 0;
    if (max > NET_HORSE_MAX_ENTRIES) max = NET_HORSE_MAX_ENTRIES;
    int n = 0;
    for (int i = 0; i < MAX_HORSES && n < max; i++) {
        const HorseState *h = &st->horses[i];
        if (!h->active || h->scene_id != scene_id) continue;
        NetHorseEntry *e = &out[n++];
        memset(e, 0, sizeof(*e));
        e->slot = (unsigned char)i;
        e->goal = (unsigned char)((h->goal >= 0 && h->goal < HORSE_GOAL_COUNT) ? h->goal : HORSE_GOAL_IDLE);
        e->skin = h->skin;
        e->ground = (unsigned char)(h->grounded ? 1 : 0);
        e->occupant = (short)((h->occupant_player_id >= 0 && h->occupant_player_id < MAX_CLIENTS)
                              ? h->occupant_player_id : -1);
        int stamina = h->stamina_milli;
        if (stamina < 0) stamina = 0;
        if (stamina > HORSE_STAMINA_MAX_MILLI) stamina = HORSE_STAMINA_MAX_MILLI;
        e->stamina = (unsigned short)stamina;
        int carrots = h->carrots;
        if (carrots < 0) carrots = 0;
        if (carrots > 255) carrots = 255;
        e->carrots = (unsigned char)carrots;
        e->x = h->x; e->y = h->y; e->z = h->z;
        e->yaw = h->yaw;
        e->speed = h->speed;
    }
    return n;
}

/* Apply a whole-scene horse state. `len` is the received datagram length, so a truncated packet
 * describes fewer horses instead of reading past its own end. Returns how many entries landed. */
static inline int horse_sync_apply(ServerState *st, const NetHorseState *pkt, int len) {
    if (!st || !pkt) return 0;
    int held = (len - (int)(sizeof(NetHeader) + 4)) / (int)sizeof(NetHorseEntry);
    if (held < 0) held = 0;
    int want = pkt->count;
    if (want > held) want = held;
    if (want > NET_HORSE_MAX_ENTRIES) want = NET_HORSE_MAX_ENTRIES;

    /* Whole-scene semantics: a horse in this scene the packet does not mention is gone. */
    int scene = (int)pkt->hdr.scene_id;
    for (int i = 0; i < MAX_HORSES; i++)
        if (st->horses[i].scene_id == scene) st->horses[i].active = 0;

    int applied = 0;
    for (int k = 0; k < want; k++) {
        const NetHorseEntry *e = &pkt->e[k];
        if (e->slot >= MAX_HORSES) continue;
        HorseState *h = &st->horses[e->slot];
        memset(h, 0, sizeof(*h));
        h->active = 1;
        h->id = (int)e->slot;
        h->scene_id = scene;
        h->x = e->x; h->y = e->y; h->z = e->z;
        h->yaw = e->yaw;
        h->speed = e->speed;
        h->home_x = e->x;
        h->home_z = e->z;
        h->goal = (e->goal < HORSE_GOAL_COUNT) ? (int)e->goal : HORSE_GOAL_IDLE;
        h->goal_x = e->x;
        h->goal_z = e->z;
        h->skin = (e->skin <= HORSE_SKIN_UNICORN) ? e->skin : HORSE_SKIN_UNICORN;
        h->grounded = e->ground ? 1 : 0;
        h->occupant_player_id = (e->occupant >= 0 && e->occupant < MAX_CLIENTS) ? (int)e->occupant : -1;
        h->stamina_milli = (e->stamina > HORSE_STAMINA_MAX_MILLI) ? HORSE_STAMINA_MAX_MILLI : (int)e->stamina;
        h->carrots = (int)e->carrots;
        applied++;
    }
    return applied;
}

/* Drain the horse family of REFLUX actions past *cursor, up to `max` rows. The cursor is an ABSOLUTE
 * dispatch number (see reflux_runtime.h's own note on why the retained size cannot be one), and it
 * is left on the first row that did not fit, so calling this again delivers the rest in order.
 * A fresh cursor (-1) baselines at the current dispatch count instead of replaying history at a
 * newly joined peer.
 *
 * Returns how many rows were written. Rows that the log overwrote before they could be sent are
 * skipped, not faked: they are gone, and the state packet is what keeps the world correct. */
static inline int horse_sync_collect_events(NetHorseEvent *out, int max, int *cursor) {
    if (!out || !cursor || max <= 0) return 0;
    int total = reflux_host_total_dispatched();
    if (*cursor < 0 || *cursor > total) *cursor = total;
    int k = 0;
    int abs = *cursor;
    for (; abs < total; abs++) {
        int idx = reflux_host_index_for_abs(abs);
        if (idx < 0) continue;              /* overwritten before we got to it */
        int type = reflux_host_action_type_at(idx);
        if (!reflux_action_is_horse(type)) continue;
        if (k >= max) break;                /* leave it on the cursor for the next call */
        out[k].type = type;
        out[k].a = reflux_host_action_a_at(idx);
        out[k].b = reflux_host_action_b_at(idx);
        out[k].c = reflux_host_action_c_at(idx);
        k++;
    }
    *cursor = abs;
    return k;
}

/* How many rows a cursor has ALREADY lost to the ring (0 when it is still inside it). Call this
 * before collecting: the answer is "rows between here and the oldest retained one", i.e. genuinely
 * gone, not merely not-sent-yet. Kept as its own function so a lost event is a reported fact instead
 * of an unexplained gap in a mod's timeline -- see reflux_runtime.h's own note. */
static inline int horse_sync_missed(int cursor) {
    if (cursor < 0) return 0;                       /* a fresh cursor has no history to lose */
    int oldest = reflux_host_oldest_abs();
    return (cursor < oldest) ? (oldest - cursor) : 0;
}

/* Re-dispatch relayed events into THIS peer's own REFLUX log -- the other half of "syncs to the
 * network via reflux". Anything outside the horse family is refused, not forwarded. */
static inline int horse_sync_apply_events(const NetHorseEvent *in, int n) {
    if (!in || n <= 0) return 0;
    int applied = 0;
    for (int i = 0; i < n; i++) {
        if (!reflux_action_is_horse(in[i].type)) continue;
        reflux_host_dispatch(in[i].type, in[i].a, in[i].b, in[i].c);
        applied++;
    }
    return applied;
}

/* ── piles: the same whole-scene contract as horses, for the same reason ──────────────────────────
 *
 * What a horse leaves on the ground is STATE, not an event: a client that was not listening (or
 * joined late) still has to draw the paddock correctly, and a pile that expires has to disappear
 * on a client that never saw an event either way. So PACKET_HORSE_PILES is absolute and
 * whole-scene, exactly like PACKET_HORSE_STATE: a pile the packet does not mention is gone.
 * REFLUX_ACTION_HORSE_POOPED still fires at the drop for mods that want to REACT (that is the
 * difference between the two channels, and it is the reason both exist). */

/* Fill `out` with every live pile in `scene_id`, at most `max`. Returns the count written. */
static inline int horse_sync_fill_piles(const ServerState *st, int scene_id, NetHorsePileEntry *out, int max) {
    if (!st || !out) return 0;
    if (max > NET_HORSE_PILE_MAX) max = NET_HORSE_PILE_MAX;
    int n = 0;
    for (int i = 0; i < MAX_HORSE_PILES && n < max; i++) {
        const HorsePile *p = &st->piles[i];
        if (!p->active || p->scene_id != scene_id) continue;
        NetHorsePileEntry *e = &out[n++];
        memset(e, 0, sizeof(*e));
        e->x = p->x; e->y = p->y; e->z = p->z;
        e->horse = (short)((p->horse_id >= 0 && p->horse_id < MAX_HORSES) ? p->horse_id : -1);
    }
    return n;
}

/* Apply a whole-scene pile state. `len` is the received datagram length, so a truncated packet
 * describes fewer piles instead of reading past its own end. Returns how many entries landed. */
static inline int horse_sync_apply_piles(ServerState *st, const NetHorsePiles *pkt, int len) {
    if (!st || !pkt) return 0;
    int held = (len - (int)(sizeof(NetHeader) + 4)) / (int)sizeof(NetHorsePileEntry);
    if (held < 0) held = 0;
    int want = pkt->count;
    if (want > held) want = held;
    if (want > NET_HORSE_PILE_MAX) want = NET_HORSE_PILE_MAX;

    int scene = (int)pkt->hdr.scene_id;
    memset(st->piles, 0, sizeof(st->piles));
    for (int i = 0; i < MAX_HORSE_PILES; i++) st->piles[i].horse_id = -1;

    int applied = 0;
    for (int k = 0; k < want; k++) {
        const NetHorsePileEntry *e = &pkt->e[k];
        HorsePile *p = &st->piles[k];
        p->active = 1;
        p->scene_id = scene;
        p->horse_id = (e->horse >= 0 && e->horse < MAX_HORSES) ? (int)e->horse : -1;
        p->x = e->x; p->y = e->y; p->z = e->z;
        p->born_ms = 0;   /* host-local age; the lifetime is the server's to run */
        applied++;
    }
    return applied;
}

#endif /* HORSE_SYNC_H */
