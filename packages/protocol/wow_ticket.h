#ifndef SHANKPIT_WOW_TICKET_H
#define SHANKPIT_WOW_TICKET_H

/* wow_ticket.h -- the IDUNA player ticket, byte for byte as IDUNA mints it
 * (IDUNA/internal/http/handlers/shankpit_ticket.go):
 *
 *   player_id  16 raw UUID bytes
 *   expires_at  4 bytes, little-endian unix seconds
 *   mac        16 bytes = HMAC-SHA256(SHANKPIT_TICKET_SECRET, player_id||expires_at)[:16]
 *   ---------------------------------------------------------------
 *   total      36 bytes, 5-minute TTL, presented once per connect
 *
 * IDUNA returns it hex-encoded (72 chars) in its JSON body; the wire carries the RAW bytes, so the
 * server does no hex parsing on the receive path and the ticket costs 36 bytes instead of 72. The MAC
 * is truncated to 128 bits by the issuer, not by us. `now_unix` is the caller's clock: this header
 * never reads one, so it is testable and has no hidden time dependency. */

#include <stdint.h>
#include <string.h>
#include "../common/hmac_sha256.h"

#define WOW_TICKET_BYTES    36
#define WOW_TICKET_PAYLOAD  20
#define WOW_TICKET_MAC      16
#define WOW_TICKET_UUID_LEN 36

/* Mint a ticket. The client never needs this -- IDUNA mints -- but the test does, and it is the only
 * reason the signing side exists in C at all. */
static inline void wow_ticket_make(const uint8_t *secret, int secret_len,
                                   const uint8_t *player_id,
                                   uint32_t expires_at,
                                   uint8_t out[WOW_TICKET_BYTES]) {
    uint8_t mac[HMAC_SHA256_BYTES];
    memcpy(out, player_id, 16);
    out[16] = (uint8_t)(expires_at & 0xFFu);
    out[17] = (uint8_t)((expires_at >> 8) & 0xFFu);
    out[18] = (uint8_t)((expires_at >> 16) & 0xFFu);
    out[19] = (uint8_t)((expires_at >> 24) & 0xFFu);
    hmac_sha256(secret, (size_t)secret_len, out, WOW_TICKET_PAYLOAD, mac);
    memcpy(out + WOW_TICKET_PAYLOAD, mac, WOW_TICKET_MAC);
}

/* The canonical 8-4-4-4-12 UUID text IDUNA would have returned as player_id. */
static inline void wow_ticket_uuid_text(const uint8_t id[16], char *out, int cap) {
    static const char hex[] = "0123456789abcdef";
    int i, n = 0;
    if (!out) return;
    if (cap < WOW_TICKET_UUID_LEN + 1) { if (cap > 0) out[0] = '\0'; return; }
    for (i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out[n++] = '-';
        out[n++] = hex[id[i] >> 4];
        out[n++] = hex[id[i] & 0x0F];
    }
    out[n] = '\0';
}

/* 1 verified, 0 refused (bad MAC, or expired at `now_unix`), -1 malformed arguments.
 * The MAC is checked BEFORE the expiry, so a forged ticket cannot use this function as an oracle for
 * the server's clock, and a wrong-secret ticket is refused without formatting anything. */
static inline int wow_ticket_verify(const uint8_t *secret, int secret_len,
                                    const uint8_t ticket[WOW_TICKET_BYTES],
                                    uint32_t now_unix,
                                    char *player_id, int cap) {
    uint8_t mac[HMAC_SHA256_BYTES];
    uint32_t expires;
    if (!secret || secret_len <= 0 || !ticket || !player_id || cap < WOW_TICKET_UUID_LEN + 1) return -1;
    hmac_sha256(secret, (size_t)secret_len, ticket, WOW_TICKET_PAYLOAD, mac);
    if (!hmac_sha256_ct_equal(mac, ticket + WOW_TICKET_PAYLOAD, WOW_TICKET_MAC)) return 0;
    expires = (uint32_t)ticket[16] | ((uint32_t)ticket[17] << 8) |
              ((uint32_t)ticket[18] << 16) | ((uint32_t)ticket[19] << 24);
    if (expires <= now_unix) return 0;
    wow_ticket_uuid_text(ticket, player_id, cap);
    return 1;
}

#endif /* SHANKPIT_WOW_TICKET_H */
