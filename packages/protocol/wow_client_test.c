/* wow_client_test.c -- the client half driven into the real server: change detection, sequence order,
 * cast sequencing, and AUTH. make test-wow-client. */
#include "wow_client.h"
#include "wow_server.h"
#include "wow_ticket.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("client: FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static const uint8_t secret[] = "shankpit-test-secret";
static const uint8_t pid[16] = {0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10};
#define NOW 5000u

static int ticket_auth(void *ctx, const unsigned char *ticket, int len, char *out, int cap) {
    (void)ctx;
    if (len != WOW_TICKET_BYTES) return 0;
    return wow_ticket_verify(secret, (int)sizeof secret - 1, ticket, NOW, out, cap);
}

int main(void) {
    WowClient cl;
    WowServer s;
    uint8_t frame[64], ack[WOW_WIRE_ACK_BYTES];
    int n, first_n;
    uint8_t first[WOW_WIRE_INTENT_BYTES];
    WowCastIntent cast;

    wow_client_begin(&cl);
    wow_server_begin(&s);
    s.auth = ticket_auth;

    /* ---- targets: only a change goes on the air ---- */
    n = wow_client_target(&cl, 0, 7, frame, sizeof frame);
    CHECK(n == WOW_WIRE_INTENT_BYTES && frame[1] == WOW_OP_SET_TARGET && frame[4] == 0 &&
          (frame[5] | (frame[6] << 8)) == 7, "a target change encoded to %d bytes", n);
    CHECK(wow_server_rx(&s, 1, frame, n, ack, sizeof ack) == -2, "a target was applied before AUTH");
    CHECK(wow_client_target(&cl, 0, 7, frame, sizeof frame) == 0, "the same target was sent twice");
    memcpy(first, frame, sizeof first); first_n = n;
    n = wow_client_target(&cl, 0, 9, frame, sizeof frame);
    CHECK(n == WOW_WIRE_INTENT_BYTES, "a second target change encoded to %d bytes", n);
    CHECK(wow_client_target(&cl, 0, -1, frame, sizeof frame) == WOW_WIRE_INTENT_BYTES && frame[1] == WOW_OP_CLEAR_TARGET,
          "clearing a target did not produce a CLEAR_TARGET frame");
    CHECK(wow_client_target(&cl, 0, -1, frame, sizeof frame) == 0, "clearing an empty slot was sent anyway");
    CHECK(wow_client_target(&cl, 7, 3, frame, sizeof frame) == -1, "a bad slot was accepted");

    /* ---- AUTH, then the same sequence against a live client slot ---- */
    {
        uint8_t ticket[WOW_TICKET_BYTES];
        wow_ticket_make(secret, (int)sizeof secret - 1, pid, NOW + 60u, ticket);
        n = wow_client_auth(ticket, frame, sizeof frame);
        CHECK(n == WOW_WIRE_AUTH_BYTES, "AUTH encoded to %d bytes", n);
        CHECK(wow_server_rx(&s, 1, frame, n, ack, sizeof ack) == 1 && s.c[1].authed == 1 &&
              strcmp(s.c[1].player_id, "01020304-0506-0708-090a-0b0c0d0e0f10") == 0,
              "AUTH with a real ticket did not bind the player");
    }

    n = wow_client_target(&cl, 0, 11, frame, sizeof frame);
    CHECK(n == WOW_WIRE_INTENT_BYTES && wow_server_rx(&s, 1, frame, n, ack, sizeof ack) == 1 &&
          wow_targets_get(&s.c[1].targets, 0) == 11, "an authenticated target change was not applied");

    /* an older frame replayed after a newer one is dropped by the receiver, not applied */
    {
        uint8_t newer[WOW_WIRE_INTENT_BYTES];
        int nn = wow_client_target(&cl, 1, 12, newer, sizeof newer);
        CHECK(nn == WOW_WIRE_INTENT_BYTES, "a focus change encoded to %d bytes", nn);
        (void)first_n; (void)first;
        CHECK(wow_server_rx(&s, 1, newer, nn, ack, sizeof ack) == 1, "the newer intent was not applied");
        CHECK(wow_server_rx(&s, 1, frame, n, ack, sizeof ack) == 0, "a replayed older intent was applied");
        CHECK(wow_targets_get(&s.c[1].targets, 1) == 12, "the newer focus was lost");
    }

    /* ---- casts: every press is sent, and each carries a newer sequence ---- */
    n = wow_client_cast(&cl, 2, WOW_TOKEN_TARGET, frame, sizeof frame);
    CHECK(n == WOW_WIRE_CAST_BYTES && frame[2] == 2 && frame[3] == WOW_TOKEN_TARGET, "a cast encoded to %d bytes", n);
    CHECK(wow_server_rx(&s, 1, frame, n, ack, sizeof ack) == 1, "a cast for owned weapon 2 was refused");
    n = wow_client_cast(&cl, 2, WOW_TOKEN_TARGET, frame, sizeof frame);
    CHECK(wow_server_rx(&s, 1, frame, n, ack, sizeof ack) == 1, "a repeated cast was refused");
    CHECK(wow_server_take_cast(&s, 1, &cast) && cast.weapon == 2 && cast.token == WOW_TOKEN_TARGET,
          "the queued cast did not come back out");
    CHECK(wow_server_take_cast(&s, 1, &cast) && cast.seq != 0, "the second cast was not queued FIFO");
    CHECK(wow_client_cast(&cl, -1, 0, frame, sizeof frame) == -1, "a negative weapon id was accepted");
    CHECK(wow_client_cast(&cl, 999, 0, frame, sizeof frame) == -1, "a weapon id above 255 was accepted");

    /* ---- retry + ACK: a lost ACK is resent UNCHANGED, an ACK retires the frame, duplicates are
     * re-ACKed, and the retry count is bounded by PARENA's wow_retry_max ---- */
    {
        WowClient rc;
        uint8_t f[WOW_WIRE_AUTH_BYTES], out[WOW_WIRE_AUTH_BYTES], a[WOW_WIRE_ACK_BYTES];
        WowAck wa;
        uint32_t t0 = 1000;
        int m;

        wow_client_begin(&rc);
        wow_client_set_preset(&rc, 1);                 /* slow link: 500 ms period, 2 retries */
        m = wow_client_target(&rc, 0, 21, f, sizeof f);
        CHECK(m == WOW_WIRE_INTENT_BYTES, "a retry-test target encoded to %d bytes", m);
        CHECK(wow_client_track(&rc, f, m, t0) == 1, "a sent frame was not tracked for retry");
        CHECK(wow_client_retry(&rc, t0 + 499, out, sizeof out) == 0, "a frame was resent early");
        CHECK(wow_client_retry(&rc, t0 + 500, out, sizeof out) == m && memcmp(out, f, (size_t)m) == 0,
              "the resend was not byte-identical (a fresh sequence would be dropped as stale)");

        wa.seq = (uint16_t)(f[2] | (f[3] << 8));
        wa.kind = WOW_OP_SET_TARGET;
        CHECK(wow_ack_encode(&wa, a, sizeof a) == WOW_WIRE_ACK_BYTES, "an ACK did not encode");
        CHECK(wow_client_ack(&rc, a, sizeof a) == 1, "an ACK did not retire the frame it names");
        CHECK(wow_client_pending(&rc) == 0, "an ACKed frame stayed pending");
        CHECK(wow_client_retry(&rc, t0 + 9000, out, sizeof out) == 0, "an ACKed frame was still retried");
        CHECK(wow_client_ack(&rc, a, sizeof a) == 0, "a duplicate ACK retired something");

        /* bounded: preset 1 allows 2 resends, then the frame is abandoned instead of looping */
        wow_client_begin(&rc);
        wow_client_set_preset(&rc, 1);
        m = wow_client_target(&rc, 0, 22, f, sizeof f);
        CHECK(wow_client_track(&rc, f, m, t0) == 1, "the second frame was not tracked");
        CHECK(wow_client_retry(&rc, t0 + 500, out, sizeof out) == m, "retry 1 was not sent");
        CHECK(wow_client_retry(&rc, t0 + 1000, out, sizeof out) == m, "retry 2 was not sent");
        CHECK(wow_client_retry(&rc, t0 + 1500, out, sizeof out) == -2, "the retry cap was not enforced");
        CHECK(wow_client_pending(&rc) == 0 && rc.abandoned == 1, "the abandoned frame was not dropped");
        CHECK(wow_client_track(NULL, f, m, t0) == -1, "a null client was tracked");
        CHECK(wow_client_track(&rc, f, 1, t0) == -1, "a truncated frame was tracked");

        /* a resend the server already applied is re-ACKed, so the LOST-FIRST-ACK case recovers */
        {
            WowServer rs;
            WowClient cc;
            uint8_t f2[WOW_WIRE_INTENT_BYTES], ack2[WOW_WIRE_ACK_BYTES];
            int q;
            wow_server_begin(&rs);
            rs.require_auth = 0;                       /* this block is about idempotence, not identity */
            wow_client_begin(&cc);
            q = wow_client_target(&cc, 0, 31, f2, sizeof f2);
            CHECK(q == WOW_WIRE_INTENT_BYTES, "the idempotence target encoded to %d bytes", q);
            CHECK(wow_server_rx(&rs, 2, f2, q, ack2, sizeof ack2) == 1, "a first intent was not ACKed");
            CHECK(wow_server_rx(&rs, 2, f2, q, ack2, sizeof ack2) == 1,
                  "a resent intent was not re-ACKed (a lost ACK would strand the client)");
            CHECK(wow_targets_get(&rs.c[2].targets, 0) == 31, "the resent intent changed the target");
        }
    }

    if (fails) { printf("client: %d FAILED\n", fails); return 1; }
    printf("client: all checks passed\n");
    return 0;
}
