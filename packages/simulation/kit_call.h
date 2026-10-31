/* kit_call.h -- the KIT's vehicle gadgets on the NETWORK path (PACKET_KIT_CALL, protocol.h).
 *
 * Before this a kit key was a local/sandbox action only: in STATE_GAME_NET the server owns spawning,
 * so pressing ` / - / = did nothing at all and said nothing about it (N1 in docs/NAMED_NOT_DONE.md).
 * The client cannot fix that by calling vehicle_call_spawn itself -- its buggy would exist on one
 * screen and the server would never know about it, and two clients would each mint their own car --
 * so the seam is an INTENT packet: the client says which call item it pressed, the server decides.
 *
 *   client:  kit_pulse_take(item) -> kit_call_build -> PACKET_KIT_CALL
 *   server:  kit_call_parse(buffer, size) -> kit_call_server_apply(&local_state.players[id], item)
 *   everyone: the spawned buggy / helicopter arrives in PACKET_SNAPSHOT, unchanged
 *
 * All the authority is in kit_call_server_apply. It refuses a dead or spectating caller first -- a
 * corpse calling a car is the exact thing a hostile client tries first, and a wisp seated in a buggy
 * is a player stranded in a vehicle they cannot drive -- then hands off to vehicle_call_spawn, which
 * already owns "not a call item / already driving / on cooldown / no free garage slot". Nothing here
 * trusts any field of the datagram beyond the item byte.
 *
 * Included by hosts AFTER packages/simulation/vehicle_call.h.
 */
#ifndef SHANKPIT_KIT_CALL_H
#define SHANKPIT_KIT_CALL_H

#include "vehicle_call.h"
#include "../common/protocol.h"

/* kit_call_parse -- a received PACKET_KIT_CALL as a KIT_ITEM_*, or KIT_ITEM_NONE. The datagram is
 * untrusted input: a wrong type, a short buffer, a non-vehicle item or an out-of-range byte are all
 * refusals rather than spawns. */
static inline int kit_call_parse(const void *buf, int len) {
    if (!buf || len < (int)sizeof(NetKitCall)) return KIT_ITEM_NONE;
    NetKitCall pkt;
    memcpy(&pkt, buf, sizeof(pkt));
    if (pkt.hdr.type != PACKET_KIT_CALL) return KIT_ITEM_NONE;
    int item = (int)pkt.item;
    if (!kit_item_is_vehicle(item)) return KIT_ITEM_NONE;
    return item;
}

/* kit_call_build -- the client half. One datagram per call, not batched: a call is a single
 * deliberate key press, and a lost one is a press that did not happen, which the player can repeat. */
static inline void kit_call_build(NetKitCall *out, int item, int client_id) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->hdr.type = PACKET_KIT_CALL;
    out->hdr.client_id = (unsigned char)(client_id & 0xFF);
    out->item = (unsigned char)(item & 0xFF);
}

/* kit_call_server_apply -- the server's verdict. Returns VEH_NONE when refused, else the vehicle type
 * that was spawned (VEH_BUGGY covers the rocket car, which IS a buggy). */
static inline int kit_call_server_apply(PlayerState *p, int item) {
    if (!p || !p->active) return VEH_NONE;
    if (!kit_item_is_vehicle(item)) return VEH_NONE;
    if (p->state == STATE_DEAD || p->state == STATE_SPECTATOR) return VEH_NONE;
    return vehicle_call_spawn(p, item);
}

#endif /* SHANKPIT_KIT_CALL_H */
