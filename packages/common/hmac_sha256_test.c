/* hmac_sha256_test.c -- RFC 4231 vectors for hmac_sha256.h. make test-hmac.
 * These are the RFC's numbers, so a wrong key schedule or padding cannot agree with them. */
#include "hmac_sha256.h"
#include <stdio.h>

static int fails = 0;

static void hex_of(const uint8_t *b, size_t n, char *out) {
    static const char *d = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; i++) { out[2 * i] = d[b[i] >> 4]; out[2 * i + 1] = d[b[i] & 0x0F]; }
    out[2 * n] = '\0';
}

static void vec(int id, const uint8_t *key, size_t klen, const uint8_t *data, size_t dlen, const char *want) {
    uint8_t mac[HMAC_SHA256_BYTES];
    char got[2 * HMAC_SHA256_BYTES + 1];
    hmac_sha256(key, klen, data, dlen, mac);
    hex_of(mac, sizeof mac, got);
    if (strcmp(got, want) != 0) { printf("hmac: FAIL rfc4231 case %d\n  got  %s\n  want %s\n", id, got, want); fails++; }
}

int main(void) {
    uint8_t k20[20], a20[20], dd50[50], long_key[131];
    memset(k20, 0x0b, sizeof k20);
    vec(1, k20, sizeof k20, (const uint8_t *)"Hi There", 8,
        "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");

    vec(2, (const uint8_t *)"Jefe", 4, (const uint8_t *)"what do ya want for nothing?", 28,
        "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");

    memset(a20, 0xaa, sizeof a20);
    memset(dd50, 0xdd, sizeof dd50);
    vec(3, a20, sizeof a20, dd50, sizeof dd50,
        "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe");

    memset(long_key, 0xaa, sizeof long_key);
    vec(6, long_key, sizeof long_key,
        (const uint8_t *)"Test Using Larger Than Block-Size Key - Hash Key First", 54,
        "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");

    {
        uint8_t a[HMAC_SHA256_BYTES] = {0}, b[HMAC_SHA256_BYTES] = {0};
        if (hmac_sha256_ct_equal(a, b, sizeof a) != 1) { printf("hmac: FAIL equal MACs compared unequal\n"); fails++; }
        b[HMAC_SHA256_BYTES - 1] = 1;
        if (hmac_sha256_ct_equal(a, b, sizeof a) != 0) { printf("hmac: FAIL a one-bit difference compared equal\n"); fails++; }
    }

    if (fails) { printf("hmac: %d FAILED\n", fails); return 1; }
    printf("hmac: all checks passed\n");
    return 0;
}
