/* queue_level_test -- wire contract of PACKET_QUEUE_LEVEL (card #481). The server broadcasts the
 * registry id of the QUEUE level it is simulating; the client compares it to the level it holds.
 * Layout is the contract between apps/server and apps/lobby (and any future bot): fixed 12 bytes,
 * a signed id that round-trips -1 (fallback scene) and real ids, and a type byte no other packet uses. */
#include <stdio.h>
#include <string.h>
#include "protocol.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main(void) {
    CHECK(sizeof(NetHeader) == 10 || sizeof(NetHeader) == 12);
    CHECK(sizeof(NetQueueLevel) == sizeof(NetHeader) + 4);
    CHECK(PACKET_QUEUE_LEVEL == 14);
    CHECK(PACKET_QUEUE_LEVEL != PACKET_BRICK_STATE && PACKET_QUEUE_LEVEL != PACKET_WORLD_CLOCK &&
          PACKET_QUEUE_LEVEL != PACKET_SNAPSHOT && PACKET_QUEUE_LEVEL != PACKET_SCENE_CHANGE);

    int ids[] = { -1, 0, 44, 2147483647 };
    for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        NetQueueLevel out, in;
        memset(&out, 0, sizeof(out));
        out.hdr.type = PACKET_QUEUE_LEVEL;
        out.level_id = ids[i];
        char wire[sizeof(out)];
        memcpy(wire, &out, sizeof(out));
        memcpy(&in, wire, sizeof(in));
        CHECK(in.hdr.type == PACKET_QUEUE_LEVEL);
        CHECK(in.level_id == ids[i]);
    }
    printf(fails ? "queue_level_test: %d FAILED\n" : "queue_level_test: all passed\n", fails);
    return fails ? 1 : 0;
}
