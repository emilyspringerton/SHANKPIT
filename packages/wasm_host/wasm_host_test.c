/* packages/wasm_host/wasm_host_test.c -- `make test-wasm-host`. The host's contract
 * (docs/T0_LEVEL_WASM_CONTRACT.md "Host C API"), against hand-assembled modules:
 *   a host function receives its arguments in order and the export's result comes back;
 *   unbound import, wrong import signature, non-function import, malformed bytes: refused at load,
 *   with a message that names the import;
 *   export lookup by exact name AND signature;
 *   a trap returns WH_ERR_TRAP and the instance stays dead.
 * Then the real thing, if present: WOTAN's committed brawlpit.wasm (zero imports), bp_init + 600
 * bp_frame calls, the framebuffer must not be blank, and the per-frame cost is printed (the frame-budget
 * acceptance item is measured on Windows, not asserted here -- this box is a 2.2 GHz cloud core).
 *   WH_FIXTURE=<path> overrides the fixture path; a missing fixture is reported and skipped, not faked.
 */
#define _POSIX_C_SOURCE 199309L
#include "wasm_host.h"
#include "wasm_host_test_fixtures.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures = 0;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); failures++; } else printf("PASS: %s\n", m); } while (0)

static int32_t g_cb_args[4];
static int g_cb_calls = 0;
static int32_t cb(void *user, const int32_t *args) {
    int k;
    (void)user;
    for (k = 0; k < 4; k++) g_cb_args[k] = args[k];
    g_cb_calls++;
    return 0;
}

