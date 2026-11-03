#ifndef SHANKPIT_HMAC_SHA256_H
#define SHANKPIT_HMAC_SHA256_H

/* hmac_sha256.h -- RFC 2104 HMAC over goldenband/sha256.h.
 *
 * This exists for one reason: IDUNA signs the SHANKPIT player ticket with HMAC-SHA256
 * (IDUNA/internal/http/handlers/shankpit_ticket.go) and the C game server has to verify that ticket
 * locally -- no HTTP client, no crypto library, no second ticket format. The two sides therefore share
 * this file and the issuer's own layout, and the vectors in hmac_sha256_test.c (`make test-hmac`) are
 * RFC 4231's, not ours.
 *
 * The key schedule is not kept after the call: k is wiped before returning. */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../goldenband/sha256.h"

#define HMAC_SHA256_BYTES 32
#define HMAC_SHA256_BLOCK 64

static inline void hmac_sha256(const uint8_t *key, size_t key_len,
                               const uint8_t *data, size_t data_len,
                               uint8_t out[HMAC_SHA256_BYTES]) {
    uint8_t k[HMAC_SHA256_BLOCK];
    uint8_t pad[HMAC_SHA256_BLOCK];
    uint8_t inner[HMAC_SHA256_BYTES];
    sha256_ctx ctx;
    size_t i;

    memset(k, 0, sizeof k);
    if (key_len > HMAC_SHA256_BLOCK) sha256(key, key_len, k);   /* a long key is hashed down first */
    else if (key_len) memcpy(k, key, key_len);

    for (i = 0; i < HMAC_SHA256_BLOCK; i++) pad[i] = (uint8_t)(k[i] ^ 0x36);
    sha256_init(&ctx);
    sha256_update(&ctx, pad, sizeof pad);
    if (data_len) sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, inner);

    for (i = 0; i < HMAC_SHA256_BLOCK; i++) pad[i] = (uint8_t)(k[i] ^ 0x5c);
    sha256_init(&ctx);
    sha256_update(&ctx, pad, sizeof pad);
    sha256_update(&ctx, inner, sizeof inner);
    sha256_final(&ctx, out);

    memset(k, 0, sizeof k);
}

/* 1 equal, 0 different, without short-circuiting: a MAC comparison must not leak how far it matched. */
static inline int hmac_sha256_ct_equal(const uint8_t *a, const uint8_t *b, size_t n) {
    uint8_t diff = 0;
    size_t i;
    for (i = 0; i < n; i++) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}

#endif /* SHANKPIT_HMAC_SHA256_H */
