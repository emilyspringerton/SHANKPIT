/* wow_e2e_test.c -- the macro wire over a REAL UDP socket, against the REAL server binary.
 *
 * Every other wire test drives wow_server_rx() in-process, which proves the state machine agrees with
 * itself but not that a server, a socket, a ticket and a client do. This one forks `bin/shank_server`,
 * sends the same datagrams the lobby sends, mints an IDUNA-shaped ticket with the same shared secret the
 * server is given, and asserts the ACKs that actually come back over the loopback interface.
 *
 * Headless on purpose: the server needs no display, and SHANKPIT_BAKED_LEVELS=1 keeps it from trying to
 * fetch levels from IDUNA during the test. `--beacon-port 0` keeps this instance off the LAN's radar and
 * `--fast-forward` drops the 60 Hz pacing.
 *
 * What is asserted: a ticket signed with the WRONG secret gets no ACK (fail closed); a real ticket is
 * ACKed and binds the player; a target intent is ACKed; the SAME intent re-sent is re-ACKed (the
 * idempotence that makes client retry work); an ACK retires the client's pending frame; and a cast is
 * ACKed.
 *
 * make test-wow-e2e [E2E_PORT=6979]
 */
#define _POSIX_C_SOURCE 200809L
#include "wow_client.h"
#include "wow_server.h"
#include "wow_ticket.h"
/* The datagram envelope lives in packages/common/protocol.h. Spelled relatively on purpose: this file
   sits in packages/protocol/, whose own include dir comes first on the command line, so a bare
   "protocol.h" would resolve to whichever header that directory happens to hold. */
