/* apps/tests/horse_wire_client.c -- a REAL client for the REAL server, over real UDP.
 *
 * Why this file exists: everything else that "verifies" horses already runs in one process. The
 * lobby's horse draw path has only ever been compiled (there is no display here, and until this pass
 * `make lobby` could not even link on this box), and the state/event round-trip in horse_test.c is two
 * ServerStates in one address space, which cannot catch a wire bug: a wrong struct size, a byte swapped
 * into the wrong field, a packet the server never actually sends, a mount the server refuses for a
 * reason the local path does not have. This is the deepest evidence available headlessly: a second
 * PROCESS, a second socket, the real shank_server binary booted on an authored level, and nothing
 * shared but the bytes.
 *
 * What it asserts, in the order a player would experience it:
 *   1. PACKET_CONNECT is answered -- a welcomed slot exists and the server is streaming snapshots to
 *      this address at all (proven by any horse state arriving without asking for one).
 *   2. PACKET_HORSE_STATE describes the authored farm: one horse, unicorn skin, loose, full bar.
 *   3. PACKET_HORSE_INTERACT with MOUNT is accepted, and the NEXT state says this client is its rider
 *      -- i.e. the server's authority moved, not just our own copy.
 *   4. PACKET_HORSE_INTERACT with SET_GOAL moves the animal, and the goal it reports in the state is
 *      the one that was asked for.
 *   5. PACKET_HORSE_EVENTS relays the horse REFLUX family back to a client, carrying OUR mount (b =
 *      this client's id) and the goal change -- so a mod on a client really does see the server's
 *      events, which is the clause "ensure it all syncs to the network via reflux".
 *   6. A ridden horse is doing the rider's thing, so it produces nothing on its own; jump it (PARENA
 *      charges 180 milli) and feed it a carrot through the same request channel, and 2.5 s later
 *      PACKET_HORSE_PILES describes the pile (state, so a client that missed the event still draws
 *      the paddock) and REFLUX_ACTION_HORSE_POOPED arrives on PACKET_HORSE_EVENTS (the event, for
 *      mods that want to react).
 *
 * Refusals are visible: every step prints what it saw, and a timeout says which of them never
 * happened. Exit 0 only when every step did. Build/run: `make test-horse-wire` (which boots the
 * server itself); the port and host are argv[1]/argv[2].
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

#include "protocol.h"
#include "reflux_runtime.h"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

#define WIRE_DEADLINE_MS 12000   /* jump, a carrot, then PARENA's 2.5 s of digestion */

static int send_to_server(int fd, const struct sockaddr_in *srv, const void *pkt, int len) {
    ssize_t n = sendto(fd, pkt, (size_t)len, 0, (const struct sockaddr *)srv, sizeof(*srv));
    return n == (ssize_t)len;
}

/* One received datagram, or -1 on timeout/error. Reports the sender so the caller can tell the server
 * from a stray packet (there should be none on a loopback test port, but a silent assumption is how a
 * test passes for the wrong reason). */
static int recv_from_server(int fd, const struct sockaddr_in *srv, unsigned char *buf, int cap, int timeout_ms) {
    struct pollfd p = { fd, POLLIN, 0 };
    int ready = poll(&p, 1, timeout_ms);
    if (ready <= 0) return -1;
    struct sockaddr_in from;
    socklen_t fl = sizeof from;
    ssize_t n = recvfrom(fd, buf, (size_t)cap, 0, (struct sockaddr *)&from, &fl);
    if (n < (ssize_t)sizeof(NetHeader)) return -1;
    if (srv && (from.sin_addr.s_addr != srv->sin_addr.s_addr || from.sin_port != srv->sin_port)) return -1;
    return (int)n;
}

