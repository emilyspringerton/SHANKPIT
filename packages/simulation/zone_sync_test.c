/* zone_sync_test.c -- the zone wire contract, and the reload decision that keeps a 1 Hz packet from
 * rebuilding the world every second.
 *
 * The interesting checks are the refusals: a short datagram, the wrong packet type, an id with no
 * terminator, and a repeated packet that must NOT trigger a reload.
 */
#include <stdio.h>
#include <string.h>

#include "zone_sync.h"

static int g_fail = 0, g_checks = 0;
static void ok(int cond, const char *what) {
    g_checks++;
    if (!cond) { g_fail++; printf("FAIL  %s\n", what); }
    else printf("ok    %s\n", what);
}

int main(void) {
    NetZoneState pkt;
    ZoneSync in, loaded;

    ok(PACKET_ZONE_STATE != PACKET_SNAPSHOT && PACKET_ZONE_STATE != PACKET_WELCOME &&
       PACKET_ZONE_STATE != PACKET_QUEUE_LEVEL && PACKET_ZONE_STATE != PACKET_BRICK_STATE &&
       PACKET_ZONE_STATE != PACKET_WORLD_CLOCK && PACKET_ZONE_STATE != PACKET_SPRAY_DECAL,
       "the zone packet has its own wire type");

    zone_sync_fill(&pkt, "dungeon_7", 0xDEADBEEFu, 3, 1, 0, 1234);
    ok(pkt.hdr.type == PACKET_ZONE_STATE && pkt.hdr.timestamp == 1234, "fill stamps the header");
    ok(strcmp(pkt.zone_id, "dungeon_7") == 0 && pkt.seed == 0xDEADBEEFu, "with the zone's identity");
    ok(pkt.players == 3 && pkt.state == 1 && pkt.idle_ms == 0, "and its occupancy");

    memset(&in, 0, sizeof in);
    ok(zone_sync_decode(&in, &pkt, (int)sizeof pkt) == 1, "the packet decodes");
    ok(strcmp(in.zone_id, "dungeon_7") == 0 && in.seed == 0xDEADBEEFu && in.players == 3 && in.state == 1 &&
       in.active == 1, "with every field intact");

    /* refusals -- a packet is untrusted input */
    ok(zone_sync_decode(&in, &pkt, (int)sizeof pkt - 1) == 0, "a short datagram is refused");
    {
        NetZoneState wrong = pkt;
        wrong.hdr.type = PACKET_SNAPSHOT;
        ok(zone_sync_decode(&in, &wrong, (int)sizeof wrong) == 0, "the wrong packet type is refused");
    }
    {
        NetZoneState bad = pkt;
        memset(bad.zone_id, 'A', sizeof bad.zone_id);   /* no terminator: not a safe filename */
        ok(zone_sync_decode(&in, &bad, (int)sizeof bad) == 0, "an unterminated zone id is refused");
    }
    {
        NetZoneState empty = pkt;
        empty.zone_id[0] = '\0';
        ok(zone_sync_decode(&in, &empty, (int)sizeof empty) == 0, "an empty zone id is refused");
    }

    /* the reload decision: exactly once per real change */
    memset(&loaded, 0, sizeof loaded);
    ok(zone_sync_should_reload(&loaded, &in) == 1, "nothing loaded yet: the first packet loads the zone");

    loaded = in;
    {
        ZoneSync repeat = in;
        ok(zone_sync_should_reload(&loaded, &repeat) == 0, "the same zone at 1 Hz does NOT reload the world");
    }
    {
        ZoneSync same_zone_new_seed = in;
        same_zone_new_seed.seed = 12345u;
        ok(zone_sync_should_reload(&loaded, &same_zone_new_seed) == 1,
           "the same zone id with a new seed reloads (it expired and regenerated)");
    }
    {
        ZoneSync other = in;
        snprintf(other.zone_id, sizeof other.zone_id, "dungeon_8");
        ok(zone_sync_should_reload(&loaded, &other) == 1, "a different zone reloads");
    }
    {
        ZoneSync dead = in;
        dead.state = 0;
        dead.active = 0;
        ok(zone_sync_should_reload(&loaded, &dead) == 0, "an uninitialized zone never reloads anything");
        ok(zone_sync_should_reload(&loaded, NULL) == 0, "and a missing packet is not a reload");
    }

    /* occupancy and the idle countdown ride along so a client can show them */
    zone_sync_fill(&pkt, "dungeon_7", 0xDEADBEEFu, 0, 2, 42000, 9000);
    ok(zone_sync_decode(&in, &pkt, (int)sizeof pkt) == 1 && in.state == 2 && in.players == 0 && in.idle_ms == 42000,
       "an idle zone's countdown survives the wire");
    {
        ZoneSync same = in;
        ok(zone_sync_should_reload(&loaded, &same) == 0,
           "and an idle zone with the same seed does not reload (it is still the same dungeon)");
    }

    /* clamping rather than wrapping */
    zone_sync_fill(&pkt, "dungeon_7", 1u, 1000, 1, 0, 1);
    ok(pkt.players == 255, "an absurd player count clamps instead of wrapping");
    zone_sync_fill(&pkt, NULL, 1u, -5, 1, 0, 1);
    ok(pkt.zone_id[0] == '\0' && pkt.players == 0, "a missing id becomes empty, a negative count becomes zero");

    printf("\n%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
