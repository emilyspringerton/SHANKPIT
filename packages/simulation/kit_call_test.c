/* kit_call_test.c -- the kit-call packet's two halves (packages/simulation/kit_call.h):
 * parsing an untrusted datagram, and the server's verdict on a real player.
 *
 * The claims pinned here, in the order a hostile client would try them:
 *   * a well-formed call for a vehicle item parses to exactly that item, and the built packet is one
 *     fixed-size datagram;
 *   * a short datagram, a datagram claiming another packet type, a non-vehicle kit item (PHEROMONE)
 *     and an out-of-range byte all parse to KIT_ITEM_NONE -- a refusal, never a spawn;
 *   * the server refuses a dead player and a spectator (the two states a client cannot talk its way
 *     out of), and refuses a call it cannot honour (no free garage slot);
 *   * an allowed call really spawns the vehicle and seats the driver, through the SAME verb local play
 *     uses, and the vehicle cooldown then refuses the immediate second call.
 *
 * Build/run: make test-kit-call. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "physics.h"
#include "net_sim.h"
#include "kit.h"
#include "local_game.h"
#include "vehicle_call.h"
#include "kit_call.h"
#include "rocket_car_rules_host.h"
#include "drone_rules_host.h"

/* local_game.h declares these; apps/server and apps/lobby each define their own copy, and so does
 * this test (it links the simulation without either binary's main). */
int g_story_cutscene_done = 0;
int g_story_outro_requested = 0;
int g_shankpit_is_server = 0;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static PlayerState *reset_player(void) {
    memset(&local_state, 0, sizeof local_state);
    map_count = 1;
    PlayerState *p = &local_state.players[0];
    p->active = 1;
    p->id = 0;
    p->scene_id = SCENE_STADIUM;
    p->state = STATE_ALIVE;
    return p;
}

static int count_active_buggies(void) {
    int n = 0;
    for (int i = 0; i < MAX_BUGGIES; i++) if (local_state.buggies[i].active) n++;
    return n;
}

