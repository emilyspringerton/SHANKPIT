/* citizen_sync.h -- how a MODE_ZOMBIES citizen's REFLUX actions reach everybody else.
 *
 * The gap this closes, stated plainly: a citizen is a real PlayerState, so where its body is already
 * reaches clients on PACKET_SNAPSHOT -- but the FACT never did. "This citizen slid", "that one
 * slide-jumped into a zombie", "it drew the AR" were dispatched on the sim host and died there, so a
 * mod running on a client could not react to a citizen at all. That is the half of the founder's
 * "syncs to the network via reflux so other mods can interact" that horses already had and citizens
 * did not.
 *
 * There is deliberately only ONE packet here, unlike horses. A horse needed PACKET_HORSE_STATE
 * because a client that loads a level has no horse at all unless the server says so. Citizens are
 * players: the snapshot already carries their position, velocity, crouch and weapon, and it is
 * absolute and idempotent, so a dropped event costs a mod one line of activity, never a wrong world.
 * Duplicating citizen bodies into a state packet would be a second, divergent copy of the snapshot.
 *
 * Trust boundary, stated rather than assumed: a relayed row is untrusted input. apply_events refuses
 * anything outside reflux_action_is_citizen's range, so a peer can never make a client's mod believe
 * a horse was mounted or a door was pressed. The three payload scalars are opaque I32s exactly as
 * REFLUX carries them -- they are read by mods, never used as indices into anything here.
 *
 * The cursor is an ABSOLUTE dispatch number, not a retained-log index, for the reason reflux_runtime.h
 * documents at length: reflux_host_log_size() saturates at REFLUX_LOG_CAPACITY, so a high-water mark
 * taken from the log's own size goes permanently silent after 256 dispatches of ANY family. The horse
 * relay had exactly that bug for one pass; this one is written correctly from the start.
 *
 * `max` is a datagram budget, not a limit on delivery: the cursor is left ON the first row that did
 * not fit, so the caller loops until it returns 0 (the server loops four times a snapshot, 32 rows).
 * Rows the ring overwrote before the drain are skipped rather than faked, and citizen_sync_missed()
 * makes that loss a reported number instead of an unexplained gap in a mod's timeline.
 */
#ifndef CITIZEN_SYNC_H
#define CITIZEN_SYNC_H

#include "protocol.h"
#include "../reflux/reflux_runtime.h"

/* Drain the citizen family of REFLUX actions past *cursor, up to `max` rows. A fresh cursor (-1)
 * baselines at the current dispatch count instead of replaying host history at a newly joined peer.
 * Returns how many rows were written. */
static inline int citizen_sync_collect_events(NetCitizenEvent *out, int max, int *cursor) {
    if (!out || !cursor || max <= 0) return 0;
    int total = reflux_host_total_dispatched();
    if (*cursor < 0 || *cursor > total) *cursor = total;
    int k = 0;
    int abs = *cursor;
    for (; abs < total; abs++) {
        int idx = reflux_host_index_for_abs(abs);
        if (idx < 0) continue;              /* overwritten before we got to it */
        int type = reflux_host_action_type_at(idx);
        if (!reflux_action_is_citizen(type)) continue;
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

/* How many rows the cursor has already lost to REFLUX's ring (0 while it is still inside it). Call
 * before collecting: the answer is "genuinely gone", not "not sent yet". */
static inline int citizen_sync_missed(int cursor) {
    if (cursor < 0) return 0;                       /* a fresh cursor has no history to lose */
    int oldest = reflux_host_oldest_abs();
    return (cursor < oldest) ? (oldest - cursor) : 0;
}

/* Re-dispatch relayed rows into THIS peer's own REFLUX log, so a mod polling here sees a citizen
 * slide that happened on the server without knowing the server exists. Anything outside the family
 * is refused, not forwarded. Returns how many rows were accepted. */
static inline int citizen_sync_apply_events(const NetCitizenEvent *in, int n) {
    if (!in || n <= 0) return 0;
    int applied = 0;
    for (int i = 0; i < n; i++) {
        if (!reflux_action_is_citizen(in[i].type)) continue;
        reflux_host_dispatch(in[i].type, in[i].a, in[i].b, in[i].c);
        applied++;
    }
    return applied;
}

#endif /* CITIZEN_SYNC_H */
