/* empire_sync.h -- the EMPIRE shared world's mutable state on the wire, and the one rule a client applies.
 *
 * Founder: "make sure we can configure a skin for an ally ... spawning in citizens zombies soldiers etc
 * ... taking over territory gathering resources" and, for the world itself, that a LAN ally who joins the
 * host's game is in the SAME world, not a private copy of it. The host owns the shared world: it is what
 * actually gathers, claims and banks. A client therefore mirrors it, and this header is the small,
 * testable seam between "a packet arrived" and "show that":
 *
 *   empire_sync_fill   -- build the packet on the server side, so encode and decode cannot drift
 *   empire_sync_decode -- validate untrusted bytes (length, type, counts that fit the struct)
 *   empire_sync_apply  -- write the amounts/owners/stockpiles into the local world, overlapping only
 *
 * Deliberately no I/O and no networking: the caller owns the socket and the HUD.
 *
 * What is NOT sent, on purpose: positions, radii and capacities. Both sides loaded the same authored level
 * (or generated the same zone from one seed), so those are already correct locally, and letting a packet
 * move a resource node would be a trust bug rather than a feature.
 */
#ifndef SHANKPIT_EMPIRE_SYNC_H
#define SHANKPIT_EMPIRE_SYNC_H

#include <string.h>

#include "../common/protocol.h"
#include "empire_units.h"

#define EMPIRE_SYNC_AMOUNT_MAX 65535

/* Server side: one place builds the packet, so the encode cannot drift from the decode. */
static inline void empire_sync_fill(NetEmpireState *pkt, const EUWorld *w, unsigned int now_ms) {
    if (!pkt) return;
    memset(pkt, 0, sizeof *pkt);
    pkt->hdr.type = PACKET_EMPIRE_STATE;
    pkt->hdr.client_id = 0;
    pkt->hdr.timestamp = now_ms;
    if (!w) return;
    for (int f = 0; f < 3; f++) {
        int v = w->stockpile[f];
        if (v < 0) v = 0;
        if (v > EMPIRE_SYNC_AMOUNT_MAX) v = EMPIRE_SYNC_AMOUNT_MAX;
        pkt->stockpile[f] = (unsigned short)v;
    }
    pkt->node_count = (unsigned char)(w->node_count < NET_EMPIRE_MAX_NODES ? w->node_count : NET_EMPIRE_MAX_NODES);
    pkt->cell_count = (unsigned char)(w->cell_count < NET_EMPIRE_MAX_CELLS ? w->cell_count : NET_EMPIRE_MAX_CELLS);
    for (int i = 0; i < pkt->node_count; i++) {
        int v = w->nodes[i].amount;
        if (v < 0) v = 0;
        if (v > EMPIRE_SYNC_AMOUNT_MAX) v = EMPIRE_SYNC_AMOUNT_MAX;
        pkt->node_amount[i] = (unsigned short)v;
    }
    for (int i = 0; i < pkt->cell_count; i++) {
        int owner = w->cells[i].owner;
        if (owner < 0 || owner >= EU_FACTION_COUNT) owner = EU_FACTION_NEUTRAL;
        pkt->cell_owner[i] = (unsigned char)owner;
    }
}

/* Decodes one datagram into *out. Returns 1 only for a complete, well-formed empire packet: a packet is
 * untrusted input, and a count past the struct's own arrays would otherwise be read as memory. */
static inline int empire_sync_decode(NetEmpireState *out, const void *buf, int len) {
    const NetEmpireState *p = (const NetEmpireState *)buf;
    if (!out || !buf || len < (int)sizeof(NetEmpireState)) return 0;
    if (p->hdr.type != PACKET_EMPIRE_STATE) return 0;
    if (p->node_count > NET_EMPIRE_MAX_NODES) return 0;
    if (p->cell_count > NET_EMPIRE_MAX_CELLS) return 0;
    memcpy(out, p, sizeof *out);
    return 1;
}

/* Applies the host's world to the local one. Entries are matched by index in the shared level's own list,
 * so only the overlap is written: a client still loading an older world cannot be walked off the end of
 * its arrays. Owners outside EU_FACTION_* count as neutral rather than trusting the byte. Returns the
 * number of nodes it applied (0 for a packet that matches nothing, e.g. a world with no authored ground). */
static inline int empire_sync_apply(EUWorld *w, const NetEmpireState *pkt) {
    if (!w || !pkt) return 0;
    for (int f = 0; f < 3; f++) w->stockpile[f] = (int)pkt->stockpile[f];
    int nodes = pkt->node_count < w->node_count ? pkt->node_count : w->node_count;
    int cells = pkt->cell_count < w->cell_count ? pkt->cell_count : w->cell_count;
    if (nodes < 0) nodes = 0;
    if (cells < 0) cells = 0;
    for (int i = 0; i < nodes; i++) {
        w->nodes[i].amount = (int)pkt->node_amount[i];
        if (w->nodes[i].amount > w->nodes[i].capacity) w->nodes[i].capacity = w->nodes[i].amount;
    }
    for (int i = 0; i < cells; i++) {
        int owner = (int)pkt->cell_owner[i];
        w->cells[i].owner = (owner < 0 || owner >= EU_FACTION_COUNT) ? EU_FACTION_NEUTRAL : owner;
        /* The host is authoritative over ownership, so whoever was mid-claim locally stops: the host's
           own claimant progress is what decides the next flip, and a client's bar must not contradict it. */
        if (w->cells[i].owner == EU_FACTION_NEUTRAL) w->cells[i].claim_ms = 0;
    }
    return nodes;
}

#endif /* SHANKPIT_EMPIRE_SYNC_H */