int main(void) {
    rocket_car_rules_install();
    drone_rules_install();

    /* 0. the packet is a fixed size, and a built one round-trips through the parser */
    CHECK(sizeof(NetKitCall) == sizeof(NetHeader) + 4, "NetKitCall is %d bytes, expected %d",
          (int)sizeof(NetKitCall), (int)(sizeof(NetHeader) + 4));
    {
        NetKitCall pkt;
        kit_call_build(&pkt, KIT_ITEM_HELI, 5);
        CHECK(pkt.hdr.type == PACKET_KIT_CALL, "built packet has type %d", (int)pkt.hdr.type);
        CHECK(pkt.hdr.client_id == 5, "built packet carries client %d", (int)pkt.hdr.client_id);
        CHECK(kit_call_parse(&pkt, (int)sizeof(pkt)) == KIT_ITEM_HELI, "heli call did not parse back");
        kit_call_build(&pkt, KIT_ITEM_ROCKET_CAR, 0);
        CHECK(kit_call_parse(&pkt, (int)sizeof(pkt)) == KIT_ITEM_ROCKET_CAR, "rocket car call did not parse back");
        printf("PASS: a built call parses back to the item it named\n");
    }

    /* 1. every way an untrusted datagram can be wrong is a refusal */
    {
        NetKitCall pkt;
        kit_call_build(&pkt, KIT_ITEM_BUGGY, 1);
        CHECK(kit_call_parse(&pkt, (int)sizeof(pkt) - 1) == KIT_ITEM_NONE, "a short datagram was accepted");
        CHECK(kit_call_parse(NULL, (int)sizeof(pkt)) == KIT_ITEM_NONE, "a null buffer was accepted");

        kit_call_build(&pkt, KIT_ITEM_BUGGY, 1);
        pkt.hdr.type = PACKET_USERCMD;
        CHECK(kit_call_parse(&pkt, (int)sizeof(pkt)) == KIT_ITEM_NONE, "another packet type was accepted as a call");

        kit_call_build(&pkt, KIT_ITEM_PHEROMONE, 1);      /* a real kit item, but not a call item */
        CHECK(kit_call_parse(&pkt, (int)sizeof(pkt)) == KIT_ITEM_NONE, "a non-vehicle item was accepted");

        kit_call_build(&pkt, 200, 1);                     /* out of range */
        CHECK(kit_call_parse(&pkt, (int)sizeof(pkt)) == KIT_ITEM_NONE, "an out-of-range item byte was accepted");

        kit_call_build(&pkt, KIT_ITEM_NONE, 1);
        CHECK(kit_call_parse(&pkt, (int)sizeof(pkt)) == KIT_ITEM_NONE, "an empty call was accepted");
        printf("PASS: short / wrong-type / non-vehicle / out-of-range datagrams are all refused\n");
    }

    /* 2. the server's verdict: alive and allowed spawns; dead and spectating never do */
    {
        PlayerState *p = reset_player();
        int v = kit_call_server_apply(p, KIT_ITEM_BUGGY);
        CHECK(v == VEH_BUGGY, "an alive player's buggy call returned %d", v);
        CHECK(p->in_vehicle && p->vehicle_type == VEH_BUGGY, "the caller was not seated in the buggy");
        CHECK(count_active_buggies() == 1, "%d buggies in the world after one call", count_active_buggies());
        CHECK(kit_call_server_apply(p, KIT_ITEM_ROCKET_CAR) == VEH_NONE, "a second call ignored the cooldown");
        CHECK(count_active_buggies() == 1, "a refused call still spawned a buggy");
        printf("PASS: an allowed call spawns and seats; a second call on the cooldown does not\n");
    }

    {
        PlayerState *p = reset_player();
        p->state = STATE_DEAD;
        CHECK(kit_call_server_apply(p, KIT_ITEM_BUGGY) == VEH_NONE, "a dead player called a buggy");
        CHECK(count_active_buggies() == 0, "a dead player's call spawned a buggy");

        p = reset_player();
        p->state = STATE_SPECTATOR;
        CHECK(kit_call_server_apply(p, KIT_ITEM_HELI) == VEH_NONE, "a spectator called a helicopter");
        CHECK(!local_state.helicopters[0].active, "a spectator's call spawned a helicopter");

        p = reset_player();
        p->active = 0;
        CHECK(kit_call_server_apply(p, KIT_ITEM_BUGGY) == VEH_NONE, "an inactive slot called a buggy");
        printf("PASS: dead, spectating and inactive callers are refused\n");
    }

    /* 3. a call the server cannot honour is refused rather than recycling someone else's car */
    {
        PlayerState *p = reset_player();
        for (int i = 0; i < MAX_BUGGIES; i++) {
            local_state.buggies[i].active = 1;
            local_state.buggies[i].id = 100 + i;
            local_state.buggies[i].occupant_player_id = 50 + i;   /* somebody else's */
        }
        CHECK(kit_call_server_apply(p, KIT_ITEM_BUGGY) == VEH_NONE, "a call with a full garage was accepted");
        CHECK(!p->in_vehicle, "a refused call still seated the player");
        CHECK(local_state.buggies[0].occupant_player_id == 50, "a refused call stole a parked buggy");
        printf("PASS: a full garage refuses the call and nobody's buggy is recycled\n");
    }

    /* 4. the rocket car and the helicopter go through the same gate as everywhere else */
    {
        PlayerState *p = reset_player();
        CHECK(kit_call_server_apply(p, KIT_ITEM_ROCKET_CAR) == VEH_BUGGY, "the rocket car call was refused");
        int found = 0;
        for (int i = 0; i < MAX_BUGGIES; i++)
            if (local_state.buggies[i].active && local_state.buggies[i].rocket) found = 1;
        CHECK(found, "the rocket car call spawned a plain buggy");

        p = reset_player();
        CHECK(kit_call_server_apply(p, KIT_ITEM_HELI) == VEH_HELICOPTER, "the helicopter call was refused");
        CHECK(p->in_vehicle && p->vehicle_type == VEH_HELICOPTER, "the caller was not seated in the helicopter");
        printf("PASS: rocket car and helicopter spawn through the same verb local play uses\n");
    }

    if (fails) { printf("kit_call_test: %d FAILED\n", fails); return 1; }
    printf("kit_call_test: all checks passed\n");
    return 0;
}
