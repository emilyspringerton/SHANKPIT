/* empire_sync_test.c -- the EMPIRE shared world on the wire. `make test-empire-sync`.
 *
 * What this proves, without a socket: the host's world survives a full fill -> bytes -> decode -> apply
 * round trip (stockpiles, node amounts, who owns what); a packet claiming more entries than the struct
 * holds, the wrong type, and a short datagram are all refused rather than half-trusted; and applying a
 * packet never moves anything -- positions and radii stay whatever the local level authored, because the
 * host must not be able to drag a resource node across the map with a spoofed datagram.
 */
#include <stdio.h>
#include <string.h>

#include "empire_units_rules_host.h"
#include "empire_sync.h"

static int g_checks = 0;
static int g_failures = 0;

static void ok(int cond, const char *what) {
    g_checks++;
    if (!cond) {
        g_failures++;
        printf("FAIL  %s\n", what);
    } else {
        printf("ok    %s\n", what);
    }
}

/* A load of the shape a level authors: three nodes, four cells. */
static void build_world(EUWorld *w) {
    float nx[3] = { 10.0f, -10.0f, 0.0f };
    float nz[3] = { 0.0f, 5.0f, -20.0f };
    int amt[3] = { 7, 4, 9 };
    float cx[4] = { 20.0f, -20.0f, 0.0f, 4.0f };
    float cz[4] = { 0.0f, 0.0f, 20.0f, -4.0f };
    int rad[4] = { 0, 9, 0, 0 };
    eu_world_load(w, 3, nx, nz, amt, 4, cx, cz, rad);
}

int main(void) {
    EUWorld host, client;
    build_world(&host);
    build_world(&client);

    /* The host really gathers and really takes ground, through the same functions the tick calls. */
    EUUnit ally, hostile;
    eu_unit_init(&ally, EU_ARCH_ALLY, 0, -1, -1);
    eu_unit_init(&hostile, EU_ARCH_SOLDIER, 0, -1, -1);
    eu_world_gather(&host, &ally, 0);
    eu_world_gather(&host, &ally, 0);
    eu_world_claim_tick(&host, &hostile, 1, 30000);
    ok(host.nodes[0].amount == 5 && host.stockpile[EU_FACTION_ALLY] == 2, "host gathers and banks");
    ok(host.cells[1].owner == EU_FACTION_HOSTILE, "host takes ground");

    printf("== round trip ==\n");
    NetEmpireState pkt;
    empire_sync_fill(&pkt, &host, 1234u);
    ok(pkt.hdr.type == PACKET_EMPIRE_STATE, "filled packet carries its own type");
    ok(pkt.node_count == 3 && pkt.cell_count == 4, "counts travel with the entries");

    NetEmpireState got;
    ok(empire_sync_decode(&got, &pkt, (int)sizeof pkt) == 1, "a full, well-formed packet decodes");
    ok(got.stockpile[EU_FACTION_ALLY] == 2, "the ally stockpile survives the wire");
    ok(empire_sync_apply(&client, &got) == 3, "and applies to the client's world");

    ok(client.nodes[0].amount == 5 && client.nodes[1].amount == 4 && client.nodes[2].amount == 9,
       "every node's remaining amount is mirrored");
    ok(client.stockpile[EU_FACTION_ALLY] == 2 && client.stockpile[EU_FACTION_HOSTILE] == 0,
       "stockpiles are mirrored, not accumulated");
    ok(client.cells[1].owner == EU_FACTION_HOSTILE && client.cells[0].owner == EU_FACTION_NEUTRAL,
       "ground held is mirrored exactly");

    printf("== nothing moves ==\n");
    ok(client.nodes[0].x == 10.0f && client.nodes[0].z == 0.0f, "node positions stay as locally authored");
    ok(client.nodes[2].z == -20.0f && client.cells[1].radius == 9,
       "positions and radii are never on the wire, so they cannot be spoofed");
    ok(client.nodes[0].capacity == 7, "capacity is the level's own number, not the remaining amount");
    ok(client.cells[1].claim_ms == 0, "a client cannot keep a claim bar the host did not grant");

    printf("== untrusted input ==\n");
    NetEmpireState bad;
    memcpy(&bad, &pkt, sizeof bad);
    bad.hdr.type = PACKET_ZONE_STATE;
    ok(empire_sync_decode(&got, &bad, (int)sizeof bad) == 0, "the wrong packet type is refused");
    ok(empire_sync_decode(&got, &pkt, (int)sizeof(NetHeader)) == 0, "a short datagram is refused");
    memcpy(&bad, &pkt, sizeof bad);
    bad.node_count = NET_EMPIRE_MAX_NODES + 1;
    ok(empire_sync_decode(&got, &bad, (int)sizeof bad) == 0, "a count past the array is refused");
    memcpy(&bad, &pkt, sizeof bad);
    bad.cell_count = NET_EMPIRE_MAX_CELLS + 1;
    ok(empire_sync_decode(&got, &bad, (int)sizeof bad) == 0, "the same for the claim cells");
    memcpy(&bad, &pkt, sizeof bad);
    bad.cell_owner[0] = 200;
    ok(empire_sync_decode(&got, &bad, (int)sizeof bad) == 1, "an out-of-range owner is not a whole-packet error");
    empire_sync_apply(&client, &got);
    ok(client.cells[0].owner == EU_FACTION_NEUTRAL, "an unknown faction byte lands as neutral");

    printf("== overlap only ==\n");
    {
        EUWorld small;
        float nx[1] = { 3.0f }, nz[1] = { 4.0f };
        int amt[1] = { 2 };
        eu_world_load(&small, 1, nx, nz, amt, 0, NULL, NULL, NULL);
        ok(empire_sync_apply(&small, &pkt) == 1, "a client with fewer nodes applies only what it has");
        ok(small.nodes[0].amount == 5 && small.nodes[0].x == 3.0f, "the overlapping node is updated in place");
    }

    printf("== an EMPIRE-free host ==\n");
    {
        EUWorld none;
        eu_world_clear(&none);
        NetEmpireState empty;
        empire_sync_fill(&empty, &none, 0u);
        ok(empire_sync_decode(&got, &empty, (int)sizeof empty) == 1, "an empty world still makes a valid packet");
        ok(empire_sync_apply(&client, &got) == 0, "and a world with no nodes reports that it applied nothing");
    }

    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