#include "../common/protocol.h"
#include "wow_weapons.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>   /* struct timeval, for SO_RCVTIMEO */
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("e2e: FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

#define E2E_SECRET       "shankpit-e2e-secret"
#define E2E_OTHER_SECRET "not-the-servers-secret"

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static pid_t spawn_server(int port) {
    pid_t pid = fork();
    if (pid != 0) return pid;
    /* Child: a private, headless instance. Never the shared service. */
    setenv("SHANKPIT_BAKED_LEVELS", "1", 1);
    setenv("SHANKPIT_TICKET_SECRET", E2E_SECRET, 1);
    /* Best effort -- a failed redirect loses the server's log but must not stop the test. The return
       value is checked rather than ignored because freopen is declared warn_unused_result. */
    if (!freopen("/tmp/shankpit_wow_e2e_server.log", "w", stdout)) { /* keep the inherited stdout */ }
    if (!freopen("/tmp/shankpit_wow_e2e_server.log", "a", stderr)) { /* keep the inherited stderr */ }
    {
        char portbuf[16];
        snprintf(portbuf, sizeof portbuf, "%d", port);
        execl("bin/shank_server", "shank_server", "--port", portbuf, "--beacon-port", "0",
              "--fast-forward", (char *)NULL);
    }
    _exit(127);
}

static int send_frame(int s, const struct sockaddr_in *to, const uint8_t *bytes, int len) {
    NetWowWire pkt;
    if (len <= 0 || len > (int)sizeof pkt.bytes) return -1;
    memset(&pkt, 0, sizeof pkt);
    pkt.hdr.type = PACKET_WOW_WIRE;
    pkt.len = (unsigned char)len;
    memcpy(pkt.bytes, bytes, (size_t)len);
    return (int)sendto(s, (const char *)&pkt, sizeof pkt, 0, (const struct sockaddr *)to, sizeof *to);
}

/* Wait for a WOW_WIRE datagram; everything else (WELCOME, snapshots) is ignored, because a real client
   receives those too and must not confuse them with an ACK. 1 got one, 0 timed out. */
static int recv_ack(int s, uint8_t *ack, int timeout_ms) {
    long long deadline = now_ms() + timeout_ms;
    for (;;) {
        unsigned char buf[65536];
        ssize_t len = recv(s, (char *)buf, sizeof buf, 0);
        if (len >= (ssize_t)(sizeof(NetHeader) + 1)) {
            NetHeader *h = (NetHeader *)buf;
            if (h->type == PACKET_WOW_WIRE) {
                NetWowWire *w = (NetWowWire *)buf;
                if (w->len == WOW_WIRE_ACK_BYTES &&
                    (int)(sizeof(NetHeader) + 1 + w->len) <= (int)len) {
                    memcpy(ack, w->bytes, WOW_WIRE_ACK_BYTES);
                    return 1;
                }
            }
        }
        if (now_ms() >= deadline) return 0;
    }
}

static void send_connect(int s, const struct sockaddr_in *to);   /* defined just below, used by the wait */

/* CONNECT is the only packet allowed to mint a client slot, so it must actually land: a single send
 * races the server's own startup (bind happens after BOOTY/terrain setup) and a lost CONNECT makes every
 * later packet "unknown sender", which looks exactly like a broken wire. Retry until WELCOME, the same
 * thing the lobby's reconnect path does. 1 welcomed, 0 never. */
static int wait_welcome(int s, const struct sockaddr_in *to, int timeout_ms) {
    long long deadline = now_ms() + timeout_ms;
    while (now_ms() < deadline) {
        long long step = now_ms() + 250;
        send_connect(s, to);
        while (now_ms() < step) {
            unsigned char buf[65536];
            ssize_t len = recv(s, (char *)buf, sizeof buf, 0);
            if (len >= (ssize_t)sizeof(NetHeader) && ((NetHeader *)buf)->type == PACKET_WELCOME) return 1;
        }
    }
    return 0;
}

static void send_connect(int s, const struct sockaddr_in *to) {
    unsigned char buf[sizeof(NetHeader) + 1];
    NetHeader *h = (NetHeader *)buf;
    memset(buf, 0, sizeof buf);
    h->type = PACKET_CONNECT;
    buf[sizeof(NetHeader)] = (unsigned char)MODE_DEATHMATCH;
    sendto(s, (const char *)buf, sizeof buf, 0, (const struct sockaddr *)to, sizeof *to);
}

int main(int argc, char **argv) {
    int port = (argc > 1) ? atoi(argv[1]) : 6979;
    setvbuf(stdout, NULL, _IONBF, 0);   /* progress must be visible even if the test is killed */
    int s, n, got;
    pid_t pid;
    int sent_ok = 0;
    struct sockaddr_in to;
    uint8_t ticket[WOW_TICKET_BYTES], bad_ticket[WOW_TICKET_BYTES];
    uint8_t frame[WOW_WIRE_AUTH_BYTES], ack[WOW_WIRE_ACK_BYTES];
    uint8_t player_id[16];
    WowAck a;
    WowClient c;
    long long deadline;
    int i;

    for (i = 0; i < 16; i++) player_id[i] = (uint8_t)(0x40 + i);

    pid = spawn_server(port);
    printf("e2e: server pid=%d port=%d\n", (int)pid, port);
    if (pid < 0) {
        printf("e2e: could not fork the server\n");
        return 1;
    }

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        printf("e2e: could not open a UDP socket\n");
        kill(pid, SIGKILL);
        return 1;
    }
    {
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;                 /* so recv_ack can poll the deadline */
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    }
    memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_port = htons((unsigned short)port);
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    /* A CONNECT is what mints a client slot on this server; without one every other packet is dropped
       as an unknown sender (UNKNOWN_SENDER_DROPPED), which is itself worth exercising here. */
    CHECK(wait_welcome(s, &to, 8000), "the server never WELCOMEd a CONNECT on 127.0.0.1:%d", port);
    printf("e2e: welcomed by 127.0.0.1:%d\n", port);

    /* ---- fail closed: a ticket signed with another secret must NOT be acknowledged ---- */
    wow_ticket_make((const uint8_t *)E2E_OTHER_SECRET, (int)strlen(E2E_OTHER_SECRET),
                    player_id, (uint32_t)(time(NULL) + 300), bad_ticket);
    n = wow_client_auth(bad_ticket, frame, sizeof frame);
    CHECK(n == WOW_WIRE_AUTH_BYTES, "a forged AUTH encoded to %d bytes", n);
    sent_ok = send_frame(s, &to, frame, n);
    printf("e2e: sent forged AUTH (%d bytes, sendto=%d)\n", n, sent_ok);
    CHECK(sent_ok > 0, "the forged AUTH was not sent");
    CHECK(recv_ack(s, ack, 400) == 0, "a ticket signed by another secret was ACKed");

    /* ---- the real ticket: retry AUTH until the server has finished starting ---- */
    wow_ticket_make((const uint8_t *)E2E_SECRET, (int)strlen(E2E_SECRET),
                    player_id, (uint32_t)(time(NULL) + 300), ticket);
    n = wow_client_auth(ticket, frame, sizeof frame);
    CHECK(n == WOW_WIRE_AUTH_BYTES, "AUTH encoded to %d bytes", n);
    deadline = now_ms() + 8000;
    got = 0;
    while (now_ms() < deadline) {
        int sr = send_frame(s, &to, frame, n);
        if (recv_ack(s, ack, 250)) { got = 1; break; }
        printf("e2e: AUTH no ACK yet (sendto=%d, %lld ms left)\n", sr, deadline - now_ms());
    }
    CHECK(got, "the server never ACKed a valid IDUNA ticket (see /tmp/shankpit_wow_e2e_server.log)");
    if (got) {
        CHECK(wow_ack_decode(ack, WOW_WIRE_ACK_BYTES, &a) == WOW_WIRE_ACK_BYTES && a.kind == WOW_OP_AUTH,
              "the first ACK was not an AUTH ACK");
    }

    /* ---- target: ACKed, re-sent -> re-ACKed (idempotent), ACK retires the pending frame ---- */
    wow_client_begin(&c);
    n = wow_client_target(&c, 0, 1, frame, sizeof frame);
    CHECK(n == WOW_WIRE_INTENT_BYTES, "a target change encoded to %d bytes", n);
    CHECK(send_frame(s, &to, frame, n) > 0, "the target intent was not sent");
    CHECK(recv_ack(s, ack, 1500) == 1 &&
          wow_ack_decode(ack, WOW_WIRE_ACK_BYTES, &a) == WOW_WIRE_ACK_BYTES &&
          a.kind == WOW_OP_SET_TARGET, "a target change was not ACKed over the socket");
    CHECK(wow_client_track(&c, frame, n, now_ms()) == 1, "the sent intent was not tracked");
    CHECK(send_frame(s, &to, frame, n) > 0, "the resend was not sent");
    CHECK(recv_ack(s, ack, 1500) == 1 &&
          wow_ack_decode(ack, WOW_WIRE_ACK_BYTES, &a) == WOW_WIRE_ACK_BYTES &&
          a.kind == WOW_OP_SET_TARGET,
          "a re-sent intent was not re-ACKed (the retry would strand itself)");
    CHECK(wow_client_ack(&c, ack, WOW_WIRE_ACK_BYTES) == 1 && wow_client_pending(&c) == 0,
          "the ACK did not retire the pending frame");
    CHECK(wow_client_retry(&c, now_ms() + 10000, frame, sizeof frame) == 0,
          "a retired frame was still due for a resend");

    /* ---- cast: the weapon id the engine actually has, at the target slot ---- */
    n = wow_client_cast(&c, wow_weapon_from_name("magnum"), WOW_TOKEN_TARGET, frame, sizeof frame);
    CHECK(n == WOW_WIRE_CAST_BYTES, "a cast encoded to %d bytes", n);
    CHECK(send_frame(s, &to, frame, n) > 0, "the cast was not sent");
    CHECK(recv_ack(s, ack, 1500) == 1 &&
          wow_ack_decode(ack, WOW_WIRE_ACK_BYTES, &a) == WOW_WIRE_ACK_BYTES &&
          a.kind == WOW_OP_CAST, "a cast was not ACKed over the socket");

    /* ---- an unacknowledged frame really does become due, on the real clock ---- */
    {
        WowClient u;
        uint8_t f2[WOW_WIRE_INTENT_BYTES];
        int m;
        wow_client_begin(&u);
        wow_client_set_preset(&u, 1);                 /* 500 ms period */
        m = wow_client_target(&u, 1, 2, f2, sizeof f2);
        CHECK(m == WOW_WIRE_INTENT_BYTES && wow_client_track(&u, f2, m, now_ms()) == 1,
              "a frame was not tracked for the retry check");
        CHECK(wow_client_retry(&u, now_ms(), f2, sizeof f2) == 0, "a frame was due immediately");
        CHECK(wow_client_retry(&u, now_ms() + (uint32_t)wow_ack_ms(1), f2, sizeof f2) == m,
              "a frame was not due after the ack period");
    }

    close(s);
    kill(pid, SIGTERM);
    {
        int status = 0, waited = 0;
        for (i = 0; i < 100; i++) {
            if (waitpid(pid, &status, WNOHANG) == pid) { waited = 1; break; }
            sleep_ms(20);
        }
        if (!waited) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
        }
    }

    if (fails) { printf("e2e: %d FAILED\n", fails); return 1; }
    printf("e2e: all checks passed (real UDP, real server, port %d)\n", port);
    return 0;
}