static unsigned char *read_file(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    long len;
    unsigned char *b;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    b = (unsigned char *)malloc((size_t)len);
    if (b && fread(b, 1, (size_t)len, f) != (size_t)len) { free(b); b = NULL; }
    fclose(f);
    *n = (size_t)len;
    return b;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

static void test_small_modules(void) {
    wh_import offer = { "env", "cb", "v(iiii)", cb, NULL };
    wh_import wrong = { "env", "cb", "v(iii)", cb, NULL };
    wh_instance *inst = NULL;
    wh_func go, boom, add, bad;
    char msg[256];
    int32_t r = 0, two[2];
    wh_status s;

    s = wh_instantiate(fx_calls, sizeof fx_calls, &offer, 1, 0, &inst, msg, sizeof msg);
    CHECK(s == WH_OK && inst != NULL, "a module whose one import is offered with the right signature loads");
    if (!inst) return;
    CHECK(wh_lookup(inst, "go", "i()", &go, msg, sizeof msg) == WH_OK, "export go binds as i()");
    CHECK(wh_call_i32(&go, NULL, 0, &r, msg, sizeof msg) == WH_OK && r == 42, "go returns 42");
    CHECK(g_cb_calls == 1 && g_cb_args[0] == 7 && g_cb_args[1] == 1 && g_cb_args[2] == 2 && g_cb_args[3] == 3,
          "the host function saw (7, 1, 2, 3) in order");
    CHECK(wh_lookup(inst, "add", "i(ii)", &add, msg, sizeof msg) == WH_OK, "export add binds as i(ii)");
    two[0] = 40; two[1] = 2;
    CHECK(wh_call_i32(&add, two, 2, &r, msg, sizeof msg) == WH_OK && r == 42, "add(40, 2) = 42");
    two[0] = 2147483647; two[1] = 1;
    CHECK(wh_call_i32(&add, two, 2, &r, msg, sizeof msg) == WH_OK && r == (int32_t)0x80000000u,
          "i32 wraps exactly like wasm (no host-side widening)");
    CHECK(wh_lookup(inst, "add", "i(i)", &bad, msg, sizeof msg) == WH_ERR_EXPORT_SIG && strstr(msg, "i(ii)") != NULL,
          "lookup with the wrong signature is refused and says what the module has");
    CHECK(wh_lookup(inst, "nope", "v()", &bad, msg, sizeof msg) == WH_ERR_EXPORT_MISSING, "a missing export is refused");
    CHECK(wh_call_i32(&add, two, 1, &r, msg, sizeof msg) == WH_ERR_ARGS, "a call with the wrong arity is refused");
    CHECK(wh_lookup(inst, "boom", "v()", &boom, msg, sizeof msg) == WH_OK, "export boom binds");
    s = wh_call_i32(&boom, NULL, 0, NULL, msg, sizeof msg);
    CHECK(s == WH_ERR_TRAP && strstr(msg, "unreachable") != NULL, "unreachable traps, and the message says so");
    CHECK(wh_is_dead(inst), "the trap killed the instance");
    CHECK(wh_call_i32(&go, NULL, 0, &r, msg, sizeof msg) == WH_ERR_TRAP && g_cb_calls == 1,
          "a dead instance refuses every later call (go did not run again)");
    CHECK(wh_lookup(inst, "go", "i()", &bad, msg, sizeof msg) == WH_ERR_TRAP, "and every later lookup");
    wh_destroy(inst);

    inst = NULL;
    s = wh_instantiate(fx_calls, sizeof fx_calls, NULL, 0, 0, &inst, msg, sizeof msg);
    CHECK(s == WH_ERR_IMPORT_UNBOUND && inst == NULL && strstr(msg, "env.cb") != NULL,
          "an import the host does not offer is refused at load, by name");
    s = wh_instantiate(fx_calls, sizeof fx_calls, &wrong, 1, 0, &inst, msg, sizeof msg);
    CHECK(s == WH_ERR_IMPORT_SIG && inst == NULL && strstr(msg, "v(iiii)") != NULL,
          "an import offered with another signature is refused at load");
    s = wh_instantiate(fx_memimport, sizeof fx_memimport, &offer, 1, 0, &inst, msg, sizeof msg);
    CHECK(s == WH_ERR_IMPORT_KIND && inst == NULL, "a memory import is refused (the host offers functions only)");
    s = wh_instantiate(fx_calls, 20, &offer, 1, 0, &inst, msg, sizeof msg);
    CHECK(s == WH_ERR_PARSE && inst == NULL, "a truncated module is refused as malformed");
}

static void test_brawlpit_fixture(void) {
    const char *path = getenv("WH_FIXTURE") ? getenv("WH_FIXTURE") : "../WOTAN/BRAWLPIT/brawlpit.wasm";
    size_t n = 0;
    unsigned char *bytes = read_file(path, &n);
    wh_instance *inst = NULL;
    wh_func init, frame, fb_addr;
    wh_val t;
    char msg[256];
    int32_t fb = 0;
    uint32_t memsz = 0, k, lit = 0;
    uint8_t *mem;
    int i, ok = 1;
    double t0, t1;
    if (!bytes) {
        printf("SKIP: %s not found -- the real-module check needs the sibling WOTAN checkout (or WH_FIXTURE)\n", path);
        return;
    }
    CHECK(wh_instantiate(bytes, n, NULL, 0, 0, &inst, msg, sizeof msg) == WH_OK, "BRAWLPIT web module loads with no imports offered");
    free(bytes);
    if (!inst) { printf("      %s\n", msg); return; }
    CHECK(wh_lookup(inst, "bp_init", "v()", &init, msg, sizeof msg) == WH_OK &&
          wh_lookup(inst, "bp_frame", "v(F)", &frame, msg, sizeof msg) == WH_OK &&
          wh_lookup(inst, "bp_fb_addr", "i()", &fb_addr, msg, sizeof msg) == WH_OK, "bp_init / bp_frame / bp_fb_addr bind");
    CHECK(wh_call(&init, NULL, 0, NULL, msg, sizeof msg) == WH_OK, "bp_init runs");
    t.kind = 'F';
    t.i32 = 0;
    t0 = now_ms();
    for (i = 0; i < 600 && ok; i++) {
        t.f64 = 1000.0 + (double)i * (1000.0 / 60.0);
        ok = wh_call(&frame, &t, 1, NULL, msg, sizeof msg) == WH_OK;
    }
    t1 = now_ms();
    CHECK(ok, "600 bp_frame calls run without a trap");
    CHECK(wh_call_i32(&fb_addr, NULL, 0, &fb, msg, sizeof msg) == WH_OK && fb > 0, "bp_fb_addr answers");
    mem = wh_memory(inst, &memsz);
    CHECK(mem != NULL && memsz == 8388608u, "linear memory is the fixed 8 MB");
    if (mem && fb > 0 && (uint32_t)fb + 960u * 540u * 4u <= memsz) {
        for (k = 0; k < 960u * 540u; k++) {
            const uint8_t *px = mem + fb + k * 4u;
            if (px[0] | px[1] | px[2]) lit++;
        }
    }
    CHECK(lit > 10000u, "the framebuffer is not blank after 600 frames (title screen drawn)");
    printf("INFO: %.2f ms per bp_frame (init + 600 frames, sim + software render), %u lit pixels\n",
           (t1 - t0) / 600.0, (unsigned)lit);
    wh_destroy(inst);
}

int main(void) {
    test_small_modules();
    test_brawlpit_fixture();
    if (failures) { printf("%d FAILED\n", failures); return 1; }
    printf("ALL PASS\n");
    return 0;
}
