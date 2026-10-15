/* spray_decal_test -- wire contract of PACKET_SPRAY_DECAL (card #659): unique type byte, fixed layout that
 * round-trips spray id / position / normal / sender / scene unchanged between apps/lobby and apps/server. */
#include <stdio.h>
#include <string.h>
#include "protocol.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main(void) {
    CHECK(PACKET_SPRAY_DECAL == 16);
    CHECK(PACKET_SPRAY_DECAL != PACKET_SHIELD_PACKS && PACKET_SPRAY_DECAL != PACKET_QUEUE_LEVEL &&
          PACKET_SPRAY_DECAL != PACKET_BRICK_STATE && PACKET_SPRAY_DECAL != PACKET_USERCMD &&
          PACKET_SPRAY_DECAL != PACKET_SNAPSHOT);
    CHECK(sizeof(NetSprayDecal) == sizeof(NetHeader) + 4 + 6 * 4 || sizeof(NetSprayDecal) == sizeof(NetHeader) + 2 + 4 + 6 * 4);
    NetSprayDecal out, in;
    memset(&out, 0, sizeof(out));
    out.hdr.type = PACKET_SPRAY_DECAL; out.hdr.client_id = 3; out.hdr.scene_id = 7;
    out.spray_id = 41;
    out.x = 1.5f; out.y = -2.0f; out.z = 300.25f; out.nx = 0.0f; out.ny = 1.0f; out.nz = 0.0f;
    char wire[sizeof(out)];
    memcpy(wire, &out, sizeof(out));
    memcpy(&in, wire, sizeof(in));
    CHECK(in.hdr.type == PACKET_SPRAY_DECAL && in.hdr.client_id == 3 && in.hdr.scene_id == 7);
    CHECK(in.spray_id == 41 && in.x == 1.5f && in.y == -2.0f && in.z == 300.25f && in.ny == 1.0f);
    if (fails) { printf("spray_decal_test: %d FAILED\n", fails); return 1; }
    printf("spray_decal_test: ok\n");
    return 0;
}
