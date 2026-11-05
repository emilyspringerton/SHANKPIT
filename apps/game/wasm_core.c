/* apps/game/wasm_core.c -- the first pure-WASM slice of the clean-room game.
 *
 * No SDL, no libc, no host.c, no sockets: this links only the PARENA rules, the REFLUX runtime and the
 * level-module registry. It is the core a browser client can boot today; the render/input/audio
 * modules are still native-only, which is exactly what wasm_boot()'s capability mask says.
 *
 * Every module decision (defaults, mandatory bits, sanitising) is still packages/game/level_mods.prn.
 * This file is only the exported ABI plus the two freestanding memory helpers clang may call.
 */
#include "build_profile.h"
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

/* ---- playable framebuffer ----------------------------------------------------------------------
 * The browser owns the canvas and the keyboard; this file owns the same fixed-step movement and the
 * same projection/palette rules the native host draws with. Pixels are written as 0xAABBGGRR, which
 * is exactly the byte order ImageData wants on a little-endian canvas, so JavaScript can copy the
 * framebuffer without a per-pixel swap.
 */
#define WASM_FB_W 1024
#define WASM_FB_H 576

static unsigned int g_fb[WASM_FB_W * WASM_FB_H];
static int g_level_id = 1;
static int g_player_x;
static int g_player_z = -700;
static int g_player_yaw;
static int g_input_fwd;
static int g_input_strafe;
static int g_frame_acc;
static int g_ticks;

static unsigned int wasm_rgba(int rgb) {
    unsigned int r = (unsigned int)((rgb >> 16) & 255);
    unsigned int g = (unsigned int)((rgb >> 8) & 255);
    unsigned int b = (unsigned int)(rgb & 255);
    return 0xff000000u | (b << 16) | (g << 8) | r;
}

static unsigned int wasm_shade(int rgb, int pct) {
    int r, g, b;
    if (pct < 0) pct = 0;
    if (pct > 200) pct = 200;
    r = ((rgb >> 16) & 255) * pct / 100;
    g = ((rgb >> 8) & 255) * pct / 100;
    b = (rgb & 255) * pct / 100;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return wasm_rgba((r << 16) | (g << 8) | b);
}

static void fb_px(int x, int y, unsigned int c) {
    if (x < 0 || x >= WASM_FB_W || y < 0 || y >= WASM_FB_H) return;
    g_fb[y * WASM_FB_W + x] = c;
}

static void fb_rect(int x, int y, int w, int h, unsigned int c) {
    int yy, xx;
    if (w <= 0 || h <= 0) return;
    for (yy = y; yy < y + h; yy++) {
        if (yy < 0 || yy >= WASM_FB_H) continue;
        for (xx = x; xx < x + w; xx++) {
            if (xx < 0 || xx >= WASM_FB_W) continue;
            g_fb[yy * WASM_FB_W + xx] = c;
        }
    }
}

static void fb_line(int x0, int y0, int x1, int y1, unsigned int c) {
    int dx = x1 - x0, dy = y1 - y0;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    int err, e2;
    dx = dx < 0 ? -dx : dx;
    dy = dy < 0 ? -dy : dy;
    err = (dx > dy ? dx : -dy) / 2;
    for (;;) {
        fb_px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        e2 = err;
        if (e2 > -dx) { err -= dy; x0 += sx; }
        if (e2 < dy) { err += dx; y0 += sy; }
    }
}

/* Same near-plane clip as the native host: a grid line with one end behind the camera is shortened
 * instead of being dropped or projected into a smear. */
