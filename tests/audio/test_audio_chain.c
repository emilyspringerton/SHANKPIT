/* tests/audio/test_audio_chain.c -- `make test-audio-chain`. SHANKPIT's C filter-chain runner
 * (packages/audio/audio_chain.c over PARENA-generated audio_dsp_gen.c) must reproduce what
 * IDUNA NOCK's TypeScript runner renders for the same chain and input:
 * tests/audio/nock_chain_vectors.txt is produced by IDUNA
 * frontend/nock/scripts/gen_shankpit_chain_vectors.ts (line 1 = the chain exactly as IDUNA's
 * public /api/v1/nock-sound-filters/<name> serves it, line 2 = N, then N "in out" pairs).
 * Plus engine-side sanity: parsing, rejection of bad chains, the limiter ceiling. */
#include "../../packages/audio/audio_chain.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) printf("PASS: " __VA_ARGS__); else { printf("FAIL: " __VA_ARGS__); failures++; } printf("\n"); } while (0)

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "tests/audio/nock_chain_vectors.txt";
    FILE *fp = fopen(path, "r");
    if (!fp) { perror(path); return 2; }
    static char line[1 << 16];
    if (!fgets(line, sizeof line, fp)) return 2;
    AudioChain c;
    CHECK(audio_chain_parse_json(line, &c) && c.count == 7, "parses IDUNA's chain envelope (7 stages)");
    audio_chain_prepare(&c, 48000);
    int n = 0;
    if (fscanf(fp, "%d", &n) != 1 || n <= 0) return 2;
    float *x = malloc(sizeof(float) * (size_t)n), *want = malloc(sizeof(float) * (size_t)n);
    for (int i = 0; i < n; i++) if (fscanf(fp, "%f %f", &x[i], &want[i]) != 2) return 2;
    fclose(fp);
    /* engine-style: process in 512-frame callbacks, state carried across blocks */
    for (int o = 0; o < n; o += 512) audio_chain_process(&c, x + o, (n - o < 512) ? n - o : 512, 1);
    double maxerr = 0;
    int worst = 0;
    for (int i = 0; i < n; i++) if (fabs(x[i] - want[i]) > maxerr) { maxerr = fabs(x[i] - want[i]); worst = i; }
    CHECK(maxerr < 1e-6, "C runner == NOCK TS runner over %d samples (max |diff| %.3g at %d)", n, maxerr, worst);
    double pk = 0;
    for (int i = 0; i < n; i++) pk = fmax(pk, fabs(x[i]));
    CHECK(20 * log10(pk) <= -4.0 + 1e-6, "limiter -3 dBFS then gain -1 dB => peak <= -4 dBFS (%.3f)", 20 * log10(pk));

    AudioChain bad;
    CHECK(!audio_chain_parse_json("{\"version\":2,\"stages\":[]}", &bad), "rejects unknown version");
    CHECK(!audio_chain_parse_json("{\"version\":1,\"stages\":[{\"type\":\"reverb\"}]}", &bad), "rejects unknown stage type");
    CHECK(!audio_chain_fetch("http://127.0.0.1:1", "../etc; rm -rf /", 48000, &bad), "refuses unsafe chain names before touching the shell");

    free(x); free(want);
    if (failures) { printf("%d FAILURE(S)\n", failures); return 1; }
    printf("ALL AUDIO CHAIN CHECKS PASSED\n");
    return 0;
}
