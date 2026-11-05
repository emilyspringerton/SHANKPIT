/* apps/game/host.c -- the clean-room SHANKPIT game host.
 *
 * What lives here and what does not:
 *   - PARENA owns every decision: the loop's timestep, movement, spawn rings, camera, projection,
 *     sky stops, the mode table, combat, shield regen, the wire's sizes and the REFLUX rows.
 *     Those rules are packages/game rules, compiled to C at build time by `make game`.
 *   - This file owns only what PARENA cannot reach on this build: SDL2's window and framebuffer
 *     (PARENA has no Bytes -- docs/PARENA_GAPS.md 5), the keyboard/mouse, and the UDP socket. It
 *     rasterises the draw plan the rules hand it and dispatches the REFLUX rows the gameplay mod
 *     names. It never computes a game number of its own.
 *
 * Modes: the game boots into ZOMBIES as the lobby (packages/game/gameplay_mod.prn's default). Enter
 * deploys into the wave defence; 2 switches to shankpit-460's arena-shooter FFA, 1 back to zombies.
 * `--host [port]` / `--join <ip> [port]` enable the UDP glue; `--headless N` runs the sim with no
 * window (used by `make game-smoke`).
 */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "event_log.h"
#include "net_transport.h"
#include "game_api.h"
#include "level_catalog.h"
#include "level_mods_host.h"
#include "reflux_runtime.h"

#define MAXB 12
#define FRAME_W 1024
#define FRAME_H 576
#define FEED_LINES 3

/* ---- wire structs: every size is asserted against the PARENA rules at startup ------------------- */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t type;
    uint8_t slot;
    uint16_t seq;
    int16_t fwd;
    int16_t strafe;
    int32_t yaw;
    uint8_t fire;
    uint8_t pad[3];
} NetCmd;

typedef struct __attribute__((packed)) {
    uint8_t id;
    uint8_t flags;
    uint8_t hp;
    uint8_t shield;
    int16_t x;
    int16_t z;
    int16_t yaw;
    uint16_t score;
} NetSnapBody;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t type;
    uint8_t mode;
    uint8_t count;
    uint8_t pad;
    uint16_t seq;
    uint16_t pad2;
    NetSnapBody body[MAXB];
} NetSnapshot;

typedef struct __attribute__((packed)) {
    uint16_t level_id;
    uint16_t mask;
    uint16_t revision;
    uint16_t pad;
} NetLevelMod;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t type;
    uint8_t count;
    uint16_t seq;
    NetLevelMod mods[LEVEL_MODS_MAX_LEVELS];
} NetLevelMods;

_Static_assert(sizeof(NetCmd) == 20, "NetCmd must match net_cmd_size()");
_Static_assert(sizeof(NetSnapBody) == 12, "NetSnapBody must match net_body_bytes()");
_Static_assert(sizeof(NetSnapshot) == 156, "NetSnapshot must match net_snapshot_size()");
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t type;
    uint8_t on;
    uint16_t level;
    uint16_t mod;
    uint16_t pad;
} NetModRequest;

_Static_assert(sizeof(NetLevelMod) == 8, "NetLevelMod must match net_level_mod_bytes()");
_Static_assert(sizeof(NetLevelMods) == 72, "NetLevelMods must match net_level_mods_size()");
_Static_assert(sizeof(NetModRequest) == 12, "NetModRequest must match net_mod_request_size()");

/* ---- bodies ------------------------------------------------------------------------------------ */
typedef struct {
    int used;
    int bot;
    int alive;
    int hp;
    int shield;
    int shield_base;
    int since_hit_ms;
    int score;
    int x, z, yaw; /* cm, milliradians */
    int respawn_at;
    int fire_at;
} Body;

static Body g_bodies[MAXB];
static int g_mode = 1;
static int g_phase; /* 0 = lobby, 1 = deployed */
static int g_wave;
static int g_wave_timer;
static int g_tick;
static int g_local_slot;
static int g_player_score;
static int g_bot_score[MAXB];
static int g_cursor; /* absolute REFLUX cursor, the host's subscriber position */
static int g_over;   /* frag limit reached */

/* The level this process is running, and the module mask it opened with. A level is itself a REFLUX
 * module (lm_mod_level), so it is registered in the same table as weather/sky rather than beside it. */
static int g_level_id = 1;
static int g_level_env = 0;

static char g_feed[FEED_LINES][48];
static int g_feed_until[FEED_LINES];

/* input, filled by SDL and read once per tick */
static int g_in_fwd, g_in_strafe, g_in_fire;

/* ---- net --------------------------------------------------------------------------------------- */
static int g_is_host, g_is_client;
static NetAddr g_peer;
static int g_peer_known;
static int g_net_acc;
static int g_net_seq;

/* A one-shot client optimization request (--request-mod), sent once the server's snapshots prove the
 * peer is live. It is only a request: the server validates it and rebroadcasts the resulting mask. */
static int g_request_mod;
static int g_request_on;
static int g_request_sent;
static int g_snapshots_received;

static void net_recv_or_send(void);
static void net_client_tick(void);

static const int CLIENT_SLOT = 11; /* 0 player, 1..10 bots, 11 the one remote client */

/* ---- framebuffer -------------------------------------------------------------------------------- */
static uint32_t g_fb[FRAME_W * FRAME_H];

/* 5x7 glyphs, bits 4..0 left to right. Only the characters the HUD uses are defined. */
static const uint8_t FONT[64][7] = {
    [' ' - 32] = {0, 0, 0, 0, 0, 0, 0},
    ['-' - 32] = {0, 0, 0, 0x1f, 0, 0, 0},
    ['.' - 32] = {0, 0, 0, 0, 0, 0x0c, 0x0c},
    [':' - 32] = {0, 0x04, 0, 0, 0, 0x04, 0},
    ['/' - 32] = {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10},
    ['0' - 32] = {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e},
    ['1' - 32] = {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e},
    ['2' - 32] = {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f},
    ['3' - 32] = {0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e},
    ['4' - 32] = {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02},
    ['5' - 32] = {0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e},
    ['6' - 32] = {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e},
    ['7' - 32] = {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    ['8' - 32] = {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e},
    ['9' - 32] = {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c},
    ['A' - 32] = {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},
    ['B' - 32] = {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e},
    ['C' - 32] = {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e},
    ['D' - 32] = {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e},
    ['E' - 32] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f},
    ['F' - 32] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10},
    ['G' - 32] = {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0e},
    ['H' - 32] = {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},
    ['I' - 32] = {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e},
    ['J' - 32] = {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c},
    ['K' - 32] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
    ['L' - 32] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f},
    ['M' - 32] = {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11},
    ['N' - 32] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
    ['O' - 32] = {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},
    ['P' - 32] = {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10},
    ['Q' - 32] = {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d},
    ['R' - 32] = {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11},
    ['S' - 32] = {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e},
    ['T' - 32] = {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    ['U' - 32] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},
    ['V' - 32] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04},
    ['W' - 32] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11},
    ['X' - 32] = {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11},
    ['Y' - 32] = {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04},
    ['Z' - 32] = {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f},
};

