/* wow_ticket_test.c -- IDUNA's ticket, end to end: its byte layout, the MAC, the expiry, and the
 * server's AUTH path with a real verifier installed. make test-wow-ticket.
 * The ticket format is IDUNA's (internal/http/handlers/shankpit_ticket.go), not a local invention. */
#include "wow_ticket.h"
#include "wow_wire.h"
#include "wow_server.h"
#include <stdio.h>
#include <string.h>

/* the issuer's 36 bytes and the wire's ticket field are the same bytes; fail the build if they drift */
typedef char wow_ticket_matches_wire[(WOW_TICKET_BYTES == WOW_WIRE_TICKET_BYTES) ? 1 : -1];

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("ticket: FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static const uint8_t secret[] = "shankpit-test-secret";
static const uint8_t other_secret[] = "some-other-secret";
static const uint8_t pid[16] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x00};
#define UUID_TEXT "11223344-5566-7788-99aa-bbccddeeff00"
#define NOW 1000u

static int ticket_auth(void *ctx, const unsigned char *ticket, int len, char *out, int cap) {
    (void)ctx;
    if (len != WOW_TICKET_BYTES) return 0;
    return wow_ticket_verify(secret, (int)sizeof secret - 1, ticket, NOW, out, cap);
}

static int auth_frame(const uint8_t *t, uint8_t *out, int cap) {
    WowAuthFrame f;
    memcpy(f.ticket, t, WOW_TICKET_BYTES);
    return wow_auth_encode(&f, out, cap);
}

int main(void) {
    uint8_t t[WOW_TICKET_BYTES], bad[WOW_TICKET_BYTES];
    char who[64];
    int n;

    CHECK(WOW_WIRE_AUTH_BYTES == 38, "AUTH is %d bytes, not 2 + 36", WOW_WIRE_AUTH_BYTES);

    /* a good ticket, and the identity it carries */
    wow_ticket_make(secret, (int)sizeof secret - 1, pid, NOW + 300u, t);
    CHECK(wow_ticket_verify(secret, (int)sizeof secret - 1, t, NOW, who, sizeof who) == 1,
          "a ticket its own secret signed was refused");
    CHECK(strcmp(who, UUID_TEXT) == 0, "player id came out as \"%s\"", who);

    /* the uuid text is formatted the way IDUNA returns it, independent of the ticket */
    wow_ticket_uuid_text(pid, who, sizeof who);
    CHECK(strcmp(who, UUID_TEXT) == 0, "uuid text is \"%s\"", who);

    /* expiry: > now accepted, == now refused, < now refused */
    wow_ticket_make(secret, (int)sizeof secret - 1, pid, NOW + 1u, t);
    CHECK(wow_ticket_verify(secret, (int)sizeof secret - 1, t, NOW, who, sizeof who) == 1, "expiry now+1 refused");
    wow_ticket_make(secret, (int)sizeof secret - 1, pid, NOW, t);
    CHECK(wow_ticket_verify(secret, (int)sizeof secret - 1, t, NOW, who, sizeof who) == 0, "a ticket expiring now was accepted");
    wow_ticket_make(secret, (int)sizeof secret - 1, pid, NOW - 1u, t);
    CHECK(wow_ticket_verify(secret, (int)sizeof secret - 1, t, NOW, who, sizeof who) == 0, "an expired ticket was accepted");

    /* a ticket signed by another secret, a tampered id, a tampered MAC */
    wow_ticket_make(other_secret, (int)sizeof other_secret - 1, pid, NOW + 300u, t);
    CHECK(wow_ticket_verify(secret, (int)sizeof secret - 1, t, NOW, who, sizeof who) == 0, "another secret's ticket was accepted");
    wow_ticket_make(secret, (int)sizeof secret - 1, pid, NOW + 300u, t);
    memcpy(bad, t, sizeof t); bad[0] ^= 0x01;
    CHECK(wow_ticket_verify(secret, (int)sizeof secret - 1, bad, NOW, who, sizeof who) == 0, "a tampered player id was accepted");
    memcpy(bad, t, sizeof t); bad[WOW_TICKET_PAYLOAD] ^= 0x80;
    CHECK(wow_ticket_verify(secret, (int)sizeof secret - 1, bad, NOW, who, sizeof who) == 0, "a tampered MAC was accepted");

    /* malformed arguments are -1, not a silent refusal */
    CHECK(wow_ticket_verify(secret, 0, t, NOW, who, sizeof who) == -1, "an empty secret was not malformed");
    CHECK(wow_ticket_verify(NULL, 20, t, NOW, who, sizeof who) == -1, "a null secret was not malformed");
    CHECK(wow_ticket_verify(secret, (int)sizeof secret - 1, NULL, NOW, who, sizeof who) == -1, "a null ticket was not malformed");
    CHECK(wow_ticket_verify(secret, (int)sizeof secret - 1, t, NOW, who, 8) == -1, "a short id buffer was not malformed");

    /* ---- the wire and the server ---- */
    {
        WowServer s;
        uint8_t frame[64], ack[WOW_WIRE_ACK_BYTES];
        wow_server_begin(&s);
        wow_ticket_make(secret, (int)sizeof secret - 1, pid, NOW + 300u, t);
        n = auth_frame(t, frame, sizeof frame);
        CHECK(n == WOW_WIRE_AUTH_BYTES, "AUTH encoded to %d bytes", n);
        CHECK(wow_server_rx(&s, 1, frame, n, ack, sizeof ack) == -2 && s.c[1].authed == 0,
              "AUTH was accepted with no verifier installed");

        s.auth = ticket_auth;
        CHECK(wow_server_rx(&s, 1, frame, n, ack, sizeof ack) == 1 && s.c[1].authed == 1 &&
              strcmp(s.c[1].player_id, UUID_TEXT) == 0 && ack[1] == WOW_OP_ACK,
              "a real ticket did not authenticate the client / bind its player id");

        wow_ticket_make(other_secret, (int)sizeof other_secret - 1, pid, NOW + 300u, t);
        n = auth_frame(t, frame, sizeof frame);
        CHECK(wow_server_rx(&s, 2, frame, n, ack, sizeof ack) == -2 && s.c[2].authed == 0,
              "a ticket signed by another secret authenticated a client");

        /* a ticket that expires before now leaves the client unauthenticated, even after a good one */
        wow_ticket_make(secret, (int)sizeof secret - 1, pid, NOW - 5u, t);
        n = auth_frame(t, frame, sizeof frame);
        CHECK(wow_server_rx(&s, 1, frame, n, ack, sizeof ack) == -2 && s.c[1].authed == 0,
              "an expired ticket re-authenticated a client");

        /* unauthenticated intents are still refused */
        {
            WowTargets tx;
            uint8_t b[16];
            int m;
            wow_targets_begin(&tx);
            m = wow_targets_intent(&tx, 0, 7, b, sizeof b);
            CHECK(wow_server_rx(&s, 3, b, m, ack, sizeof ack) == -2, "an unauthenticated target was applied");
        }
    }

    if (fails) { printf("ticket: %d FAILED\n", fails); return 1; }
    printf("ticket: all checks passed\n");
    return 0;
}
