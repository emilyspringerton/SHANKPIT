/* zone_sync.h -- the zone's IDENTITY on the wire, and the one decision a client makes from it.
 *
 * The founder's co-op shape: "the first person to enter it it generates and then everyone else enters the
 * same one". On a LAN that means the host owns the zone and the client must reach the SAME dungeon --
 * without a level transfer. So PACKET_ZONE_STATE carries (zone id, seed, players, state, idle_ms) and a
 * client rebuilds the layout locally from the seed with proc_dungeon.h.
 *
 * That is only sound because generation is deterministic: the same seed produces byte-identical level
 * JSON on every machine (`make test-proc-dungeon` asserts exactly that). This header is the small,
 * testable seam between "a packet arrived" and "reload the world":
 *
 *   zone_sync_decode      -- validate untrusted bytes (length, type, non-empty NUL-terminated id)
 *   zone_sync_should_reload -- reload only when the zone or its seed actually changed, never on a repeat
 *   zone_sync_fill        -- build the packet on the server side, so encode and decode cannot drift
 *
 * Deliberately no I/O and no networking: the caller owns the socket and the level load.
 */
#ifndef SHANKPIT_ZONE_SYNC_H
#define SHANKPIT_ZONE_SYNC_H

#include <stdio.h>
#include <string.h>

#include "../common/protocol.h"

typedef struct {
    int active;                     /* the packet described a zone that exists (state != 0) */
    char zone_id[NET_ZONE_ID_MAX];
    unsigned int seed;
    int players;
    int state;                      /* zone_registry.h's ZONE_STATE_* */
    unsigned int idle_ms;
} ZoneSync;

static inline void zone_sync_clear(ZoneSync *s) {
    if (s) memset(s, 0, sizeof *s);
}

/* Decodes one datagram. Returns 1 only for a complete, well-formed zone packet: a packet is untrusted
 * input, and a half-copied id would otherwise become a filename (the host saves var/maps/proc_<id>.json). */
static inline int zone_sync_decode(ZoneSync *out, const void *buf, int len) {
    const NetZoneState *p = (const NetZoneState *)buf;
    if (!out || !buf || len < (int)sizeof(NetZoneState)) return 0;
    if (p->hdr.type != PACKET_ZONE_STATE) return 0;
    if (p->zone_id[0] == '\0') return 0;
    if (memchr(p->zone_id, '\0', sizeof p->zone_id) == NULL) return 0;
    memset(out, 0, sizeof *out);
    snprintf(out->zone_id, sizeof out->zone_id, "%s", p->zone_id);
    out->seed = p->seed;
    out->players = (int)p->players;
    out->state = (int)p->state;
    out->idle_ms = p->idle_ms;
    out->active = (p->state != 0) ? 1 : 0;
    return 1;
}

/* Reload only for a zone that exists and is not the one already loaded. A repeated packet (1 Hz) must be
 * a no-op: reloading geometry every second would drop every decal, spray and cached shot in the world. */
static inline int zone_sync_should_reload(const ZoneSync *loaded, const ZoneSync *incoming) {
    if (!incoming || !incoming->active) return 0;
    if (!loaded || !loaded->active) return 1;
    if (strcmp(loaded->zone_id, incoming->zone_id) != 0) return 1;
    if (loaded->seed != incoming->seed) return 1;
    return 0;
}

/* Server side: one place builds the packet, so the encode cannot drift from the decode. */
static inline void zone_sync_fill(NetZoneState *pkt, const char *zone_id, unsigned int seed, int players,
                                  int state, unsigned int idle_ms, unsigned int now_ms) {
    if (!pkt) return;
    memset(pkt, 0, sizeof *pkt);
    pkt->hdr.type = PACKET_ZONE_STATE;
    pkt->hdr.client_id = 0;
    pkt->hdr.timestamp = now_ms;
    snprintf(pkt->zone_id, sizeof pkt->zone_id, "%s", (zone_id && zone_id[0]) ? zone_id : "");
    pkt->seed = seed;
    pkt->idle_ms = idle_ms;
    pkt->players = (unsigned char)(players < 0 ? 0 : (players > 255 ? 255 : players));
    pkt->state = (unsigned char)state;
}

#endif /* SHANKPIT_ZONE_SYNC_H */