/* ---- pixel helpers ------------------------------------------------------------------------------ */
static uint32_t rgb(int packed) { return 0xff000000u | (uint32_t)packed; }

static uint32_t shade_color(uint32_t c, int pct) {
    int r = (int)((c >> 16) & 0xff) * pct / 100;
    int g = (int)((c >> 8) & 0xff) * pct / 100;
    int b = (int)(c & 0xff) * pct / 100;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return 0xff000000u | (uint32_t)(r << 16) | (uint32_t)(g << 8) | (uint32_t)b;
}

static void px(int x, int y, uint32_t c) {
    if (x < 0 || x >= FRAME_W || y < 0 || y >= FRAME_H) return;
    g_fb[y * FRAME_W + x] = c;
}

static void rect(int x, int y, int w, int h, uint32_t c) {
    int j;
    if (w <= 0 || h <= 0) return;
    if (x + w <= 0 || x >= FRAME_W || y + h <= 0 || y >= FRAME_H) return;
    for (j = 0; j < h; j++) {
        int i;
        int yy = y + j;
        if (yy < 0 || yy >= FRAME_H) continue;
        for (i = 0; i < w; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= FRAME_W) continue;
            g_fb[yy * FRAME_W + xx] = c;
        }
    }
}

static void line(int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = x1 - x0, dy = y1 - y0;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    int err, e2;
    dx = dx < 0 ? -dx : dx;
    dy = dy < 0 ? -dy : dy;
    err = (dx > dy ? dx : -dy) / 2;
    for (;;) {
        px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        e2 = err;
        if (e2 > -dx) { err -= dy; x0 += sx; }
        if (e2 < dy) { err += dx; y0 += sy; }
    }
}