static void world_line(int camx, int camz, int yaw, int x0, int z0, int x1, int z1,
                       unsigned int c) {
    double f0 = rp_fwd(x0 - camx, z0 - camz, yaw);
    double f1 = rp_fwd(x1 - camx, z1 - camz, yaw);
    double nearf = (double)rp_near();
    if (f0 <= nearf && f1 <= nearf) return;
    if (f0 <= nearf) {
        double t = (nearf - f0) / (f1 - f0);
        x0 = x0 + (int)((double)(x1 - x0) * t);
        z0 = z0 + (int)((double)(z1 - z0) * t);
    } else if (f1 <= nearf) {
        double t = (nearf - f1) / (f0 - f1);
        x1 = x1 + (int)((double)(x0 - x1) * t);
        z1 = z1 + (int)((double)(z0 - z1) * t);
    }
    fb_line(rp_screen_x(x0 - camx, z0 - camz, yaw),
            rp_feet_y(x0 - camx, z0 - camz, yaw),
            rp_screen_x(x1 - camx, z1 - camz, yaw),
            rp_feet_y(x1 - camx, z1 - camz, yaw), c);
}

static void draw_body(int camx, int camz, int yaw, int x, int z, int hue) {
    double f = rp_fwd(x - camx, z - camz, yaw);
    int sx, feet, h, w;
    if (!rp_visible(f)) return;
    sx = rp_screen_x(x - camx, z - camz, yaw);
    feet = rp_feet_y(x - camx, z - camz, yaw);
    h = rp_body_h(f, 180);
    w = rp_body_w(f, 180);
    if (h < 3) return;
    fb_rect(sx - w / 2, feet - h, w, h, wasm_rgba(player_hue(hue)));
    fb_rect(sx - w / 2, feet - h, w, h / 5, wasm_shade(player_hue(hue), 165));
    if (w >= 12) {
        int bar = w * 2;
        fb_rect(sx - bar / 2, feet - h - 9, bar, 3, wasm_rgba(0x301818));
        fb_rect(sx - bar / 2, feet - h - 9, bar, 3, wasm_rgba(0x40e040));
    }
}

static void wasm_draw(void) {
    int x, y, g;
    int camx = g_player_x, camz = g_player_z, yaw = g_player_yaw;
    int half = loop_arena_half();
    int horizon = rp_horizon();
    unsigned int grid = wasm_shade(floor_color(), rp_floor_shade(900.0));

    for (y = 0; y < WASM_FB_H; y++) {
        unsigned int c;
        if (y < horizon) {
            if (level_mods_sky_enabled(g_level_id)) {
                int stop = rp_sky_stop(y);
                c = wasm_rgba(stop == 2 ? sky_top() : (stop == 1 ? sky_mid() : sky_horizon()));
            } else {
                c = wasm_rgba(0x101216);
            }
        } else {
            c = wasm_rgba(floor_color());
            if ((y - horizon) % 64 == 0) c = wasm_shade(floor_color(), 92);
        }
        for (x = 0; x < WASM_FB_W; x++) g_fb[y * WASM_FB_W + x] = c;
    }

    for (g = -half; g <= half; g += 200) {
        world_line(camx, camz, yaw, g, -half, g, half, grid);
        world_line(camx, camz, yaw, -half, g, half, g, grid);
    }

    {
        int i;
        for (i = 0; i < 16; i++) {
            int px = loop_ring_x(i, 16, half - 20);
            int pz = loop_ring_z(i, 16, half - 20);
            double f = rp_fwd(px - camx, pz - camz, yaw);
            int sx, feet, h, w;
            if (!rp_visible(f)) continue;
            sx = rp_screen_x(px - camx, pz - camz, yaw);
            feet = rp_feet_y(px - camx, pz - camz, yaw);
            h = rp_body_h(f, loop_wall_height());
            w = rp_body_w(f, loop_wall_height());
            if (h < 2) continue;
            fb_rect(sx - w / 2, feet - h, w, h, wasm_shade(wall_color(), rp_wall_shade()));
            fb_rect(sx - w / 2, feet - h, w, 3, wasm_shade(wall_color(), 130));
        }
    }

    /* Three arena bodies, placed with the same arc rule the native wave uses. */
    {
        int i;
        for (i = 0; i < 3; i++) {
            int bx = loop_arc_x(i, 3, 720, 100);
            int bz = loop_arc_z(i, 3, 720, 100);
            draw_body(camx, camz, yaw, bx, bz, i + 1);
        }
    }

    /* crosshair, same geometry as the native HUD */
    fb_rect(WASM_FB_W / 2 - 10, WASM_FB_H / 2, 7, 2, wasm_rgba(0xf0f0e0));
    fb_rect(WASM_FB_W / 2 + 3, WASM_FB_H / 2, 7, 2, wasm_rgba(0xf0f0e0));
    fb_rect(WASM_FB_W / 2, WASM_FB_H / 2 - 10, 2, 7, wasm_rgba(0xf0f0e0));
    fb_rect(WASM_FB_W / 2, WASM_FB_H / 2 + 3, 2, 7, wasm_rgba(0xf0f0e0));
}

