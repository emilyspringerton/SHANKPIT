/* horse_interact.h -- what a client (or a mod running on one) may ask a horse to do, and the server's
 * verdict on that request. One datagram, one decision, and the decision lives here rather than inline
 * in apps/server/src/main.c precisely because it is the trust boundary: this is the only place a
 * remote peer's word about a horse is turned into a change in the authoritative world.
 *
 * WHY THESE FOUR AND NOT SPAWN. Mount and dismount are the player's own affordances (the E key).
 * Set-goal and feed are what a MOD asks for -- lead this horse home, send it grazing, make it bolt,
 * give it a carrot -- and they are the reason the request channel carries a payload at all. Spawning
 * is deliberately NOT here: creating a horse is not an interaction with an existing actor, and a
 * client that could mint animals could fill the map with them. Spawning stays authority-side
 * (horse_api_spawn, horse_api.h), so a host-side mod can do it and a remote client cannot.
 *
 * THE RULES, and they are the same ones the local E key already used:
 *   - the caller is live and welcomed (the server checks welcomed before calling this);
 *   - the horse exists and is in the caller's OWN scene (a horse in another town is not described
 *     to you, so it cannot be requested either);
 *   - mount / set-goal / feed require the caller within HORSE_INTERACT_REACH_M (5 m, 3D -- the same
 *     radius horse_find_nearby gives the local client, so a remote player has no longer an arm than
 *     a local one);
 *   - mount requires the horse free; set-goal is refused while SOMEONE ELSE is riding it (the rider
 *     is the one in charge of the animal); feed is not, because a carrot from a passer-by is a
 *     normal thing and the horse's own tick still decides what to do with it;
 *   - dismount requires the caller to BE the rider, and needs no reach (you are on it).
 *
 * Refusals are RETURNED, not sent: the wire carries no acknowledgement by design (see horse_sync.h),
 * and the next PACKET_HORSE_STATE shows the truth. A caller (the server's log, a test) gets the
 * reason as a negative code so "refused" is never an unexplained nothing.
 *
 * Include order: this pulls in local_game.h through horse_api.h; include it AFTER local_game.h, and
 * never before it. The send helper compiles on Windows too -- protocol.h already puts winsock2 ahead
 * of the SDL/windows headers, which is the order winsock requires.
 */
#ifndef HORSE_INTERACT_H
#define HORSE_INTERACT_H

#include <string.h>

#include "horse_api.h"   /* local_game.h + horse_reflux + horse_goal_change/horse_spawn_defaults */
#include "protocol.h"    /* NetHorseInteract, HORSE_INTERACT_*, <netinet/in.h> or winsock2 */

/* The local E key's own reach (apps/lobby's own horse_find_nearby call), restated here as the wire's
 * reach so a remote request is judged by exactly the same arm. */
#define HORSE_INTERACT_REACH_M 5.0f

/* Verdicts. Anything > 0 is accepted; every refusal is its own negative value so a log line or a
 * test can say WHY, instead of "nothing happened". */
#define HORSE_INTERACT_OK                  1
#define HORSE_INTERACT_REFUSED_STATE      -1  /* no such caller / bad slot or action */
#define HORSE_INTERACT_REFUSED_NOTHERE    -2  /* no such horse, or it is in another scene */
#define HORSE_INTERACT_REFUSED_OUT_OF_REACH -3
#define HORSE_INTERACT_REFUSED_BUSY       -4  /* someone is already riding it */
#define HORSE_INTERACT_REFUSED_BAD_GOAL   -5  /* not a HORSE_GOAL_* */
#define HORSE_INTERACT_REFUSED_NO_CARROT  -6  /* nothing to give, or the bar is already full */
#define HORSE_INTERACT_REFUSED_NOT_RIDER  -7  /* a dismount from someone who is not on it */

/* Build one request. Kept beside the policy so the byte layout and the meaning of `goal` live in one
 * file, and so a test can assert the packet without a socket. */