static void draw_text(int x, int y, int scale, uint32_t color, const char *s) {
    for (; *s; s++) {
        int ch = (unsigned char)*s;
        int row;
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch < 32 || ch > 95) { x += 6 * scale; continue; }
        for (row = 0; row < 7; row++) {
            int bits = FONT[ch - 32][row];
            int col;
            for (col = 0; col < 5; col++) {
                if (bits & (1 << (4 - col))) {
                    rect(x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
        x += 6 * scale;
    }
}

/* ---- bodies ------------------------------------------------------------------------------------ */
static Body *local_body(void) { return &g_bodies[g_local_slot]; }

static int bot_count_alive(void) {
    int i, n = 0;
    for (i = 0; i < MAXB; i++) {
        if (g_bodies[i].used && g_bodies[i].bot && g_bodies[i].alive) n++;
    }
    return n;
}

static void spawn_body(int slot, int bot, int x, int z, int yaw, int hp, int shield) {
    Body *b;
    if (slot < 0 || slot >= MAXB) return;
    b = &g_bodies[slot];
    memset(b, 0, sizeof(*b));
    b->used = 1;
    b->bot = bot;
    b->alive = 1;
    b->hp = hp;
    b->shield = shield;
    b->shield_base = shield;
    b->x = x;
    b->z = z;
    b->yaw = yaw;
}

static void spawn_wave(int wave) {
    int n = gp_mode_wave_size(gp_mode_zombies(), wave);
    int radius = gp_mode_spawn_radius(gp_mode_zombies());
    int hp = gp_mode_wave_hp(gp_mode_zombies(), wave);
    int i;
    for (i = 1; i < MAXB - 1; i++) {
        g_bodies[i].used = 0;
        g_bodies[i].alive = 0;
    }
    /* A 100-degree arc, not a full ring: a wave walks in from the direction the player faces. */
    for (i = 0; i < n; i++) {
        int slot = i + 1;
        int x = loop_arc_x(i, n, radius, 100);
        int z = loop_arc_z(i, n, radius, 100);
        spawn_body(slot, 1, x, z, loop_face_yaw(x, z, 0, -700), hp, 0);
    }
    g_wave = wave;
}

/* ---- REFLUX: the host is the dispatcher; the mod only names the rows ---------------------------- */
static void feed_push(const char *text, int ticks) {
    int i;
    for (i = FEED_LINES - 1; i > 0; i--) {
        memcpy(g_feed[i], g_feed[i - 1], sizeof(g_feed[i]));
        g_feed_until[i] = g_feed_until[i - 1];
    }
    snprintf(g_feed[0], sizeof(g_feed[0]), "%s", text);
    g_feed_until[0] = g_tick + ticks;
}

static void apply_damage(int victim, int dmg, int attacker, int weapon) {
    Body *v;
    int killed, dealt, after_shield, after_hp, before_hp, before_shield;
    if (victim < 0 || victim >= MAXB) return;
    v = &g_bodies[victim];
    if (!v->used || !v->alive) return;

    before_hp = v->hp;
    before_shield = v->shield;
    killed = gp_hit_killed(v->hp, v->shield, dmg, 0);
    after_shield = gp_hit_shield(v->shield, dmg, 0);
    after_hp = gp_hit_health(v->hp, v->shield, dmg, 0);
    dealt = combat_dmg_dealt(before_hp, after_hp) + (before_shield - after_shield);

    v->hp = after_hp;
    v->shield = after_shield;
    v->shield_base = after_shield;
    v->since_hit_ms = 0;

    reflux_host_dispatch(gp_evt_hit(), attacker, victim, dealt);
    if (killed) {
        v->alive = 0;
        v->respawn_at = g_tick + gp_mode_respawn_ticks(g_mode, gp_mode_hp(g_mode));
        reflux_host_dispatch(gp_evt_ko(), victim, attacker, weapon);
    }
}

static void poll_events(void) {
    int total;
    /* The level's own subscriber: updates addressed to this level land here, whatever dispatched them. */
    level_mods_sync(g_level_id);
    total = reflux_host_total_dispatched();
    int oldest = reflux_host_oldest_abs();
    if (g_cursor < oldest) g_cursor = oldest;
    while (g_cursor < total) {
        int idx = reflux_host_index_for_abs(g_cursor);
        int t, a, b;
        if (idx < 0) { g_cursor = total; break; }
        t = reflux_host_action_type_at(idx);
        a = reflux_host_action_a_at(idx);
        b = reflux_host_action_b_at(idx);
        if (t == gp_evt_ko()) {
            char msg[48];
            if (b == g_local_slot) {
                g_player_score += gp_kill_score(g_mode, 55, 0);
                snprintf(msg, sizeof(msg), "KO %d  +%d", a, gp_kill_score(g_mode, 55, 0));
            } else {
                if (b >= 0 && b < MAXB) g_bot_score[b] += 1;
                snprintf(msg, sizeof(msg), "KO %d BY %d", a, b);
            }
            feed_push(msg, 240);
        } else if (t == gp_evt_mode()) {
            if (net_valid_mode(b)) g_mode = b;
        }
        g_cursor++;
    }
}

/* ---- tick -------------------------------------------------------------------------------------- */
static void respawn_local(void) {
    Body *p = local_body();
    int x = loop_ring_x(0, 8, 380);
    int z = loop_ring_z(0, 8, 380);
    p->alive = 1;
    p->hp = gp_mode_hp(g_mode);
    p->shield = gp_mode_shield(g_mode);
    p->shield_base = p->shield;
    p->since_hit_ms = 0;
    p->x = x;
    p->z = z;
    p->yaw = loop_face_yaw(x, z, 0, 0);
}

static void bot_tick(int i) {
    Body *b = &g_bodies[i];
    Body *p = local_body();
    int dx, dz;
    if (!b->used || !b->bot) return;
    if (!b->alive) {
        if (g_mode == gp_mode_ffa() && g_tick >= b->respawn_at) {
            int x = loop_ring_x(i, 11, gp_mode_spawn_radius(g_mode));
            int z = loop_ring_z(i, 11, gp_mode_spawn_radius(g_mode));
            spawn_body(i, 1, x, z, loop_face_yaw(x, z, p->x, p->z), gp_mode_hp(g_mode),
                       gp_mode_shield(g_mode));
        }
        return;
    }
    if (!p->alive) return;

    dx = p->x - b->x;
    dz = p->z - b->z;
    b->yaw = loop_face_yaw(b->x, b->z, p->x, p->z);
    if (dx * dx + dz * dz > 130 * 130) {
        b->x = loop_move_x(b->x, b->yaw, 45, 0);
        b->z = loop_move_z(b->z, b->yaw, 45, 0);
    } else if (g_tick >= b->fire_at) {
        /* a shambler swings slowly: ~45 ticks between hits, 12 damage, so three of them cannot
         * delete a full health+shield bar before the player has time to answer */
        b->fire_at = g_tick + 45;
        apply_damage(g_local_slot, 12, i, 2);
    }
}

static void update_round(void) {
    int i;
    Body *p = local_body();

    g_tick++;

    /* shield regen: the rule is idempotent in "ms since the hit", so the base held at that hit is
     * what is fed back in -- feeding the current shield would compound the ramp. */
    for (i = 0; i < MAXB; i++) {
        Body *b = &g_bodies[i];
        if (!b->used || !b->alive) continue;
        b->since_hit_ms += loop_tick_ms();
        if (b->shield < shield_max()) {
            int next = gp_regen_shield(b->shield_base, b->since_hit_ms);
            if (next > shield_max()) next = shield_max();
            b->shield = next;
        }
    }

    /* local movement */
    if (p->alive) {
        p->x = loop_move_x(p->x, p->yaw, g_in_fwd, g_in_strafe);
        p->z = loop_move_z(p->z, p->yaw, g_in_fwd, g_in_strafe);
    } else if (g_tick >= p->respawn_at) {
        respawn_local();
    }

    if (g_is_client) return;

    /* bots */
    for (i = 1; i < MAXB; i++) bot_tick(i);

    /* firing */
    if (g_in_fire && p->alive && g_tick >= p->fire_at) {
        int best = -1, bestd = 1600 * 1600;
        for (i = 1; i < MAXB; i++) {
            Body *b = &g_bodies[i];
            double f, r;
            int d2;
            if (!b->used || !b->alive) continue;
            f = rp_fwd(b->x - p->x, b->z - p->z, p->yaw);
            r = rp_right(b->x - p->x, b->z - p->z, p->yaw);
            if (f < 30.0 || f > 1600.0) continue;
            if (fabs(r) > 70.0 + f * 0.06) continue;
            d2 = (b->x - p->x) * (b->x - p->x) + (b->z - p->z) * (b->z - p->z);
            if (d2 < bestd) { bestd = d2; best = i; }
        }
        if (best >= 0) {
            p->fire_at = g_tick + 6;
            apply_damage(best, gp_weapon_damage(2), g_local_slot, 2);
        } else {
            p->fire_at = g_tick + 6;
        }
    }

    /* wave flow: zombies only */
    if (g_mode == gp_mode_zombies() && p->alive && bot_count_alive() == 0) {
        if (g_wave_timer == 0) {
            g_wave_timer = g_tick + gp_mode_wave_break_ms(g_mode) / loop_tick_ms();
        } else if (g_tick >= g_wave_timer) {
            g_wave_timer = 0;
            spawn_wave(g_wave + 1);
        }
    }

    if (g_mode == gp_mode_ffa() && gp_mode_frag_limit(g_mode) > 0 &&
        g_player_score >= gp_mode_frag_limit(g_mode)) {
        g_over = 1;
    }
}

/* ---- networking -------------------------------------------------------------------------------- */
static void net_open(int bind_port) {
    if (net_transport_open(bind_port) != 0) {
        fprintf(stderr, "shank_game: transport open (bind %d) failed\n", bind_port);
        exit(1);
    }
}

static void net_send_snapshot(void) {
    NetSnapshot s;
    int i;
    memset(&s, 0, sizeof(s));
    s.magic = (uint32_t)net_magic();
    s.type = (uint8_t)net_packet_snapshot();
    s.mode = (uint8_t)g_mode;
    s.seq = (uint16_t)(g_net_seq++);
    for (i = 0; i < MAXB; i++) {
        Body *b = &g_bodies[i];
        NetSnapBody *nb = &s.body[i];
        if (!b->used) continue;
        nb->id = (uint8_t)i;
        nb->flags = (uint8_t)((b->alive ? net_body_flags_alive() : 0) |
                              (b->bot ? net_body_flags_bot() : 0));
        nb->hp = (uint8_t)b->hp;
        nb->shield = (uint8_t)b->shield;
        nb->x = (int16_t)net_quant(b->x);
        nb->z = (int16_t)net_quant(b->z);
        nb->yaw = (int16_t)(b->yaw / 10);
        nb->score = (uint16_t)b->score;
        s.count = (uint8_t)(i + 1);
    }
    if (g_peer_known) net_transport_send(&g_peer, &s, (int)sizeof(s));
}

/* Broadcast every live level's mask. Absolute state, not a delta: the client may miss any packet and
 * still be correct after the next one, and a client joining late needs no extra handshake. */
static void net_send_level_mods(void) {
    NetLevelMods m;
    int i;

    memset(&m, 0, sizeof(m));
    m.magic = (uint32_t)net_magic();
    m.type = (uint8_t)net_packet_level_mods();
    m.seq = (uint16_t)(g_net_seq++);
    for (i = 0; i < LEVEL_MODS_MAX_LEVELS && m.count < net_max_level_mods(); i++) {
        int level_id = level_mods_level_at(i);
        if (level_id <= 0 || level_id > 65535) continue;
        m.mods[m.count].level_id = (uint16_t)level_id;
        m.mods[m.count].mask = (uint16_t)level_mods_mask(level_id);
        m.mods[m.count].revision = (uint16_t)level_mods_revision(level_id);
        m.count++;
    }
    if (g_peer_known && m.count > 0) net_transport_send(&g_peer, &m, (int)sizeof(m));
}

static void net_host_recv(void) {
    for (;;) {
        /* Two real client datagrams arrive here: a 20-byte command and a 12-byte mod request. Both
         * share magic+type at the same offsets, so one buffer is inspected and then size-checked. */
        union {
            NetCmd cmd;
            NetModRequest req;
        } pkt;
        NetAddr from;
        int n = net_transport_recv(&from, &pkt, (int)sizeof(pkt));
        if (n < 0) return;
        if (n < (int)sizeof(uint32_t) || (int)pkt.cmd.magic != net_magic()) continue;

        if (pkt.cmd.type == net_packet_mod_request()) {
            if (n != (ssize_t)sizeof(NetModRequest)) continue;
            if (pkt.req.on > 1) continue;
            if (level_mods_env((int)pkt.req.level) < 0) continue; /* only a live level */
            /* Server authority: this applies through the same validated path a server-side update
             * uses (mandatory modules refused by level_mods_request via level_mods_set). */
            level_mods_set((int)pkt.req.level, (int)pkt.req.mod, (int)pkt.req.on);
            continue;
        }

        if (pkt.cmd.type != net_packet_cmd() || n != (ssize_t)sizeof(NetCmd)) continue;
        g_peer = from;
        g_peer_known = 1;

        if (!g_bodies[CLIENT_SLOT].used) {
            spawn_body(CLIENT_SLOT, 0, loop_ring_x(2, 8, 620), loop_ring_z(2, 8, 620), 0,
                       gp_mode_hp(g_mode), gp_mode_shield(g_mode));
        }
        {
            Body *b = &g_bodies[CLIENT_SLOT];
            int fwd = pkt.cmd.fwd, strafe = pkt.cmd.strafe;
            b->yaw = pkt.cmd.yaw;
            if (b->alive) {
                b->x = loop_move_x(b->x, b->yaw, fwd, strafe);
                b->z = loop_move_z(b->z, b->yaw, fwd, strafe);
            }
            if (pkt.cmd.fire && b->alive && g_tick >= b->fire_at) {
                int i, best = -1, bestd = 1600 * 1600;
                b->fire_at = g_tick + 6;
                for (i = 1; i < MAXB; i++) {
                    int d2;
                    double f, r;
                    Body *o = &g_bodies[i];
                    if (!o->used || !o->alive || i == CLIENT_SLOT) continue;
                    f = rp_fwd(o->x - b->x, o->z - b->z, b->yaw);
                    r = rp_right(o->x - b->x, o->z - b->z, b->yaw);
                    if (f < 30.0 || f > 1600.0 || fabs(r) > 70.0 + f * 0.06) continue;
                    d2 = (o->x - b->x) * (o->x - b->x) + (o->z - b->z) * (o->z - b->z);
                    if (d2 < bestd) { bestd = d2; best = i; }
                }
                if (best >= 0) apply_damage(best, gp_weapon_damage(2), CLIENT_SLOT, 2);
            }
        }
    }
}

/* One call per simulation tick, whatever role this process is: the host drains client commands and
 * broadcasts at 30 Hz, a client sends its own command and applies whatever snapshot has arrived. */
static void net_recv_or_send(void) {
    if (!net_transport_is_open()) return;
    if (g_is_host) {
        net_host_recv();
        g_net_acc += loop_tick_ms();
        if (g_net_acc >= net_tick_ms()) {
            g_net_acc -= net_tick_ms();
            net_send_snapshot();
            net_send_level_mods();
        }
    } else if (g_is_client) {
        net_client_tick();
    }
}

static void net_client_tick(void) {
    NetCmd cmd;
    Body *p = local_body();
    /* One receive buffer, two real packet shapes. The header is identical up to `type`, so the tag is
     * read once and the rest of the datagram is only trusted after its own exact size is checked. */
    union {
        NetSnapshot snap;
        NetLevelMods mods;
    } pkt;
    NetAddr from;

    memset(&cmd, 0, sizeof(cmd));
    cmd.magic = (uint32_t)net_magic();
    cmd.type = (uint8_t)net_packet_cmd();
    cmd.slot = (uint8_t)CLIENT_SLOT;
    cmd.seq = (uint16_t)(g_net_seq++);
    cmd.fwd = (int16_t)g_in_fwd;
    cmd.strafe = (int16_t)g_in_strafe;
    cmd.yaw = p->yaw;
    cmd.fire = (uint8_t)(g_in_fire ? 1 : 0);
    net_transport_send(&g_peer, &cmd, (int)sizeof(cmd));

    /* One-shot: only after a snapshot proves the server has us as its peer. */
    if (g_request_mod && !g_request_sent && g_snapshots_received > 0) {
        NetModRequest req;
        memset(&req, 0, sizeof(req));
        req.magic = (uint32_t)net_magic();
        req.type = (uint8_t)net_packet_mod_request();
        req.level = (uint16_t)g_level_id;
        req.mod = (uint16_t)g_request_mod;
        req.on = (uint8_t)(g_request_on ? 1 : 0);
        net_transport_send(&g_peer, &req, (int)sizeof(req));
        g_request_sent = 1;
    }

    for (;;) {
        int n = net_transport_recv(&from, &pkt, (int)sizeof(pkt));
        int i;
        if (n < 0) break;
        if (n < (int)sizeof(uint32_t) || (int)pkt.snap.magic != net_magic()) continue;

        if (pkt.snap.type == net_packet_level_mods()) {
            if (n != (ssize_t)sizeof(NetLevelMods)) continue;
            if (!net_valid_level_count(pkt.mods.count)) continue;
            for (i = 0; i < pkt.mods.count && i < net_max_level_mods(); i++) {
                NetLevelMod *row = &pkt.mods.mods[i];
                if ((int)row->level_id != g_level_id) continue;
                if (level_mods_apply_state(g_level_id, row->mask, row->revision)) {
                    feed_push("LEVEL MODS UPDATED", 180);
                }
            }
            continue;
        }

        if (pkt.snap.type != net_packet_snapshot()) continue;
        if (n != (ssize_t)sizeof(NetSnapshot)) continue;
        if (!net_valid_mode(pkt.snap.mode)) continue;
        {
            NetSnapshot *s = &pkt.snap;
            g_snapshots_received++;
            g_mode = s->mode;
            g_phase = 1;
            for (i = 0; i < MAXB; i++) {
                NetSnapBody *nb = &s->body[i];
                Body *b = &g_bodies[i];
                if (i >= s->count) break;
                if (nb->id != i) continue;
                if (i == g_local_slot) {
                    if (nb->x != 0 || nb->z != 0) {
                        int dx = b->x - nb->x, dz = b->z - nb->z;
                        if (dx * dx + dz * dz > 250 * 250) { b->x = nb->x; b->z = nb->z; }
                    }
                    if (!(nb->flags & net_body_flags_alive())) p->alive = 0;
                    else p->alive = 1;
                    p->hp = nb->hp;
                    p->shield = nb->shield;
                    continue;
                }
                b->used = 1;
                b->bot = (nb->flags & net_body_flags_bot()) ? 1 : 0;
                b->alive = (nb->flags & net_body_flags_alive()) ? 1 : 0;
                b->hp = nb->hp;
                b->shield = nb->shield;
                b->shield_base = nb->shield;
                b->x = nb->x;
                b->z = nb->z;
                b->yaw = nb->yaw * 10;
                b->score = nb->score;
            }
        }
    }
}

/* ---- render ------------------------------------------------------------------------------------ */
typedef struct { int id; double f; } DrawSort;

/* A grid line has one end behind the camera more often than not (the camera stands inside the
 * arena), so the segment is clipped at the near plane before it is projected. Without this the
 * whole perspective half of the grid silently vanished. */
static void world_line(int camx, int camz, int yaw, int x0, int z0, int x1, int z1, uint32_t c) {
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
    line(rp_screen_x(x0 - camx, z0 - camz, yaw), rp_feet_y(x0 - camx, z0 - camz, yaw),
         rp_screen_x(x1 - camx, z1 - camz, yaw), rp_feet_y(x1 - camx, z1 - camz, yaw), c);
}

static void draw_floor_grid(int camx, int camz, int yaw) {
    int g;
    int half = loop_arena_half();
    uint32_t c = shade_color(rgb(floor_color()), rp_floor_shade(900.0));
    for (g = -half; g <= half; g += 200) {
        world_line(camx, camz, yaw, g, -half, g, half, c);
        world_line(camx, camz, yaw, -half, g, half, g, c);
    }
}

static void draw_pillars(int camx, int camz, int yaw) {
    DrawSort order[16];
    int i, j;
    for (i = 0; i < 16; i++) {
        int x = loop_ring_x(i, 16, loop_arena_half() - 20);
        int z = loop_ring_z(i, 16, loop_arena_half() - 20);
        order[i].id = i;
        order[i].f = rp_fwd(x - camx, z - camz, yaw);
    }
    for (i = 0; i < 16; i++) {
        for (j = i + 1; j < 16; j++) {
            if (order[j].f > order[i].f) {
                DrawSort t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
        }
    }
    for (i = 0; i < 16; i++) {
        int id = order[i].id;
        int x = loop_ring_x(id, 16, loop_arena_half() - 20);
        int z = loop_ring_z(id, 16, loop_arena_half() - 20);
        int dx = x - camx, dz = z - camz;
        double f = rp_fwd(dx, dz, yaw);
        int sx, feet, h, w;
        if (!rp_visible(f)) continue;
        sx = rp_screen_x(dx, dz, yaw);
        feet = rp_feet_y(dx, dz, yaw);
        h = rp_body_h(f, loop_wall_height());
        w = rp_body_w(f, loop_wall_height());
        if (h < 2) continue;
        rect(sx - w / 2, feet - h, w, h, shade_color(rgb(wall_color()), 85));
        rect(sx - w / 2, feet - h, w, 3, shade_color(rgb(wall_color()), 130));
    }
}

static void draw_bodies(int camx, int camz, int yaw) {
    DrawSort order[MAXB];
    int n = 0, i, j;
    for (i = 0; i < MAXB; i++) {
        Body *b = &g_bodies[i];
        if (!b->used || i == g_local_slot) continue;
        order[n].id = i;
        order[n].f = rp_fwd(b->x - camx, b->z - camz, yaw);
        n++;
    }
    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n; j++) {
            if (order[j].f > order[i].f) {
                DrawSort t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
        }
    }
    for (i = 0; i < n; i++) {
        Body *b = &g_bodies[order[i].id];
        int dx = b->x - camx, dz = b->z - camz;
        double f = rp_fwd(dx, dz, yaw);
        int sx, feet, h, w, bar;
        uint32_t col;
        if (!rp_visible(f)) continue;
        sx = rp_screen_x(dx, dz, yaw);
        feet = rp_feet_y(dx, dz, yaw);
        h = rp_body_h(f, 180);
        w = rp_body_w(f, 180);
        if (h < 3) continue;
        col = rgb(player_hue(order[i].id % 10));
        if (!b->alive) col = shade_color(col, 30);
        rect(sx - w / 2, feet - h, w, h, col);
        rect(sx - w / 2, feet - h, w, h / 5, shade_color(col, 165));
        if (!b->alive) continue;

        /* health bar above the head, shield as a thin cyan line above that */
        bar = w * 2;
        if (bar < 18) bar = 18;
        rect(sx - bar / 2, feet - h - 9, bar, 3, 0xff301818);
        rect(sx - bar / 2, feet - h - 9, bar * b->hp / 100, 3, 0xff40e040);
        if (b->shield > 0) {
            rect(sx - bar / 2, feet - h - 13, bar * b->shield / 100, 2, 0xff60d0ff);
        }
        if (b->bot) {
            int tag_w = 9;
            rect(sx - tag_w / 2, feet - h - 22, tag_w, 3, 0xffd0b040);
        }
    }
}

static const char *mode_title(int mode) {
    return mode == gp_mode_ffa() ? "460 FFA" : "ZOMBIES";
}

static void draw_hud(void) {
    char buf[96];
    Body *p = local_body();
    int i;

    draw_text(16, 14, rp_hud_scale(), rgb(0xe8dcc0), mode_title(g_mode));
    {
        const char *level_name = level_catalog_name(g_level_id);
        if (level_name[0]) draw_text(16, 64, 1, rgb(0x807666), level_name);
    }
    if (g_phase == 0) {
        draw_text(16, 40, 1, rgb(0x9a8f7a), "LOBBY");
    } else if (g_mode == gp_mode_zombies()) {
        snprintf(buf, sizeof(buf), "WAVE %d  SCORE %d", g_wave + 1, g_player_score);
        draw_text(16, 40, 1, rgb(0xc8bca4), buf);
    } else {
        snprintf(buf, sizeof(buf), "FRAGS %d/%d", g_player_score, gp_mode_frag_limit(g_mode));
        draw_text(16, 40, 1, rgb(0xc8bca4), buf);
    }

    /* health + shield, bottom left */
    snprintf(buf, sizeof(buf), "HP %d", p->hp);
    draw_text(16, FRAME_H - 58, 2, rgb(0xe06060), buf);
    snprintf(buf, sizeof(buf), "SHIELD %d", p->shield);
    draw_text(16, FRAME_H - 28, 1, rgb(0x60c8ff), buf);
    rect(16, FRAME_H - 40, 180, 6, 0xff301818);
    rect(16, FRAME_H - 40, 180 * (p->hp < 0 ? 0 : p->hp) / 100, 6, 0xffe04040);
    rect(16, FRAME_H - 18, 180 * (p->shield < 0 ? 0 : p->shield) / 100, 4, 0xff60c8ff);

    if (p->shield < shield_max() && !p->alive) {
        draw_text(16, FRAME_H - 80, 1, rgb(0x9a8f7a), "DOWN");
    }

    /* kill feed, top right */
    for (i = 0; i < FEED_LINES; i++) {
        if (g_feed_until[i] > g_tick) {
            draw_text(FRAME_W - 210, 14 + i * 16, 1, rgb(0xd0b080), g_feed[i]);
        }
    }

    /* bottom right: net state */
    if (g_is_host) {
        draw_text(FRAME_W - 150, FRAME_H - 24, 1, rgb(0x60d080), "HOST 7777");
    } else if (g_is_client) {
        draw_text(FRAME_W - 190, FRAME_H - 24, 1, rgb(0x60c8ff), "CLIENT 11");
    }

    /* crosshair */
    {
        uint32_t c = rgb(0xf0f0e0);
        rect(FRAME_W / 2 - 10, FRAME_H / 2, 7, 2, c);
        rect(FRAME_W / 2 + 3, FRAME_H / 2, 7, 2, c);
        rect(FRAME_W / 2, FRAME_H / 2 - 10, 2, 7, c);
        rect(FRAME_W / 2, FRAME_H / 2 + 3, 2, 7, c);
    }

    /* gun, first person */
    rect(FRAME_W - 230, FRAME_H - 120, 120, 26, rgb(0x2e3238));
    rect(FRAME_W - 190, FRAME_H - 150, 34, 40, rgb(0x3c424a));

    if (g_phase == 0) {
        const char *prompt = "PRESS ENTER TO DEPLOY";
        int w = (int)strlen(prompt) * 12;
        draw_text(FRAME_W / 2 - w / 2, FRAME_H / 2 + 70, 2, rgb(0xe8dcc0), prompt);
        draw_text(FRAME_W / 2 - 150, FRAME_H / 2 + 106, 1, rgb(0x9a8f7a),
                  "1 ZOMBIES   2 460 FFA");
    } else if (g_over) {
        draw_text(FRAME_W / 2 - 90, FRAME_H / 2 + 70, 2, rgb(0xe8dcc0), "MATCH OVER");
    } else if (!p->alive) {
        int left = p->respawn_at - g_tick;
        if (left < 0) left = 0;
        snprintf(buf, sizeof(buf), "DOWN  %d", left / 60);
        draw_text(FRAME_W / 2 - 60, FRAME_H / 2 + 70, 2, rgb(0xe06060), buf);
    }
}

/* The weather-render mod's real work: 40 deterministic streaks. No RNG and no state, so a level with
 * the module off draws exactly zero pixels and the same tick always produces the same drizzle. */
static void draw_weather(void) {
    int i;
    for (i = 0; i < 40; i++) {
        int x = (i * 97 + g_tick * 3) % FRAME_W;
        int y = (i * 53 + g_tick * 7) % rp_horizon();
        int j;
        for (j = 0; j < 9; j++) px(x + j / 3, y + j, rgb(0x8fa6b8));
    }
}

static void draw_frame(void) {
    Body *p = local_body();
    int camx = p->x, camz = p->z, yaw = p->yaw;
    int y, x;
    uint32_t floor_c = rgb(floor_color());
    uint32_t sky_c[3];
    sky_c[0] = rgb(sky_horizon());
    sky_c[1] = rgb(sky_mid());
    sky_c[2] = rgb(sky_top());

    for (y = 0; y < FRAME_H; y++) {
        uint32_t c;
        if (y < rp_horizon()) {
            /* No sky module: an underground/moon ceiling, not a sky that happens to be dark. */
            c = level_mods_sky_enabled(g_level_id) ? sky_c[rp_sky_stop(y)] : rgb(0x101216);
        } else {
            c = floor_c;
            if ((y - rp_horizon()) % 64 == 0) c = shade_color(c, 92);
        }
        for (x = 0; x < FRAME_W; x++) g_fb[y * FRAME_W + x] = c;
    }

    draw_floor_grid(camx, camz, yaw);
    draw_pillars(camx, camz, yaw);
    if (level_mods_weather_enabled(g_level_id)) draw_weather();
    draw_bodies(camx, camz, yaw);
    draw_hud();
}

/* ---- lobby ------------------------------------------------------------------------------------- */
/* The lobby is the arena itself with a few bodies standing in it, not a menu screen: the mode it
 * previews is the mode the game boots into (zombies), and 2 switches the population to FFA's five. */
static void init_lobby(void) {
    int i;
    int n = (g_mode == gp_mode_ffa()) ? 5 : 3;
    int spread = (g_mode == gp_mode_ffa()) ? 110 : 70;
    memset(g_bodies, 0, sizeof(g_bodies));
    g_phase = 0;
    g_over = 0;
    g_player_score = 0;
    g_wave = 0;
    g_local_slot = 0;
    spawn_body(0, 0, 0, -520, 0, gp_mode_hp(g_mode), gp_mode_shield(g_mode));
    for (i = 0; i < n; i++) {
        int x = loop_arc_x(i, n, 700, spread);
        int z = loop_arc_z(i, n, 700, spread);
        spawn_body(i + 1, 1, x, z, loop_face_yaw(x, z, 0, -520),
                   gp_mode_wave_hp(gp_mode_zombies(), 0), 0);
    }
}

static void select_mode(int mode) {
    int old = g_mode;
    if (!gp_mode_valid(mode)) return;
    if (g_phase == 1) init_lobby(); /* a mode pick mid-round returns to the lobby first */
    g_mode = mode;
    reflux_host_dispatch(gp_evt_mode(), old, mode, 0);
}

static void start_round(void) {
    int i;
    memset(g_bodies, 0, sizeof(g_bodies));
    memset(g_bot_score, 0, sizeof(g_bot_score));
    g_phase = 1;
    g_over = 0;
    g_wave = 0;
    g_wave_timer = 0;
    g_player_score = 0;
    g_local_slot = g_is_client ? CLIENT_SLOT : 0;
    spawn_body(g_local_slot, 0, 0, -700, 0, gp_mode_hp(g_mode), gp_mode_shield(g_mode));
    /* Flush anything that happened before the round (startup registration, a --set-mod) before the
     * reset empties the shared log; otherwise the audit stream would start mid-story. */
    event_log_sync(g_tick);
    reflux_host_reset();
    g_cursor = 0;
    event_log_reset_cursor();
    /* The log just emptied, so re-publish the level's full state before the round's own rows: a late
     * reader must be able to reconstruct which mods this level runs without a reload. */
    level_mods_publish(g_level_id);
    reflux_host_dispatch(gp_evt_mode(), 0, g_mode, 0);

    if (g_is_client) return;
    if (g_mode == gp_mode_zombies()) {
        spawn_wave(0);
    } else {
        int n = gp_mode_bots(g_mode);
        int radius = gp_mode_spawn_radius(g_mode);
        for (i = 0; i < n; i++) {
            int x = loop_arc_x(i, n, radius, 140);
            int z = loop_arc_z(i, n, radius, 140);
            spawn_body(i + 1, 1, x, z, loop_face_yaw(x, z, 0, -700),
                       gp_mode_hp(g_mode), gp_mode_shield(g_mode));
        }
    }
}

/* ---- main -------------------------------------------------------------------------------------- */
static void handle_key(SDL_Keycode key) {
    switch (key) {
        case SDLK_ESCAPE: exit(0);
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (g_phase == 0 || g_over) start_round();
            break;
        case SDLK_1: select_mode(gp_mode_zombies()); break;
        case SDLK_2: select_mode(gp_mode_ffa()); break;
        case SDLK_r:
            if (g_phase == 1 && !local_body()->alive) respawn_local();
            break;
        default: break;
    }
}

int main(int argc, char **argv) {
    int i;
    int headless = 0;
    int frames = 0;
    int start_now = 0;
    const char *shot = NULL;
    const char *join_ip = NULL;
    int port = net_port();
    int boot_mode = gp_mode_default();
    const char *levels_path = NULL;
    const char *event_log_path = NULL;
    const char *replay_path = NULL;
    int level_id = 0; /* 0 = unspecified: the manifest's first level, or level 1 without one */
    int level_env = lm_env_surface();
    int set_mod = 0;
    int set_on = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--headless") == 0 && i + 1 < argc) {
            headless = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            shot = argv[++i];
        } else if (strcmp(argv[i], "--start") == 0) {
            start_now = 1;
        } else if (strcmp(argv[i], "--ffa") == 0) {
            boot_mode = gp_mode_ffa();
        } else if (strcmp(argv[i], "--host") == 0) {
            g_is_host = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-') port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--join") == 0 && i + 1 < argc) {
            g_is_client = 1;
            join_ip = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--levels") == 0 && i + 1 < argc) {
            levels_path = argv[++i];
        } else if (strcmp(argv[i], "--event-log") == 0 && i + 1 < argc) {
            event_log_path = argv[++i];
        } else if (strcmp(argv[i], "--replay-events") == 0 && i + 1 < argc) {
            replay_path = argv[++i];
        } else if (strcmp(argv[i], "--request-mod") == 0 && i + 2 < argc) {
            g_request_mod = atoi(argv[++i]);
            g_request_on = atoi(argv[++i]) != 0;
        } else if (strcmp(argv[i], "--capabilities") == 0 && i + 1 < argc) {
            /* A client that has only ported some modules declares its mask before registration. */
            level_mods_set_capabilities(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--level") == 0 && i + 1 < argc) {
            level_id = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--env") == 0 && i + 1 < argc) {
            const char *v = argv[++i];
            if (strcmp(v, "surface") == 0) level_env = lm_env_surface();
            else if (strcmp(v, "underground") == 0) level_env = lm_env_underground();
            else if (strcmp(v, "moon") == 0) level_env = lm_env_moon();
            else level_env = atoi(v);
        } else if (strcmp(argv[i], "--set-mod") == 0 && i + 2 < argc) {
            set_mod = atoi(argv[++i]);
            set_on = atoi(argv[++i]) != 0;
        } else {
            fprintf(stderr, "usage: %s [--host [port]] [--join ip [port]] [--start] [--ffa]"
                            " [--levels manifest.json] [--level N] [--event-log events.jsonl]"
                            " [--replay-events events.jsonl] [--request-mod MOD 0|1]"
                            " [--env surface|underground|moon] [--set-mod MOD 0|1]"
                            " [--capabilities MASK]"
                            " [--headless N] [--frames N --shot out.bmp]\n", argv[0]);
            return 2;
        }
    }

    /* The rules own the numbers; the wire structs and the palette are checked before a frame. */
    if (palette_check() != 0) {
        fprintf(stderr, "shank_game: 460 palette check failed\n");
        return 2;
    }
    if (net_cmd_size() != (int)sizeof(NetCmd) || net_snapshot_size() != (int)sizeof(NetSnapshot) ||
        net_body_bytes() != (int)sizeof(NetSnapBody) ||
        net_level_mod_bytes() != (int)sizeof(NetLevelMod) ||
        net_level_mods_size() != (int)sizeof(NetLevelMods) ||
        net_mod_request_size() != (int)sizeof(NetModRequest)) {
        fprintf(stderr, "shank_game: wire struct size disagrees with net_rules.prn\n");
        return 2;
    }
    g_mode = boot_mode;
    /* Open the stream before any level row exists, so startup registration and any --set-mod are
     * part of the audit trail instead of being skipped by the "no replay of earlier rows" rule. */
    if (event_log_path && !event_log_open(event_log_path)) {
        fprintf(stderr, "shank_game: cannot open event log %s\n", event_log_path);
        return 2;
    }
    level_mods_reset();
    level_catalog_reset();
    if (levels_path) {
        int n = level_catalog_load(levels_path);
        if (n <= 0) {
            fprintf(stderr, "shank_game: %s: level manifest rejected (%d)\n", levels_path, n);
            return 2;
        }
        if (level_id == 0) level_id = level_catalog_at(0)->id;
        else if (!level_catalog_find(level_id)) {
            fprintf(stderr, "shank_game: level %d is not in %s\n", level_id, levels_path);
            return 2;
        }
        g_level_env = level_mods_env(level_id);
    } else {
        if (level_id == 0) level_id = 1;
        if (level_mods_register(level_id, level_env) == 0) {
            fprintf(stderr, "shank_game: bad --level %d / --env %d (level > 0, env 0..2)\n",
                    level_id, level_env);
            return 2;
        }
        g_level_env = level_env;
    }
    g_level_id = level_id;
    if (replay_path) {
        int replayed = event_log_replay(replay_path);
        if (replayed < 0) {
            fprintf(stderr, "shank_game: cannot replay event log %s\n", replay_path);
            return 2;
        }
        printf("replayed %d level mod row(s) from %s\n", replayed, replay_path);
    }
    if (set_mod && !level_mods_set(g_level_id, set_mod, set_on)) {
        fprintf(stderr, "shank_game: --set-mod %d %d refused (mandatory, unknown, or no change)\n",
                set_mod, set_on);
    }
    init_lobby();

    if (g_is_client) {
        if (!net_transport_addr(join_ip, port, &g_peer)) {
            fprintf(stderr, "shank_game: bad join address %s\n", join_ip);
            return 2;
        }
        g_peer_known = 1;
        net_open(0);
        start_round();
    } else if (g_is_host) {
        net_open(port);
    }

    if (headless) {
        int t;
        if (!g_phase) start_round();
        for (t = 0; t < headless; t++) {
            g_in_fwd = 0;
            g_in_strafe = 0;
            g_in_fire = (t % 20 == 0) ? 1 : 0;
            update_round();
            net_recv_or_send();
            poll_events();
            event_log_sync(g_tick);
            /* A real socket needs real time on both ends; the other headless paths are pure sim. */
            if (g_is_host) SDL_Delay(8);
            else if (g_is_client) SDL_Delay(16);
        }
        printf("headless %d ticks: mode=%d %s=%d score=%d hp=%d shield=%d bodies=%d events=%d"
               " level=%d env=%d mods=%d rev=%d snapshots=%d peer=%d level_name=%s\n",
               headless, g_mode, g_mode == gp_mode_zombies() ? "wave" : "frags",
               g_mode == gp_mode_zombies() ? g_wave + 1 : g_player_score,
               g_player_score, local_body()->hp, local_body()->shield, bot_count_alive(),
               reflux_host_total_dispatched(), g_level_id, g_level_env,
               level_mods_mask(g_level_id), level_mods_revision(g_level_id),
               g_snapshots_received, g_peer_known, level_catalog_name(g_level_id));
        net_transport_close();
        event_log_close();
        return 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "shank_game: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    {
        SDL_Window *win = SDL_CreateWindow("SHANKPIT -- PARENA clean room",
                                            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                            FRAME_W, FRAME_H, SDL_WINDOW_SHOWN);
        SDL_Renderer *ren;
        SDL_Texture *tex;
        int running = 1;
        Uint32 last;
        int acc = 0;
        int frame_count = 0;
        int mouse_look = 1;

        if (!win) {
            fprintf(stderr, "shank_game: window: %s\n", SDL_GetError());
            return 1;
        }
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                FRAME_W, FRAME_H);
        if (!ren || !tex) {
            fprintf(stderr, "shank_game: renderer: %s\n", SDL_GetError());
            return 1;
        }
        SDL_SetRelativeMouseMode(SDL_TRUE);

        if (start_now) start_round();
        last = SDL_GetTicks();

        while (running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) running = 0;
                else if (e.type == SDL_KEYDOWN) {
                    if (e.key.keysym.sym == SDLK_F1) { mouse_look = !mouse_look;
                        SDL_SetRelativeMouseMode(mouse_look ? SDL_TRUE : SDL_FALSE); }
                    else handle_key(e.key.keysym.sym);
                } else if (e.type == SDL_MOUSEMOTION && mouse_look) {
                    local_body()->yaw += e.motion.xrel * 4;
                    if (local_body()->yaw > 3141) local_body()->yaw -= 6283;
                    if (local_body()->yaw < -3141) local_body()->yaw += 6283;
                } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                    g_in_fire = 1;
                } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                    g_in_fire = 0;
                }
            }
            {
                const Uint8 *keys = SDL_GetKeyboardState(NULL);
                g_in_fwd = (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP] ? 100 : 0) -
                           (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN] ? 100 : 0);
                g_in_strafe = (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT] ? 100 : 0) -
                              (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT] ? 100 : 0);
            }

            {
                Uint32 now = SDL_GetTicks();
                int dt = (int)(now - last);
                last = now;
                if (dt > 100) dt = 100;
                acc += dt;
                {
                    int steps = 0;
                    while (acc >= loop_tick_ms() && steps < loop_max_steps()) {
                        if (g_phase == 1) update_round();
                        net_recv_or_send();
                        acc -= loop_tick_ms();
                        steps++;
                    }
                    if (acc > loop_tick_ms()) acc = loop_tick_ms();
                }
            }
            poll_events();
            event_log_sync(g_tick);
            draw_frame();
            SDL_UpdateTexture(tex, NULL, g_fb, FRAME_W * 4);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, NULL, NULL);
            SDL_RenderPresent(ren);
            frame_count++;

            if (shot && frames > 0 && frame_count >= frames) {
                SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(g_fb, FRAME_W, FRAME_H, 32, FRAME_W * 4,
                                                             0x00ff0000, 0x0000ff00, 0x000000ff,
                                                             0xff000000);
                if (surf) {
                    SDL_SaveBMP(surf, shot);
                    SDL_FreeSurface(surf);
                }
                printf("wrote %s after %d frames (mode=%d phase=%d)\n", shot, frame_count, g_mode,
                       g_phase);
                running = 0;
            }
            SDL_Delay(1);
        }
        SDL_DestroyTexture(tex);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
    }
    net_transport_close();
    event_log_close();
    SDL_Quit();
    return 0;
}