/* Boot a core-only client: the registry starts empty and declares which modules this build has
 * actually ported. Returns the effective capability mask (3 today: core sim + the level module). */
__attribute__((visibility("default"))) int wasm_boot(void) {
    level_mods_reset();
    reflux_host_reset();
    return build_profile_apply();
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

/* 0 = this build links no transport, read from the same profile table wasm_boot applied. */
__attribute__((visibility("default"))) int wasm_net_status(void) {
    return build_profile_net();
}

__attribute__((visibility("default"))) int wasm_profile_target(void) {
    return build_profile_target();
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

/* ---- playable client ABI ------------------------------------------------------------------------
 * wasm_boot stays the registry entry point; wasm_play_boot is the browser's single call: boot the
 * core, register a level, put a body on the floor and draw the first frame. Input is a stick, not a
 * velocity, and movement still goes through the loop rules exactly as it does natively.
 */
__attribute__((visibility("default"))) int wasm_play_boot(int level_id, int env) {
    int caps = wasm_boot();
    if (!level_mods_register(level_id, env)) return -1;
    g_level_id = level_id;
    g_player_x = 0;
    g_player_z = -700;
    g_player_yaw = 0;
    g_input_fwd = 0;
    g_input_strafe = 0;
    g_frame_acc = 0;
    g_ticks = 0;
    wasm_draw();
    return caps;
}

__attribute__((visibility("default"))) int wasm_set_input(int fwd, int strafe) {
    g_input_fwd = loop_clamp(fwd, -100, 100);
    g_input_strafe = loop_clamp(strafe, -100, 100);
    return 0;
}

/* Mouse and keyboard turn arrive as deltas, like the native SDL mouse path. */
__attribute__((visibility("default"))) int wasm_turn(int md) {
    g_player_yaw += md;
    while (g_player_yaw > 3141) g_player_yaw -= 6283;
    while (g_player_yaw < -3141) g_player_yaw += 6283;
    return g_player_yaw;
}

/* One browser frame: spend real elapsed milliseconds on whole 16 ms ticks, move the body through the
 * loop rules, then draw. Returns the number of simulation ticks actually run. */
__attribute__((visibility("default"))) int wasm_frame(int dt_ms) {
    int steps = frame_steps(g_frame_acc, dt_ms);
    int i;
    g_frame_acc = frame_acc(g_frame_acc, dt_ms);
    for (i = 0; i < steps; i++) {
        g_player_x = loop_move_x(g_player_x, g_player_yaw, g_input_fwd, g_input_strafe);
        g_player_z = loop_move_z(g_player_z, g_player_yaw, g_input_fwd, g_input_strafe);
    }
    g_ticks += steps;
    wasm_draw();
    return steps;
}

__attribute__((visibility("default"))) int wasm_player_x(void) { return g_player_x; }
__attribute__((visibility("default"))) int wasm_player_z(void) { return g_player_z; }
__attribute__((visibility("default"))) int wasm_player_yaw(void) { return g_player_yaw; }
__attribute__((visibility("default"))) int wasm_ticks(void) { return g_ticks; }
__attribute__((visibility("default"))) int wasm_level(void) { return g_level_id; }
__attribute__((visibility("default"))) int wasm_fb_width(void) { return WASM_FB_W; }
__attribute__((visibility("default"))) int wasm_fb_height(void) { return WASM_FB_H; }

/* Pointer into the module's linear memory. The canvas reads 0xAABBGGRR words directly. */
__attribute__((visibility("default"))) unsigned int wasm_fb_ptr(void) {
    return (unsigned int)(__SIZE_TYPE__)g_fb;
}