static inline void horse_interact_build(NetHorseInteract *pkt, int client_id, int slot, int action, int goal) {
    if (!pkt) return;
    memset(pkt, 0, sizeof(*pkt));
    pkt->hdr.type = PACKET_HORSE_INTERACT;
    pkt->hdr.client_id = (unsigned char)((client_id < 0 ? 0 : client_id) & 0xff);
    pkt->slot = (unsigned char)((slot < 0 ? 0 : slot) & 0xff);
    pkt->action = (unsigned char)((action < 0 ? 0 : action) & 0xff);
    pkt->goal = (unsigned char)((goal < 0 ? 0 : goal) & 0xff);
}

/* Send one request. Fire and forget, exactly like a kit call: the answer is the horse moving in the
 * next state packet, and a lost datagram is a key press that did not happen. Returns 1 when the
 * datagram was handed to the socket. */
static inline int horse_interact_send(int fd, const struct sockaddr_in *to, int client_id,
                                      int slot, int action, int goal) {
    if (fd < 0 || !to) return 0;
    NetHorseInteract pkt;
    horse_interact_build(&pkt, client_id, slot, action, goal);
    return sendto(fd, (const char *)&pkt, (int)sizeof(pkt), 0,
                  (const struct sockaddr *)to, (int)sizeof(*to)) == (int)sizeof(pkt);
}

/* The server's verdict on one request. Pure: reads the world, changes it only through the horse's
 * own verbs (horse_try_enter / horse_try_exit / horse_goal_change / horse_api_feed), so a remote
 * request and a local key press cannot diverge -- they call the same code. */
static inline int horse_interact_apply(ServerState *st, PlayerState *caller, int slot, int action,
                                       int goal, unsigned int now_ms) {
    if (!st || !caller || !caller->active) return HORSE_INTERACT_REFUSED_STATE;
    if (slot < 0 || slot >= MAX_HORSES) return HORSE_INTERACT_REFUSED_STATE;
    if (action < 0 || action >= HORSE_INTERACT_ACTION_COUNT) return HORSE_INTERACT_REFUSED_STATE;

    HorseState *h = &st->horses[slot];
    if (!h->active || h->scene_id != caller->scene_id) return HORSE_INTERACT_REFUSED_NOTHERE;

    /* You are sitting on it, so there is no reach to check. */
    if (action == HORSE_INTERACT_DISMOUNT) {
        if (h->occupant_player_id != caller->id) return HORSE_INTERACT_REFUSED_NOT_RIDER;
        return horse_try_exit(caller, h) ? HORSE_INTERACT_OK : HORSE_INTERACT_REFUSED_STATE;
    }

    float dx = h->x - caller->x, dy = h->y - caller->y, dz = h->z - caller->z;
    if (dx * dx + dy * dy + dz * dz > HORSE_INTERACT_REACH_M * HORSE_INTERACT_REACH_M)
        return HORSE_INTERACT_REFUSED_OUT_OF_REACH;

    switch (action) {
        case HORSE_INTERACT_MOUNT:
            if (h->occupant_player_id >= 0) return HORSE_INTERACT_REFUSED_BUSY;
            return horse_try_enter(caller, h) ? HORSE_INTERACT_OK : HORSE_INTERACT_REFUSED_STATE;

        case HORSE_INTERACT_SET_GOAL:
            if (goal < 0 || goal >= HORSE_GOAL_COUNT) return HORSE_INTERACT_REFUSED_BAD_GOAL;
            if (h->occupant_player_id >= 0 && h->occupant_player_id != caller->id)
                return HORSE_INTERACT_REFUSED_BUSY;
            horse_goal_change(slot, h, goal, now_ms);
            return HORSE_INTERACT_OK;

        case HORSE_INTERACT_FEED:
            /* horse_api_feed has one refusal for two honest cases: the saddlebag is empty, or the
               bar is already full and a carrot would be wasted. Both mean "not taken". */
            return horse_api_feed(st, slot, now_ms) ? HORSE_INTERACT_OK
                                                    : HORSE_INTERACT_REFUSED_NO_CARROT;

        default:
            return HORSE_INTERACT_REFUSED_STATE;
    }
}

#endif /* HORSE_INTERACT_H */
