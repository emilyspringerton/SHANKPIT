/* apps/tests/citizen_wire_client.c -- a REAL client for the REAL server, over real UDP, proving that
 * a MODE_ZOMBIES citizen's REFLUX actions reach the wire.
 *
 * Why this file exists: `make test-citizen-actor` proves the citizen family is DRained, batched and
 * re-dispatched correctly, but it does so in one address space, which by construction cannot catch a
 * wire bug -- a wrong struct size, a byte swapped into the wrong field, a packet the server never
 * actually sends, a broadcast that only ever reaches the peer it came from. The horse relay had this
 * same gap once and `make test-horse-wire` closed it. This is the citizens' half.
 *
 * What it asserts, in the order a player would experience it:
 *   1. PACKET_CONNECT is answered (a welcomed slot), and snapshots are streaming to this address --
 *      the citizen's BODY is what the snapshot carries, which is why only the events need a packet.
 *   2. PACKET_CITIZEN_EVENTS arrives carrying the citizen family, and every row in it is inside the
 *      family (reflux_action_is_citizen) -- a relay is untrusted input and this is the peer's whole
 *      defence.
 *   3. The rows are the ones the fight produced: a weapon drawn and a shot fired for the same
 *      citizen (the pair the server plants with SHANKPIT_TEST_ZOMBIE_PAIR), and -- because the fight
 *      plan is what a citizen does when a claw is in reach -- at least one dodge, cover or retreat
 *      row, which cannot be produced by any other code path.
 *   4. Applying the rows through citizen_sync_apply_events grows THIS peer's own REFLUX log by
 *      exactly the number of rows accepted, i.e. a mod polling here sees what the server saw.
 *
 * Every step prints what it saw; a timeout says which of them never happened. Exit 0 only when every
 * step did. Build/run: `make test-citizen-wire` (which boots the server itself); host and port are
 * argv[1]/argv[2]. */
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
#include "citizen_sync.h"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

/* Deadline, not a schedule: the pair the server plants starts fighting on its first tick, so a
 * healthy wire answers inside a few hundred milliseconds; the rest is slack for a loaded box. */
#define WIRE_DEADLINE_MS 10000

static const char *citizen_event_name(int t) {
    switch (t) {
        case REFLUX_ACTION_CITIZEN_JUMPED: return "jumped";
        case REFLUX_ACTION_CITIZEN_SLID: return "slid";
        case REFLUX_ACTION_CITIZEN_SLIDE_JUMPED: return "slide-jumped";
        case REFLUX_ACTION_CITIZEN_WIELDED: return "wielded";
        case REFLUX_ACTION_CITIZEN_FIRED: return "fired";
        case REFLUX_ACTION_CITIZEN_DODGED: return "dodged";
        case REFLUX_ACTION_CITIZEN_TOOK_COVER: return "took-cover";
        case REFLUX_ACTION_CITIZEN_RETREATED: return "retreated";
        default: return "?";
    }
}

