/* apps/game/wasm_core.c -- the first pure-WASM slice of the clean-room game.
 *
 * No SDL, no libc, no host.c, no sockets: this links only the PARENA rules, the REFLUX runtime and the
 * level-module registry. It is the core a browser client can boot today; the render/input/audio
 * modules are still native-only, which is exactly what wasm_boot()'s capability mask says.
 *
 * Every module decision (defaults, mandatory bits, sanitising) is still packages/game/level_mods.prn.
 * This file is only the exported ABI plus the two freestanding memory helpers clang may call.
 */
#include "game_api.h"
#include "level_mods_host.h"
#include "reflux_runtime.h"

typedef __SIZE_TYPE__ wasm_size_t;

/* -nostdlib: these are the only string routines the linked host code can reach. */
void *memset(void *dst, int value, wasm_size_t n) {
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)value;
    return dst;
}

void *memcpy(void *dst, const void *src, wasm_size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

/* ---- freestanding trig -------------------------------------------------------------------------
 * game_rules.c's loop_sin/loop_cos/loop_atan2 call libm. wasm32 has no libm, so the core carries
 * its own: fdlibm's sin kernel with a quadrant fold, and a minimax atan for atan2. Accurate to well
 * under a milliradian over the rules' range, deterministic, and no imports.
 */
#define WASM_PI 3.14159265358979323846
#define WASM_TAU 6.28318530717958647692
#define WASM_HALF_PI 1.57079632679489661923

static double wasm_kernel_sin(double x) {
    double x2 = x * x;
    return x + x * x2 *
                   (-1.66666666666666324348e-01 +
                    x2 * (8.33333333332248946124e-03 +
                          x2 * (-1.98412698298579493134e-04 +
                                x2 * (2.75573137070700676789e-06 +
                                      x2 * (-2.50507602534068634195e-08 +
                                            x2 * 1.58969099521155010221e-10)))));
}

double sin(double x) {
    double turns = x / WASM_TAU;
    long long n = (long long)turns; /* toward zero, then fold into [-pi, pi] */
    x -= (double)n * WASM_TAU;
    if (x > WASM_PI) x -= WASM_TAU;
    if (x < -WASM_PI) x += WASM_TAU;
    if (x > WASM_HALF_PI) x = WASM_PI - x;
    else if (x < -WASM_HALF_PI) x = -WASM_PI - x;
    return wasm_kernel_sin(x);
}

double cos(double x) { return sin(x + WASM_HALF_PI); }

static double wasm_atan_small(double x) {
    double x2 = x * x;
    return x * (0.9999993329 +
                x2 * (-0.3332985605 +
                      x2 * (0.1994653599 +
                            x2 * (-0.1390853351 +
                                  x2 * (0.0964200441 +
                                        x2 * (-0.0559098861 +
                                              x2 * (0.0218612288 + x2 * -0.0040540580)))))));
}

static double wasm_atan(double x) {
    int neg = x < 0;
    double ax = neg ? -x : x;
    double r;
    if (ax > 1.0) r = WASM_HALF_PI - wasm_atan_small(1.0 / ax);
    else r = wasm_atan_small(ax);
    return neg ? -r : r;
}

double atan2(double y, double x) {
    if (x > 0) return wasm_atan(y / x);
    if (x < 0) return y >= 0 ? wasm_atan(y / x) + WASM_PI : wasm_atan(y / x) - WASM_PI;
    if (y > 0) return WASM_HALF_PI;
    if (y < 0) return -WASM_HALF_PI;
    return 0.0;
}

/* Boot a core-only client: the registry starts empty and declares which modules this build has
 * actually ported. Returns the effective capability mask (3 today: core sim + the level module). */
__attribute__((visibility("default"))) int wasm_boot(void) {
    level_mods_reset();
    reflux_host_reset();
    return level_mods_set_capabilities(lm_bit(lm_mod_core_sim()) | lm_bit(lm_mod_level()));
}

__attribute__((visibility("default"))) int wasm_register_level(int level_id, int env) {
    return level_mods_register(level_id, env);
}

/* A full state from the server (or a replay) goes through the same forward-only, capability-clamped
 * path the native client uses. Returns 1 only when the resulting mask changed. */
__attribute__((visibility("default"))) int wasm_apply_state(int level_id, int mask, int revision) {
    return level_mods_apply_state(level_id, mask, revision);
}

__attribute__((visibility("default"))) int wasm_set_mod(int level_id, int mod, int on) {
    return level_mods_set(level_id, mod, on);
}

__attribute__((visibility("default"))) int wasm_mask(int level_id) {
    return level_mods_mask(level_id);
}

__attribute__((visibility("default"))) int wasm_revision(int level_id) {
    return level_mods_revision(level_id);
}

__attribute__((visibility("default"))) int wasm_enabled(int level_id, int mod) {
    return level_mods_enabled(level_id, mod);
}

__attribute__((visibility("default"))) int wasm_env(int level_id) {
    return level_mods_env(level_id);
}

__attribute__((visibility("default"))) int wasm_level_count(void) {
    return level_mods_count();
}

__attribute__((visibility("default"))) int wasm_module_count(void) {
    return lm_mod_count();
}

__attribute__((visibility("default"))) int wasm_default_mask(int env) {
    return lm_default_mask(env);
}

__attribute__((visibility("default"))) int wasm_capabilities(void) {
    return level_mods_capabilities();
}

__attribute__((visibility("default"))) int wasm_event_count(void) {
    return reflux_host_total_dispatched();
}

/* The rules' own trig, exported so a host can verify the freestanding implementation against its
 * JavaScript Math rather than assuming it. */
__attribute__((visibility("default"))) double wasm_sin_deg(int deg) {
    return loop_sin_deg(deg);
}

__attribute__((visibility("default"))) double wasm_cos_deg(int deg) {
    return loop_cos_deg(deg);
}

__attribute__((visibility("default"))) double wasm_atan2(int dz, int dx) {
    return loop_atan2(dz, dx);
}