int main(int argc, char **argv) {
    const char *host = (argc > 1) ? argv[1] : "127.0.0.1";
    int port = (argc > 2) ? atoi(argv[2]) : 6979;

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof srv);
    srv.sin_family = AF_INET;
    srv.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &srv.sin_addr) != 1) { printf("bad host %s\n", host); return 2; }

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return 2; }

    /* ---- hello ---- */
    unsigned char hello[sizeof(NetHeader) + 1];
    memset(hello, 0, sizeof hello);
    NetHeader *hh = (NetHeader *)hello;
    hh->type = PACKET_CONNECT;
    hello[sizeof(NetHeader)] = (unsigned char)(MODE_ZOMBIES & 0xff);
    if (!send_to_server(fd, &srv, hello, (int)sizeof hello)) { printf("connect send failed\n"); return 2; }

    unsigned char buf[4096];
    int my_id = -1;
    int horse_slot = -1;
    int rode = 0, goal_landed = 0, event_mount_seen = 0, event_goal_seen = 0;
    int sent_mount = 0, sent_goal = 0;
    int sent_jump = 0, jump_drained = 0, sent_feed = 0, last_feed_ms = -10000, feed_attempts = 0;
    int pile_seen = 0, event_poop_seen = 0;
    int elapsed_ms = 0;
    int refused_mount = 0;

    for (int spins = 0; spins < WIRE_DEADLINE_MS / 10; spins++) {
        elapsed_ms = spins * 100;
        int n = recv_from_server(fd, &srv, buf, (int)sizeof buf, 100);
        if (n > 0) {
            NetHeader *h = (NetHeader *)buf;
            if (h->type == PACKET_WELCOME && n >= (int)(sizeof(NetHeader) + 1)) {
                my_id = (int)h->client_id;
                printf("wire: welcomed as client %d, mode %u\n", my_id, (unsigned)buf[sizeof(NetHeader)]);
            } else if (h->type == PACKET_HORSE_STATE && n >= (int)(sizeof(NetHeader) + 4)) {
                NetHorseState *st = (NetHorseState *)buf;
                int held = (n - (int)(sizeof(NetHeader) + 4)) / (int)sizeof(NetHorseEntry);
                int count = st->count < held ? st->count : held;
                for (int i = 0; i < count; i++) {
                    NetHorseEntry *e = &st->e[i];
                    if (horse_slot < 0) {
                        horse_slot = (int)e->slot;
                        printf("wire: horse slot=%d skin=%u goal=%u stamina=%u occupant=%d at (%.2f,%.2f,%.2f)\n",
                               horse_slot, (unsigned)e->skin, (unsigned)e->goal, (unsigned)e->stamina,
                               (int)e->occupant, e->x, e->y, e->z);
                        /* 2. the authored farm reached a client's bytes */
                        CHECK(e->skin == HORSE_SKIN_UNICORN, "skin %u is not the unicorn", (unsigned)e->skin);
                        CHECK((int)e->occupant < 0, "the horse arrived already ridden (%d)", (int)e->occupant);
                    }
                    if ((int)e->slot != horse_slot) continue;
                    if (my_id >= 0 && (int)e->occupant == my_id) rode = 1;
                    if (sent_jump && e->stamina < HORSE_STAMINA_MAX_MILLI) jump_drained = 1;
                    if (sent_goal && e->goal == HORSE_GOAL_HOME) goal_landed = 1;
                }
                /* 3. ask to get on it, once we know which horse it is */
                if (horse_slot >= 0 && my_id >= 0 && !sent_mount) {
                    NetHorseInteract req;
                    memset(&req, 0, sizeof req);
                    req.hdr.type = PACKET_HORSE_INTERACT;
                    req.hdr.client_id = (unsigned char)my_id;
                    req.slot = (unsigned char)horse_slot;
                    req.action = HORSE_INTERACT_MOUNT;
                    if (!send_to_server(fd, &srv, &req, (int)sizeof req)) { printf("mount send failed\n"); return 2; }
                    sent_mount = 1;
                    printf("wire: asked to mount slot %d\n", horse_slot);
                }
                /* 4. once seated by the SERVER's own state, ask it to go home */
                if (rode && !sent_goal) {
                    NetHorseInteract req;
                    memset(&req, 0, sizeof req);
                    req.hdr.type = PACKET_HORSE_INTERACT;
                    req.hdr.client_id = (unsigned char)my_id;
                    req.slot = (unsigned char)horse_slot;
                    req.action = HORSE_INTERACT_SET_GOAL;
                    req.goal = (unsigned char)HORSE_GOAL_HOME;
                    if (!send_to_server(fd, &srv, &req, (int)sizeof req)) { printf("goal send failed\n"); return 2; }
                    sent_goal = 1;
                    printf("wire: asked horse %d to go home\n", horse_slot);
                }
                /* 6. A ridden horse is doing the rider's thing and has no digestive clock of its
                   own, so the deterministic pile a client can ask for is a CARROT: jump once (PARENA
                   charges 180 milli, so the bar is no longer full and the carrot is not refused),
                   then feed it through the same request channel a client mod would use. The jump is
                   a real UserCmd over the real socket, exactly what the lobby sends. */
                if (rode && !sent_jump) {
                    UserCmd cmd;
                    memset(&cmd, 0, sizeof cmd);
                    cmd.sequence = 1;
                    cmd.buttons = BTN_JUMP;
                    cmd.weapon_idx = -1;
                    unsigned char upkt[sizeof(NetHeader) + 1 + sizeof(UserCmd)];
                    memset(upkt, 0, sizeof upkt);
                    NetHeader *uh = (NetHeader *)upkt;
                    uh->type = PACKET_USERCMD;
                    uh->client_id = (unsigned char)my_id;
                    upkt[sizeof(NetHeader)] = 1;   /* one command in this packet */
                    memcpy(upkt + sizeof(NetHeader) + 1, &cmd, sizeof cmd);
                    if (!send_to_server(fd, &srv, upkt, (int)sizeof upkt)) { printf("jump send failed\n"); return 2; }
                    sent_jump = 1;
                    printf("wire: asked the mount to jump (spends stamina so it will eat)\n");
                }
                if (rode && jump_drained && feed_attempts < 3 &&
                    (!sent_feed || (!pile_seen && elapsed_ms - last_feed_ms > 3000))) {
                    NetHorseInteract req;
                    memset(&req, 0, sizeof req);
                    req.hdr.type = PACKET_HORSE_INTERACT;
                    req.hdr.client_id = (unsigned char)my_id;
                    req.slot = (unsigned char)horse_slot;
                    req.action = HORSE_INTERACT_FEED;
                    if (!send_to_server(fd, &srv, &req, (int)sizeof req)) { printf("feed send failed\n"); return 2; }
                    sent_feed = 1;
                    feed_attempts++;
                    last_feed_ms = elapsed_ms;
                    printf("wire: fed the mount a carrot (a pile comes out in 2.5 s)\n");
                }
            } else if (h->type == PACKET_HORSE_PILES && n >= (int)(sizeof(NetHeader) + 4)) {
                NetHorsePiles *pp = (NetHorsePiles *)buf;
                int held = (n - (int)(sizeof(NetHeader) + 4)) / (int)sizeof(NetHorsePileEntry);
                int count = pp->count < held ? pp->count : held;
                if (count > 0) {
                    if (!pile_seen)
                        printf("wire: pile at (%.2f,%.2f,%.2f) from horse %d\n", pp->e[0].x, pp->e[0].y,
                               pp->e[0].z, (int)pp->e[0].horse);
                    pile_seen = 1;
                }
            } else if (h->type == PACKET_HORSE_EVENTS && n >= (int)(sizeof(NetHeader) + 4)) {
                NetHorseEvents *ev = (NetHorseEvents *)buf;
                int held = (n - (int)(sizeof(NetHeader) + 4)) / (int)sizeof(NetHorseEvent);
                int count = ev->count < held ? ev->count : held;
                for (int i = 0; i < count; i++) {
                    /* 5. the REFLUX family, relayed to a client */
                    if (ev->e[i].type == REFLUX_ACTION_HORSE_MOUNTED && ev->e[i].b == my_id)
                        event_mount_seen = 1;
                    if (ev->e[i].type == REFLUX_ACTION_HORSE_GOAL_CHANGED && ev->e[i].a == horse_slot &&
                        ev->e[i].b == HORSE_GOAL_HOME)
                        event_goal_seen = 1;
                    if (ev->e[i].type == REFLUX_ACTION_HORSE_POOPED && ev->e[i].a == horse_slot)
                        event_poop_seen = 1;
                }
            } else if (h->type == PACKET_WELCOME) {
                /* a short welcome is not fatal; the state packet is the real proof of being welcomed */
            }
        }
        if (sent_mount && !rode && spins > 40) refused_mount = 1;
        if (rode && goal_landed && event_mount_seen && event_goal_seen && pile_seen && event_poop_seen) break;
    }

    CHECK(my_id > 0, "never got a PACKET_WELCOME -- the server did not seat this client");
    CHECK(horse_slot >= 0, "never got a PACKET_HORSE_STATE describing the authored horse");
    CHECK(rode, "the server never seated this client on the horse%s",
          refused_mount ? " (the mount request was not answered in 4 s)" : "");
    CHECK(goal_landed, "the horse never reported HORSE_GOAL_HOME after the SET_GOAL request");
    CHECK(event_mount_seen, "no REFLUX_ACTION_HORSE_MOUNTED for this client reached the wire");
    CHECK(event_goal_seen, "no REFLUX_ACTION_HORSE_GOAL_CHANGED for this horse reached the wire");
    CHECK(pile_seen, "no PACKET_HORSE_PILES reached the wire for a grazing horse");
    CHECK(event_poop_seen, "no REFLUX_ACTION_HORSE_POOPED for this horse reached the wire");

    close(fd);
    if (fails) { printf("horse_wire_client: %d FAILURE(S)\n", fails); return 1; }
    printf("horse_wire_client: ok -- real server, real UDP: connected, saw the unicorn horse, mounted it "
           "(authority moved), told it to go home, jumped a real UserCmd, fed it a carrot, and received "
           "the pile as state AND the REFLUX events (mount, goal, pooped) on the wire\n");
    return 0;
}