static int send_to_server(int fd, const struct sockaddr_in *srv, const void *pkt, int len) {
    ssize_t n = sendto(fd, pkt, (size_t)len, 0, (const struct sockaddr *)srv, sizeof(*srv));
    return n == (ssize_t)len;
}

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
    int port = (argc > 2) ? atoi(argv[2]) : 6980;

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof srv);
    srv.sin_family = AF_INET;
    srv.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &srv.sin_addr) != 1) { printf("bad host %s\n", host); return 2; }

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return 2; }

    unsigned char hello[sizeof(NetHeader) + 1];
    memset(hello, 0, sizeof hello);
    NetHeader *hh = (NetHeader *)hello;
    hh->type = PACKET_CONNECT;
    hello[sizeof(NetHeader)] = (unsigned char)(MODE_ZOMBIES & 0xff);

    unsigned char buf[4096];
    int my_id = -1;
    int snapshot_seen = 0, events_seen = 0, rows_seen = 0, rows_applied = 0, foreign_rows = 0;
    int saw_fired = -1, saw_wielded = -1;      /* player ids, -1 = never */
    int plan_rows = 0;
    int last_connect_ms = -10000;
    unsigned int log_before = 0;
    int log_before_set = 0;

    for (int spins = 0; spins < WIRE_DEADLINE_MS / 50; spins++) {
        int elapsed = spins * 50;
        /* Keep asking until seated: the server is booted by the make target and we may be faster. */
        if (my_id < 0 && elapsed - last_connect_ms >= 250) {
            if (!send_to_server(fd, &srv, hello, (int)sizeof hello)) { printf("connect send failed\n"); return 2; }
            last_connect_ms = elapsed;
        }
        int n = recv_from_server(fd, &srv, buf, (int)sizeof buf, 50);
        if (n > 0) {
            NetHeader *h = (NetHeader *)buf;
            if (h->type == PACKET_WELCOME && n >= (int)(sizeof(NetHeader) + 1)) {
                if (my_id < 0) {
                    my_id = (int)h->client_id;
                    printf("wire: welcomed as client %d, mode %u\n", my_id, (unsigned)buf[sizeof(NetHeader)]);
                }
            } else if (h->type == PACKET_SNAPSHOT) {
                snapshot_seen = 1;
            } else if (h->type == PACKET_CITIZEN_EVENTS && n >= (int)(sizeof(NetHeader) + 4)) {
                NetCitizenEvents *ev = (NetCitizenEvents *)buf;
                int held = (n - (int)(sizeof(NetHeader) + 4)) / (int)sizeof(NetCitizenEvent);
                int count = ev->count < held ? ev->count : held;
                if (count > 0) events_seen = 1;
                for (int i = 0; i < count; i++) {
                    NetCitizenEvent *e = &ev->e[i];
                    /* the packet is untrusted; the family predicate is the peer's whole defence */
                    if (!reflux_action_is_citizen(e->type)) { foreign_rows++; continue; }
                    if (!log_before_set) { log_before = (unsigned int)reflux_host_total_dispatched(); log_before_set = 1; }
                    rows_seen++;
                    if (e->type == REFLUX_ACTION_CITIZEN_FIRED) saw_fired = e->a;
                    if (e->type == REFLUX_ACTION_CITIZEN_WIELDED) saw_wielded = e->a;
                    if (e->type == REFLUX_ACTION_CITIZEN_DODGED ||
                        e->type == REFLUX_ACTION_CITIZEN_TOOK_COVER ||
                        e->type == REFLUX_ACTION_CITIZEN_RETREATED) plan_rows++;
                    printf("wire: citizen %d %s (b=%d c=%d)\n", e->a, citizen_event_name(e->type), e->b, e->c);
                }
                /* 4. the rows land in THIS process's own log, exactly as a client-side mod wants */
                rows_applied += citizen_sync_apply_events(ev->e, count);
            }
        }
        /* A drawn weapon and a shot at the planted pair are the two rows that prove the fight, not
         * just the toolchain, reached the wire; the fight plan is what a citizen does at claw range. */
        if (saw_wielded >= 0 && saw_fired >= 0 && plan_rows > 0 && rows_seen >= 6) break;
    }

    CHECK(my_id > 0, "never got a PACKET_WELCOME -- the server did not seat this client");
    CHECK(snapshot_seen, "no PACKET_SNAPSHOT arrived -- the citizen's body is not streaming either");
    CHECK(events_seen, "no PACKET_CITIZEN_EVENTS arrived at all -- the relay is not on the wire");
    CHECK(foreign_rows == 0, "%d row(s) outside the citizen family were forwarded, not refused", foreign_rows);
    CHECK(rows_seen > 0, "the packet carried no rows");
    CHECK(saw_wielded >= 0, "no CITIZEN_WIELDED row reached the wire -- the citizen never drew on the zombie");
    CHECK(saw_fired >= 0, "no CITIZEN_FIRED row reached the wire -- the citizen never shot");
    CHECK(saw_wielded == saw_fired, "wielded for citizen %d but fired for %d -- the rows are not one body",
          saw_wielded, saw_fired);
    CHECK(plan_rows > 0, "no dodge/cover/retreat row reached the wire -- the fight plan never ran");
    CHECK(rows_applied == rows_seen, "%d of %d rows were applied to this peer's REFLUX log",
          rows_applied, rows_seen);
    CHECK((unsigned int)reflux_host_total_dispatched() == log_before + (unsigned int)rows_applied,
          "applying %d row(s) grew the local log to %d, wanted %u", rows_applied,
          reflux_host_total_dispatched(), log_before + (unsigned int)rows_applied);

    close(fd);
    if (fails) { printf("citizen_wire_client: %d FAILURE(S)\n", fails); return 1; }
    printf("citizen_wire_client: ok -- real server, real UDP: connected, snapshots streaming, and the "
           "citizen's own REFLUX actions (%d rows) arrived on PACKET_CITIZEN_EVENTS, family-checked, "
           "and re-dispatched into this process's log\n", rows_seen);
    return 0;
}
