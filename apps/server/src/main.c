// apps/server/src/main.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif

#include "../../../packages/common/protocol.h"
#include "../../../packages/common/physics.h"
#include "../../../packages/common/shared_movement.h"
#include "../../../packages/common/net_sim.h"
#include "../../../packages/simulation/local_game.h"
#include "../../../packages/world/level_boxes.h"

/* cutscene handshake globals — defined in lobby/main.c for the client;
   server sim uses local_game.h but never renders cutscenes, so stub to 0. */
int g_story_cutscene_done   = 0;
int g_story_outro_requested = 0;

#include "server_mode.h"
#include "server_state.h"

#ifndef NET_VERBOSE_LOG
#define NET_VERBOSE_LOG 0
#endif
#ifndef NET_LOG_HANDSHAKE
#define NET_LOG_HANDSHAKE 1
#endif
#ifndef NET_LOG_SNAPSHOT
#define NET_LOG_SNAPSHOT 0
#endif
#ifndef NET_LOG_USERCMD
#define NET_LOG_USERCMD 0
#endif
#ifndef NET_LOG_TIMEOUT
#define NET_LOG_TIMEOUT 1
#endif

#if NET_VERBOSE_LOG
#define NET_CLIENT_LOG(fmt, ...) printf("[NET_CLIENT] " fmt "\n", ##__VA_ARGS__)
#define NET_SERVER_LOG(fmt, ...) printf("[NET_SERVER] " fmt "\n", ##__VA_ARGS__)
#define NET_WARN_LOG(fmt, ...)   printf("[NET_WARN] " fmt "\n", ##__VA_ARGS__)
#define NET_ERR_LOG(fmt, ...)    printf("[NET_ERR] " fmt "\n", ##__VA_ARGS__)
#define NET_SUMMARY_LOG(fmt, ...) printf("[NET_SUMMARY] " fmt "\n", ##__VA_ARGS__)
#else
#define NET_CLIENT_LOG(fmt, ...)
#define NET_SERVER_LOG(fmt, ...)
#define NET_WARN_LOG(fmt, ...)
#define NET_ERR_LOG(fmt, ...)
#define NET_SUMMARY_LOG(fmt, ...)
#endif

int sock = -1;
struct sockaddr_in bind_addr;
unsigned int client_last_seq[MAX_CLIENTS];

typedef struct {
    int active;
    int welcomed;
    int cmd_seen;
    struct sockaddr_in addr;
    double last_heard;
    int player_id;
} ClientSlot;

static ClientSlot slots[MAX_CLIENTS];

typedef struct {
    int enabled;
    FILE *file;
    int target_id;
    float cam_x;
    float cam_y;
    float cam_z;
    float cam_yaw;
    float cam_pitch;
    float cam_zoom;
} RecorderState;

static RecorderState recorder = {0};
#define HELI_NET_DEBUG 0

typedef struct {
    unsigned int connects;
    unsigned int welcomes;
    unsigned int snapshots_tx;
    unsigned int snapshot_ents_total;
    unsigned int usercmd_pkts;
    unsigned int usercmds_applied;
    unsigned int stale_cmds;
    unsigned int malformed;
    unsigned int last_summary_ms;
    unsigned int first_snapshot_logged[MAX_CLIENTS];
    unsigned int connect_ms[MAX_CLIENTS];
    unsigned int first_usercmd_pkt_seen[MAX_CLIENTS];
    unsigned int stale_warn_last_ms[MAX_CLIENTS];
} NetServerDiag;

static NetServerDiag g_net_diag;

#define SERVER_SNAPSHOT_INTERVAL_TICKS 2
#define SERVER_DM_FRAG_LIMIT 25
#define SERVER_DM_ROUND_MS (6 * 60 * 1000)
#define TDMO_TEAM_SIZE 6
#define TDMO_SCORE_LIMIT 25
// S459-43, founder real-time (same message as S459-41's "change default queue map to NEWPIT"
// ask): "we need to add a timer to the game mode." QUEUE has no map rotation of its own (one
// admin-flagged level, S459-41) so it gets its own round length/frag cap rather than reusing
// SERVER_DM_ROUND_MS/SERVER_DM_FRAG_LIMIT -- shorter than a DM round since QUEUE is the FFA
// bot-league lane, meant for quick, frequent matches rather than long DM sessions.
#define SERVER_QUEUE_FRAG_LIMIT 20
#define SERVER_QUEUE_ROUND_MS (4 * 60 * 1000)

static const int g_dm_rotation[] = { SCENE_STADIUM, SCENE_VOXWORLD, SCENE_OIL_TANKER, SCENE_POO_POO_ISLAND };
static int g_dm_rotation_idx = 0;
static int g_server_match_scene = SCENE_GARAGE_OSAKA;
static unsigned int g_round_start_ms = 0;
static int g_fast_forward = 0; // S459-48 -- see --fast-forward's own doc comment in main() for the real rationale
static int g_tdmo_tie_breaker = 0;

#define RECORDER_SHAKE_POS 0.08f
#define RECORDER_SHAKE_ANGLE 0.35f
#define RECORDER_SMOOTH_POS 0.08f
#define RECORDER_SMOOTH_ANGLE 0.18f
#define RECORDER_NORTH_X 0.0f
#define RECORDER_NORTH_Y 6.5f
#define RECORDER_NORTH_Z -32.0f

unsigned int get_server_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned int)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static double now_seconds(void) {
    return (double)get_server_time() / 1000.0;
}

static int net_should_log_every(unsigned int *last_ms, unsigned int interval_ms, unsigned int now_ms) {
    if (now_ms - *last_ms < interval_ms) return 0;
    *last_ms = now_ms;
    return 1;
}

static void net_format_addr(const struct sockaddr_in *addr, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!addr) return;
    char ip_buf[INET_ADDRSTRLEN] = {0};
    if (!inet_ntop(AF_INET, &addr->sin_addr, ip_buf, sizeof(ip_buf))) {
        snprintf(ip_buf, sizeof(ip_buf), "?.?.?.?");
    }
    snprintf(out, out_sz, "%s:%d", ip_buf, ntohs(addr->sin_port));
}

static void net_server_emit_summary(unsigned int now_ms) {
    if (!net_should_log_every(&g_net_diag.last_summary_ms, 1000, now_ms)) return;
    unsigned int avg_ents = g_net_diag.snapshots_tx ? (g_net_diag.snapshot_ents_total / g_net_diag.snapshots_tx) : 0;
    (void)avg_ents;
    int clients = 0;
    for (int i = 1; i < MAX_CLIENTS; i++) {
        if (slots[i].active && slots[i].welcomed) clients++;
    }
    NET_SUMMARY_LOG("SERVER clients=%d connects=%u welcomes=%u snapshots_tx=%u avg_ents=%u usercmd_pkts=%u usercmds_applied=%u stale_cmds=%u malformed=%u",
                    clients, g_net_diag.connects, g_net_diag.welcomes, g_net_diag.snapshots_tx, avg_ents,
                    g_net_diag.usercmd_pkts, g_net_diag.usercmds_applied, g_net_diag.stale_cmds, g_net_diag.malformed);
    g_net_diag.connects = 0;
    g_net_diag.welcomes = 0;
    g_net_diag.snapshots_tx = 0;
    g_net_diag.snapshot_ents_total = 0;
    g_net_diag.usercmd_pkts = 0;
    g_net_diag.usercmds_applied = 0;
    g_net_diag.stale_cmds = 0;
    g_net_diag.malformed = 0;
}

static int server_scene_is_dm_map(int scene_id) {
    return scene_id == SCENE_STADIUM || scene_id == SCENE_VOXWORLD ||
           scene_id == SCENE_OIL_TANKER || scene_id == SCENE_POO_POO_ISLAND;
}

static int server_scene_heli_count(int scene_id) {
    int count = 0;
    for (int i = 0; i < MAX_HELICOPTERS; i++) {
        if (local_state.helicopters[i].active && local_state.helicopters[i].scene_id == scene_id) count++;
    }
    return count;
}

static void server_advance_dm_rotation(unsigned int now_ms) {
    g_dm_rotation_idx = (g_dm_rotation_idx + 1) % (int)(sizeof(g_dm_rotation) / sizeof(g_dm_rotation[0]));
    g_server_match_scene = g_dm_rotation[g_dm_rotation_idx];
    scene_load(g_server_match_scene);
    g_round_start_ms = now_ms;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        PlayerState *p = &local_state.players[i];
        if (!p->active) continue;
        p->kills = 0;
        p->deaths = 0;
        p->kill_streak = 0; // S459-52 -- a new round starts every multikill streak fresh
        p->last_kill_time_ms = 0;
        p->state = STATE_ALIVE;
        p->health = 100;
        p->shield = 100;
    }
    if (g_server_match_scene == SCENE_VOXWORLD) {
        printf("[HELI] authoritative voxworld spawn count=%d\n", server_scene_heli_count(SCENE_VOXWORLD));
    }
    printf("[ROUND] next_map=%d rotation_idx=%d\n", g_server_match_scene, g_dm_rotation_idx);
}

// server_advance_queue_round -- S459-43. Same quiet-reset shape as server_advance_dm_rotation
// (no explicit "match over" pause/scoreboard state -- matches this codebase's own existing DM
// precedent of just resetting stats and continuing), deliberately NOT reloading the level: QUEUE
// always plays the one admin-flagged default level (S459-41), so re-running queue_activate_match
// here would just be a wasted registry round-trip against the same level every round.
static void server_advance_queue_round(unsigned int now_ms) {
    g_round_start_ms = now_ms;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        PlayerState *p = &local_state.players[i];
        if (!p->active) continue;
        p->kills = 0;
        p->deaths = 0;
        p->kill_streak = 0; // S459-52 -- a new round starts every multikill streak fresh
        p->last_kill_time_ms = 0;
        p->state = STATE_ALIVE;
        p->health = 100;
        p->shield = 100;
    }
    printf("[ROUND] queue_round_reset\n");
}

static int addr_equal(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

static int server_team_mode_enabled(int mode) {
    return mode == MODE_TDM || mode == MODE_TDMB || mode == MODE_TDMO || mode == MODE_CTF || mode == MODE_CTFB || mode == MODE_CTFO;
}

static int tdmo_bot_slot_available(int slot) {
    return slot > 0 && slot < MAX_CLIENTS && !slots[slot].active && !local_state.players[slot].active;
}

static int tdmo_find_free_bot_slot(void) {
    for (int i = MAX_CLIENTS - 1; i >= 1; i--) {
        if (tdmo_bot_slot_available(i)) return i;
    }
    return -1;
}

static int tdmo_human_count_on_team(int team_id) {
    int count = 0;
    for (int i = 1; i < MAX_CLIENTS; i++) {
        if (!slots[i].active || !slots[i].welcomed) continue;
        PlayerState *p = &local_state.players[i];
        if (!p->active || p->is_bot) continue;
        if (p->team_id == team_id) count++;
    }
    return count;
}

static int tdmo_total_count_on_team(int team_id) {
    int count = 0;
    for (int i = 1; i < MAX_CLIENTS; i++) {
        PlayerState *p = &local_state.players[i];
        if (!p->active) continue;
        if (p->team_id == team_id) count++;
    }
    return count;
}

static int tdmo_choose_join_team(void) {
    int blue_humans = tdmo_human_count_on_team(TDMB_BLUE_TEAM);
    int red_humans = tdmo_human_count_on_team(TDMB_RED_TEAM);
    if (blue_humans != red_humans) {
        return (blue_humans < red_humans) ? TDMB_BLUE_TEAM : TDMB_RED_TEAM;
    }
    int blue_total = tdmo_total_count_on_team(TDMB_BLUE_TEAM);
    int red_total = tdmo_total_count_on_team(TDMB_RED_TEAM);
    if (blue_total != red_total) {
        return (blue_total < red_total) ? TDMB_BLUE_TEAM : TDMB_RED_TEAM;
    }
    int team = (g_tdmo_tie_breaker++ & 1) ? TDMB_RED_TEAM : TDMB_BLUE_TEAM;
    return team;
}

static int tdmo_remove_one_bot_from_team(int team_id) {
    for (int i = MAX_CLIENTS - 1; i >= 1; i--) {
        PlayerState *p = &local_state.players[i];
        if (!p->active || !p->is_bot || p->team_id != team_id) continue;
        memset(p, 0, sizeof(*p));
        p->id = i;
        p->team_id = -1;
        return 1;
    }
    return 0;
}

static int tdmo_spawn_bot_on_team(int team_id, unsigned int now_ms) {
    int slot = tdmo_find_free_bot_slot();
    if (slot == -1) return 0;
    PlayerState *p = &local_state.players[slot];
    memset(p, 0, sizeof(*p));
    p->id = slot;
    p->active = 1;
    p->is_bot = 1;
    p->team_id = team_id;
    p->scene_id = g_server_match_scene;
    p->state = STATE_ALIVE;
    p->health = 100;
    p->shield = 100;
    p->current_weapon = WPN_AR;
    p->ammo[WPN_AR] = WPN_STATS[WPN_AR].ammo_max;
    init_genome(&p->brain);
    phys_respawn(p, now_ms);
    return 1;
}

static void tdmo_fill_team_to_target(int team_id, unsigned int now_ms) {
    while (tdmo_total_count_on_team(team_id) < TDMO_TEAM_SIZE) {
        if (!tdmo_spawn_bot_on_team(team_id, now_ms)) break;
    }
}

static void tdmo_ensure_population(unsigned int now_ms) {
    tdmo_fill_team_to_target(TDMB_BLUE_TEAM, now_ms);
    tdmo_fill_team_to_target(TDMB_RED_TEAM, now_ms);
}

static void tdmo_activate_match(unsigned int now_ms) {
    local_init_match(1, MODE_TDMO);
    g_server_match_scene = SCENE_VOXWORLD;
    scene_load(g_server_match_scene);
    local_state.game_mode = MODE_TDMO;
    local_state.score_limit = TDMO_SCORE_LIMIT;
    local_state.team_scores[TDMB_BLUE_TEAM] = 0;
    local_state.team_scores[TDMB_RED_TEAM] = 0;
    local_state.match_over = 0;
    local_state.winning_team = -1;
    local_state.players[0].active = 0;
    g_tdmo_tie_breaker = 0;
    tdmo_ensure_population(now_ms);
}

// server_apply_custom_level -- the real box/material extraction + phys_set_custom_level* apply
// sequence, factored out of main()'s own real --level CLI handling (below) so S459-38's own real
// QUEUE-level-loading logic (queue_activate_match, right below) can share it instead of
// duplicating ~25 lines of the same real box/material array-of-struct unpacking.
static void server_apply_custom_level(const CustomLevelData *lvl) {
    float x[LEVEL_BOXES_MAX], y[LEVEL_BOXES_MAX], z[LEVEL_BOXES_MAX];
    float w[LEVEL_BOXES_MAX], h[LEVEL_BOXES_MAX], d[LEVEL_BOXES_MAX];
    float r[LEVEL_BOXES_MAX], g[LEVEL_BOXES_MAX], b[LEVEL_BOXES_MAX];
    int material_idx[LEVEL_BOXES_MAX];
    for (int bi = 0; bi < lvl->count; bi++) {
        x[bi] = lvl->boxes[bi].x; y[bi] = lvl->boxes[bi].y; z[bi] = lvl->boxes[bi].z;
        w[bi] = lvl->boxes[bi].w; h[bi] = lvl->boxes[bi].h; d[bi] = lvl->boxes[bi].d;
        r[bi] = lvl->boxes[bi].r; g[bi] = lvl->boxes[bi].g; b[bi] = lvl->boxes[bi].b;
        material_idx[bi] = lvl->boxes[bi].material_idx;
    }
    char mat_names[LEVEL_BOXES_MAX_MATERIALS][CUSTOM_LEVEL_MATERIAL_NAME_LEN];
    char mat_shaders[LEVEL_BOXES_MAX_MATERIALS][CUSTOM_LEVEL_MATERIAL_NAME_LEN];
    float mat_specular[LEVEL_BOXES_MAX_MATERIALS], mat_shininess[LEVEL_BOXES_MAX_MATERIALS];
    for (int mi = 0; mi < lvl->material_count; mi++) {
        strncpy(mat_names[mi], lvl->materials[mi].name, CUSTOM_LEVEL_MATERIAL_NAME_LEN - 1);
        mat_names[mi][CUSTOM_LEVEL_MATERIAL_NAME_LEN - 1] = '\0';
        strncpy(mat_shaders[mi], lvl->materials[mi].shader_name, CUSTOM_LEVEL_MATERIAL_NAME_LEN - 1);
        mat_shaders[mi][CUSTOM_LEVEL_MATERIAL_NAME_LEN - 1] = '\0';
        mat_specular[mi] = lvl->materials[mi].specular;
        mat_shininess[mi] = lvl->materials[mi].shininess;
    }
    phys_set_custom_level_materials(mat_names, mat_shaders, mat_specular, mat_shininess, lvl->material_count);
    phys_set_custom_level(x, y, z, w, h, d, r, g, b, material_idx, lvl->count, lvl->ground_plane_enabled, lvl->ground_plane_squares);

    // S459-58: real, author-placed spawn points, team/FFA-aware.
    float sp_x[LEVEL_BOXES_MAX_SPAWNERS], sp_y[LEVEL_BOXES_MAX_SPAWNERS], sp_z[LEVEL_BOXES_MAX_SPAWNERS];
    int sp_team[LEVEL_BOXES_MAX_SPAWNERS];
    for (int si = 0; si < lvl->spawner_count; si++) {
        sp_x[si] = lvl->spawners[si].x; sp_y[si] = lvl->spawners[si].y; sp_z[si] = lvl->spawners[si].z;
        sp_team[si] = lvl->spawners[si].team;
    }
    phys_set_custom_level_spawners(sp_x, sp_y, sp_z, sp_team, lvl->spawner_count);

    g_server_match_scene = SCENE_CUSTOM_LEVEL;
    scene_load(g_server_match_scene);
}

// queue_activate_match -- S459-34, the real MODE_QUEUE match activation. Deliberately much
// smaller than tdmo_activate_match: no teams, no score-limit bookkeeping, no in-process bot
// population call -- MODE_QUEUE's own population is filled entirely by real, external, packet-
// level bot client processes connecting exactly like a human would (ops/shankpit-bot-pool.sh),
// never an in-process PlayerState puppet, so there is no server-side "ensure population" step to
// call here at all. Reuses the generic non-TDMO connect path every other free-for-all mode
// (MODE_DEATHMATCH/MODE_CTF) already goes through for per-player setup.
//
// S459-38, founder real-time: "DEFAULT QUEUE TO USE THE LEVEL CALLE 44 (NEW FUNCTIONALITY
// QUEUEING INTO LEVELS WITH BOTS)" / "default it to the oil tanker if no online levels
// available." S459-41, same real thread: "need to add an option to shankpit levels to set a
// level as default for queue" -- replaced the original hardcoded-name lookup ("44") with a real,
// admin-settable `is_default_queue` flag on the level registry itself (LevelRegistryEntry, see
// level_boxes.h). Whichever level is currently flagged wins -- no game-side rebuild needed to
// change the default, just a real admin action in NOCK's own level editor. Falls back to
// SCENE_OIL_TANKER (bypassing the normal g_dm_rotation choice entirely, matching the founder's
// own explicit instruction) on ANY real failure along the way -- registry unreachable, no level
// currently flagged as default, or the export fetch/parse itself failing -- never a half-loaded
// level.
static void queue_activate_match(unsigned int now_ms) {
    local_init_match(1, MODE_QUEUE);
    local_state.game_mode = MODE_QUEUE;
    local_state.match_over = 0;
    local_state.players[0].active = 0;
    g_round_start_ms = now_ms;

    LevelRegistryEntry entries[LEVEL_REGISTRY_MAX_ENTRIES];
    int count = level_boxes_fetch_registry_list(entries, LEVEL_REGISTRY_MAX_ENTRIES);
    int found_id = -1;
    for (int i = 0; i < count; i++) {
        if (entries[i].is_default_queue) { found_id = entries[i].id; break; }
    }
    CustomLevelData lvl;
    if (found_id >= 0 && level_boxes_fetch_export(found_id, &lvl)) {
        server_apply_custom_level(&lvl);
        NET_SERVER_LOG("QUEUE_LEVEL_LOADED name=%s id=%d boxes=%d", lvl.name, found_id, lvl.count);
        return;
    }
    NET_SERVER_LOG("QUEUE_LEVEL_FALLBACK reason=%s -- using SCENE_OIL_TANKER",
                   found_id < 0 ? "level_44_not_found" : "export_fetch_failed");
    g_server_match_scene = SCENE_OIL_TANKER;
    scene_load(g_server_match_scene);
}

static int find_slot_by_addr(const struct sockaddr_in *addr) {
    for (int i = 1; i < MAX_CLIENTS; i++) {
        if (slots[i].active && addr_equal(&slots[i].addr, addr)) {
            return i;
        }
    }
    return -1;
}

static int alloc_slot(const struct sockaddr_in *addr) {
    for (int i = 1; i < MAX_CLIENTS; i++) {
        if (!slots[i].active) {
            memset(&local_state.players[i], 0, sizeof(PlayerState));
            local_state.players[i].id = i;
            local_state.players[i].scene_id = g_server_match_scene;
            local_state.players[i].active = 0;
            phys_respawn(&local_state.players[i], get_server_time());
            local_state.players[i].yaw = 0.0f;
            local_state.players[i].pitch = 0.0f;

            slots[i].active = 1;
            slots[i].welcomed = 0;
            slots[i].cmd_seen = 0;
            slots[i].addr = *addr;
            slots[i].last_heard = now_seconds();
            slots[i].player_id = i;

            local_state.clients[i] = *addr;
            local_state.client_meta[i].active = 0;
            local_state.client_meta[i].last_heard_ms = get_server_time();
            client_last_seq[i] = 0;
            g_net_diag.first_snapshot_logged[i] = 0;
            g_net_diag.first_usercmd_pkt_seen[i] = 0;
            g_net_diag.stale_warn_last_ms[i] = 0;
            g_net_diag.connect_ms[i] = get_server_time();

            return i;
        }
    }
    return -1;
}

static void free_slot(int slot) {
    if (slot <= 0 || slot >= MAX_CLIENTS) return;
    int was_human = local_state.players[slot].active && !local_state.players[slot].is_bot;
    int prev_team = local_state.players[slot].team_id;
    for (int i = 0; i < MAX_HELICOPTERS; i++) {
        if (local_state.helicopters[i].active && local_state.helicopters[i].occupant_player_id == slot) {
            local_state.helicopters[i].occupant_player_id = -1;
        }
    }
    for (int i = 0; i < MAX_BUGGIES; i++) {
        if (local_state.buggies[i].active && local_state.buggies[i].occupant_player_id == slot) {
            local_state.buggies[i].occupant_player_id = -1;
        }
    }
    slots[slot].active = 0;
    slots[slot].welcomed = 0;
    slots[slot].cmd_seen = 0;
    memset(&slots[slot].addr, 0, sizeof(struct sockaddr_in));
    slots[slot].last_heard = 0.0;
    slots[slot].player_id = -1;
    g_net_diag.first_snapshot_logged[slot] = 0;
    g_net_diag.first_usercmd_pkt_seen[slot] = 0;
    g_net_diag.stale_warn_last_ms[slot] = 0;
    g_net_diag.connect_ms[slot] = 0;
    NET_SERVER_LOG("SLOT_FREE client_id=%d", slot);
    server_disconnect(slot, client_last_seq);
    if (local_state.game_mode == MODE_TDMO && was_human && team_id_is_valid(prev_team)) {
        tdmo_spawn_bot_on_team(prev_team, get_server_time());
        tdmo_ensure_population(get_server_time());
    }
}

static HelicopterState *server_find_nearby_heli(int scene_id, float x, float y, float z, float radius) {
    float rr = radius * radius;
    for (int i = 0; i < MAX_HELICOPTERS; i++) {
        HelicopterState *h = &local_state.helicopters[i];
        if (!h->active || h->scene_id != scene_id) continue;
        float dx = h->x - x, dy = h->y - y, dz = h->z - z;
        if ((dx * dx + dy * dy + dz * dz) <= rr) return h;
    }
    return NULL;
}

static int server_try_exit_heli(PlayerState *p, HelicopterState *h) {
    float yaw_rad = -h->yaw * 0.0174533f;
    float rx = cosf(yaw_rad), rz = sinf(yaw_rad);
    float bx = -sinf(yaw_rad), bz = cosf(yaw_rad);
    float ox[3] = { rx * g_heli_tuning.exit_offset, -rx * g_heli_tuning.exit_offset, bx * g_heli_tuning.exit_offset };
    float oz[3] = { rz * g_heli_tuning.exit_offset, -rz * g_heli_tuning.exit_offset, bz * g_heli_tuning.exit_offset };
    for (int i = 0; i < 3; i++) {
        float px = h->x + ox[i], pz = h->z + oz[i];
        if (!heli_point_collides(px, h->y + 1.0f, pz) && !heli_point_collides(px, h->y + 2.0f, pz)) {
            p->x = px; p->y = h->y; p->z = pz;
            p->in_vehicle = 0;
            p->vehicle_type = VEH_NONE;
            h->occupant_player_id = -1;
            return 1;
        }
    }
    return 0;
}

static void send_welcome(const struct sockaddr_in *addr, int client_id) {
    unsigned int now = get_server_time();
    char packet[sizeof(NetHeader) + 1];
    NetHeader *h = (NetHeader*)packet;
    h->type = PACKET_WELCOME;
    h->client_id = (unsigned char)client_id;
    h->sequence = 0;
    h->timestamp = now;
    h->entity_count = 0;
    h->scene_id = (unsigned char)local_state.players[client_id].scene_id;
    packet[sizeof(NetHeader)] = (unsigned char)(local_state.game_mode & 0xFF);
    sendto(sock, packet, sizeof(packet), 0,
           (const struct sockaddr*)addr, sizeof(struct sockaddr_in));
    if (client_id > 0 && client_id < MAX_CLIENTS) {
        slots[client_id].welcomed = 1;
        g_net_diag.welcomes++;
        char addr_buf[64];
        net_format_addr(addr, addr_buf, sizeof(addr_buf));
        NET_SERVER_LOG("WELCOME_TX client_id=%d scene_id=%d dst=%s", client_id, (int)h->scene_id, addr_buf);
    }
}

static int ensure_slot_for_sender(const struct sockaddr_in *sender) {
    int slot = find_slot_by_addr(sender);
    if (slot != -1) {
        slots[slot].last_heard = now_seconds();
        local_state.client_meta[slot].last_heard_ms = get_server_time();
        char addr_buf[64];
        net_format_addr(sender, addr_buf, sizeof(addr_buf));
        NET_SERVER_LOG("SLOT_REUSE client_id=%d addr=%s", slot, addr_buf);
        return slot;
    }

    int new_slot = alloc_slot(sender);
    if (new_slot != -1) {
        char addr_buf[64];
        net_format_addr(sender, addr_buf, sizeof(addr_buf));
        NET_SERVER_LOG("SLOT_ASSIGN client_id=%d addr=%s reason=new_sender", new_slot, addr_buf);
        g_net_diag.connects++;
    } else {
        char addr_buf[64];
        net_format_addr(sender, addr_buf, sizeof(addr_buf));
        NET_WARN_LOG("SLOT_FULL addr=%s", addr_buf);
    }
    return new_slot;
}

static int recorder_pick_target() {
    if (recorder.target_id >= 0 && recorder.target_id < MAX_CLIENTS) {
        if (local_state.players[recorder.target_id].active) {
            return recorder.target_id;
        }
    }
    int best_id = -1;
    int best_kills = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!local_state.players[i].active) continue;
        if (local_state.players[i].kills > best_kills) {
            best_kills = local_state.players[i].kills;
            best_id = i;
        }
    }
    return best_id;
}

static float recorder_compute_zoom(float dist) {
    if (dist > 120.0f) return 3.0f;
    if (dist > 70.0f) return 2.4f;
    if (dist > 35.0f) return 1.8f;
    return 1.2f;
}

static void recorder_init_file(const char *path) {
    if (!recorder.enabled) return;
    recorder.file = fopen(path, "w");
    if (!recorder.file) {
        printf("[REC] Failed to open recording file: %s\n", path);
        recorder.enabled = 0;
        return;
    }
    recorder.cam_x = RECORDER_NORTH_X;
    recorder.cam_y = RECORDER_NORTH_Y;
    recorder.cam_z = RECORDER_NORTH_Z;
    recorder.cam_yaw = 0.0f;
    recorder.cam_pitch = 0.0f;
    recorder.cam_zoom = 1.2f;
    fprintf(recorder.file, "; SHANKPIT Recorder v1 (Lisp-ASM)\n");
    fprintf(recorder.file, "(begin-recording :dt-ms 16 :north-start '(%.2f %.2f %.2f))\n",
            RECORDER_NORTH_X, RECORDER_NORTH_Y, RECORDER_NORTH_Z);
}

static void recorder_update_camera() {
    int target_id = recorder_pick_target();
    if (target_id < 0) return;
    PlayerState *target = &local_state.players[target_id];

    float desired_x = RECORDER_NORTH_X + target->x * 0.15f;
    float desired_y = RECORDER_NORTH_Y + target->y * 0.1f;
    float desired_z = RECORDER_NORTH_Z + target->z * 0.15f;

    recorder.cam_x += (desired_x - recorder.cam_x) * RECORDER_SMOOTH_POS;
    recorder.cam_y += (desired_y - recorder.cam_y) * RECORDER_SMOOTH_POS;
    recorder.cam_z += (desired_z - recorder.cam_z) * RECORDER_SMOOTH_POS;

    float dx = target->x - recorder.cam_x;
    float dy = (target->y + 2.0f) - recorder.cam_y;
    float dz = target->z - recorder.cam_z;
    float dist = sqrtf(dx * dx + dz * dz);
    float target_yaw = atan2f(dx, dz) * (180.0f / 3.14159f);
    float target_pitch = atan2f(dy, dist) * (180.0f / 3.14159f);

    recorder.cam_yaw += (target_yaw - recorder.cam_yaw) * RECORDER_SMOOTH_ANGLE;
    recorder.cam_pitch += (target_pitch - recorder.cam_pitch) * RECORDER_SMOOTH_ANGLE;

    float zoom = recorder_compute_zoom(dist);
    if (target->current_weapon == WPN_SNIPER) {
        zoom += 0.4f;
    }
    float shake_pos = RECORDER_SHAKE_POS / zoom;
    float shake_ang = RECORDER_SHAKE_ANGLE / zoom;

    recorder.cam_x += phys_rand_f() * shake_pos;
    recorder.cam_y += phys_rand_f() * shake_pos;
    recorder.cam_z += phys_rand_f() * shake_pos;
    recorder.cam_yaw += phys_rand_f() * shake_ang;
    recorder.cam_pitch += phys_rand_f() * shake_ang;
    recorder.cam_zoom = zoom;
}

static void recorder_write_frame(unsigned int tick, unsigned int now_ms) {
    if (!recorder.enabled || !recorder.file) return;
    recorder_update_camera();

    fprintf(recorder.file, "(frame :tick %u :time-ms %u\n", tick, now_ms);
    fprintf(recorder.file, "  (camera :x %.3f :y %.3f :z %.3f :yaw %.2f :pitch %.2f :zoom %.2f :mode \"handicam\")\n",
            recorder.cam_x, recorder.cam_y, recorder.cam_z,
            recorder.cam_yaw, recorder.cam_pitch, recorder.cam_zoom);


    for (int i = 0; i < MAX_CLIENTS; i++) {
        PlayerState *p = &local_state.players[i];
        if (!p->active) continue;
        fprintf(recorder.file,
                "  (actor :id %d :x %.3f :y %.3f :z %.3f :vx %.3f :vy %.3f :vz %.3f :yaw %.2f :pitch %.2f :weapon %d :state %d)\n",
                i, p->x, p->y, p->z, p->vx, p->vy, p->vz, p->yaw, p->pitch, p->current_weapon, p->state);
    }
    fprintf(recorder.file, ")\n");
    if (tick % 60 == 0) {
        fflush(recorder.file);
    }
}

int parse_server_mode(int argc, char **argv) {
    int mode = MODE_DEATHMATCH;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tdm") == 0) {
            mode = MODE_TDM;
        } else if (strcmp(argv[i], "--tdmo") == 0) {
            mode = MODE_TDMO;
        } else if (strcmp(argv[i], "--deathmatch") == 0) {
            mode = MODE_DEATHMATCH;
        }
    }
    return mode;
}

// server_net_init -- `port` defaults to 6969 (the real, standard SHANKPIT UDP port) but is now
// real, CLI-configurable (`--port N`, S459-34) -- added specifically so a second, independent
// server instance (e.g. for local dev/testing) can run without colliding with an already-bound
// live instance on the standard port, rather than needing to stop a shared, real, already-running
// service to test against (the same real caution this monorepo's own "never restart a shared
// matchmaker based on a process check alone" precedent already establishes elsewhere).
void server_net_init(int port) {
    setbuf(stdout, NULL);
    #ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
    #endif
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        NET_ERR_LOG("SOCKET_CREATE_FAILED");
        exit(1);
    }
    #ifdef _WIN32
    u_long mode = 1; ioctlsocket(sock, FIONBIO, &mode);
    #else
    int flags = fcntl(sock, F_GETFL, 0); fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    #endif
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons((unsigned short)port);
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    NET_SERVER_LOG("STARTUP mode=pending bind_addr=0.0.0.0 port=%d scene=%d", port, g_server_match_scene);
    if (bind(sock, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        NET_ERR_LOG("BIND_FAILED port=%d", port);
        exit(1);
    } else {
        NET_SERVER_LOG("SERVER_STARTED bind_addr=0.0.0.0 port=%d", port);
    }
}

int process_user_cmd(int client_id, UserCmd *cmd) {
    if (cmd->sequence <= client_last_seq[client_id]) {
        g_net_diag.stale_cmds++;
        unsigned int now_ms = get_server_time();
        if (net_should_log_every(&g_net_diag.stale_warn_last_ms[client_id], 1000, now_ms)) {
            NET_WARN_LOG("USERCMD_STALE client_id=%d seq=%u last_seq=%u", client_id, cmd->sequence, client_last_seq[client_id]);
        }
        return 0;
    }
    PlayerState *p = &local_state.players[client_id];
    shankpit_apply_usercmd_inputs(p, cmd);
    if (p->state == STATE_DEAD) {
        p->in_fwd = 0.0f;
        p->in_strafe = 0.0f;
        p->in_jump = 0;
        p->in_shoot = 0;
        p->in_reload = 0;
        p->in_use = 0;
        p->in_ability = 0;
    }
    client_last_seq[client_id] = cmd->sequence;
    g_net_diag.usercmds_applied++;
    return 1;
}

void server_handle_packet(struct sockaddr_in *sender, char *buffer, int size) {
    if (size < (int)sizeof(NetHeader)) {
        g_net_diag.malformed++;
        NET_WARN_LOG("SHORT_PACKET size=%d min=%zu", size, sizeof(NetHeader));
        return;
    }
    NetHeader *head = (NetHeader*)buffer;
    int client_id = -1;

    if (head->type == PACKET_CONNECT || head->type == PACKET_USERCMD || head->type == PACKET_DISCONNECT) {
        client_id = ensure_slot_for_sender(sender);
    }
    if (client_id == -1) return;

    if (head->type == PACKET_CONNECT) {
        char addr_buf[64];
        net_format_addr(sender, addr_buf, sizeof(addr_buf));
        int requested_mode = MODE_DEATHMATCH;
        if (size >= (int)sizeof(NetHeader) + 1) {
            requested_mode = (unsigned char)buffer[sizeof(NetHeader)];
        }
        NET_SERVER_LOG("CONNECT_RX src=%s size=%d requested_mode=%d", addr_buf, size, requested_mode);
        // Real, found-live fix (S459-34): `mode_before` was referenced below (CONNECT_ACCEPT log)
        // but never declared anywhere -- a genuine latent bug invisible under the default build
        // (NET_VERBOSE_LOG=0 strips the whole log call, arguments included, so the undeclared
        // identifier never actually needed to exist), only surfacing as a real compile error once
        // NET_VERBOSE_LOG=1 is turned on to verify this same feature. Captured here, before either
        // activate_match call below can change local_state.game_mode.
        int mode_before = local_state.game_mode;
        if (requested_mode == MODE_TDMO && local_state.game_mode != MODE_TDMO) {
            tdmo_activate_match(get_server_time());
        } else if (requested_mode == MODE_QUEUE && local_state.game_mode != MODE_QUEUE) {
            queue_activate_match(get_server_time());
        }
        PlayerState *p = &local_state.players[client_id];
        client_last_seq[client_id] = 0;
        if (local_state.game_mode == MODE_TDMO) {
            int team = tdmo_choose_join_team();
            tdmo_remove_one_bot_from_team(team);
            memset(p, 0, sizeof(*p));
            p->id = client_id;
            p->active = 1;
            p->is_bot = 0;
            p->team_id = team;
            p->scene_id = g_server_match_scene;
            p->state = STATE_ALIVE;
            p->health = 100;
            p->shield = 100;
            p->current_weapon = WPN_MAGNUM;
            p->ammo[WPN_MAGNUM] = WPN_STATS[WPN_MAGNUM].ammo_max;
            phys_respawn(p, get_server_time());
            local_state.client_meta[client_id].active = 1;
            tdmo_ensure_population(get_server_time());
        } else if (local_state.game_mode == MODE_QUEUE) {
            // Real, found-live fix (S459-34, caught via a real 3-bot connect test under
            // NET_VERBOSE_LOG=1): every OTHER free-for-all mode's default match scene is fixed at
            // server startup, so a never-yet-used PlayerState slot's zero-initialized scene_id
            // (0) already happens to match it -- but queue_activate_match changes
            // g_server_match_scene DYNAMICALLY (first real QUEUE connect can fire after the
            // server already started in a different mode/scene), so a newly connecting player's
            // scene_id needs the same explicit sync TDMO's own branch above already does, or
            // players connecting before vs. after that scene settles end up scattered across two
            // different scene_ids and never see each other. Confirmed live: without this, a
            // 3-bot test connect produced scene_id=0 for the first bot and scene_id=1 for the
            // next two.
            p->scene_id = g_server_match_scene;
            // S459-48, real, found-live CRITICAL bug: this branch set scene_id but never
            // actually SPAWNED the player -- unlike MODE_TDMO's own branch immediately above
            // (state=ALIVE, health/shield=100, weapon+ammo, phys_respawn), a QUEUE connect left
            // every other PlayerState field at whatever zero-initialized or stale-from-a-previous-
            // occupant value the slot already held: health=0, state=0 (STATE_ALIVE's own numeric
            // value, so the "am I alive" check never even caught it), no weapon/ammo set, no real
            // spawn position chosen. Found via a real, live Python client
            // (scripts/rl_env_packet.py) that connected successfully (welcomed) but then sat at
            // health=0 falling through empty space forever, never once reaching a valid alive
            // snapshot. The standing bot pool never surfaced this because its own bots, per
            // S459-35's own auto-reconnect fix, mostly stay connected across server restarts
            // rather than making a large volume of fresh CONNECTs -- this bug needed a genuinely
            // fresh connect to trigger, which the Python smoke test finally did. phys_respawn
            // itself already sets state/health/shield/weapon/ammo/spawn-position (it's the same
            // real function every in-match death->respawn cycle already calls, see this file's
            // own tick-loop call site) -- just needed to actually be called here.
            phys_respawn(p, get_server_time());
        }
        p->in_fwd = 0.0f;
        p->in_strafe = 0.0f;
        p->in_jump = 0;
        p->in_shoot = 0;
        p->in_reload = 0;
        p->in_use = 0;
        p->in_ability = 0;
        p->use_was_down = 0;
        p->portal_cooldown_until_ms = 0;
        p->vehicle_cooldown = 0;
        NET_SERVER_LOG("CONNECT_ACCEPT client_id=%d scene_id=%d team=%d mode_before=%d mode_after=%d mode_coerced=%d",
                       client_id, p->scene_id, p->team_id, mode_before, local_state.game_mode,
                       (requested_mode != local_state.game_mode));
        send_welcome(sender, client_id);
    }

    if (client_id != -1 && head->type == PACKET_DISCONNECT) {
        free_slot(client_id);
        return;
    }

    // --- USER COMMANDS ---
    if (client_id != -1 && head->type == PACKET_USERCMD) {
        g_net_diag.usercmd_pkts++;
        int cursor = (int)sizeof(NetHeader);
        if (size < cursor + 1) {
            g_net_diag.malformed++;
            NET_WARN_LOG("USERCMD_MALFORMED client_id=%d reason=missing_count size=%d", client_id, size);
            return;
        }

        unsigned char count = *(unsigned char*)(buffer + cursor); cursor += 1;
        int needed = cursor + (int)(count * sizeof(UserCmd));
        if (needed > size) {
            g_net_diag.malformed++;
            NET_WARN_LOG("USERCMD_MALFORMED client_id=%d count=%u size=%d needed=%d", client_id, count, size, needed);
            return;
        }

        UserCmd *cmds = (UserCmd*)(buffer + cursor);
        if (!g_net_diag.first_usercmd_pkt_seen[client_id]) {
            g_net_diag.first_usercmd_pkt_seen[client_id] = 1;
            unsigned int newest_seq = count > 0 ? cmds[0].sequence : 0;
            NET_SERVER_LOG("USERCMD_RX_FIRST client_id=%d count=%u newest_seq=%u size=%d", client_id, count, newest_seq, size);
            (void)newest_seq;
        }

        // process oldest->newest to preserve chronological intent
        for (int i = (int)count - 1; i >= 0; i--) {
            process_user_cmd(client_id, &cmds[i]);
        }

        slots[client_id].last_heard = now_seconds();
        local_state.client_meta[client_id].last_heard_ms = get_server_time();
        slots[client_id].cmd_seen = 1;
        if (local_state.game_mode != MODE_TDMO) {
            local_state.players[client_id].active = slots[client_id].welcomed && slots[client_id].cmd_seen;
            local_state.client_meta[client_id].active = local_state.players[client_id].active;
        }
    }
}

void server_broadcast() {
    /* Buffer sized for worst-case: all entities in one scene. */
    char buffer[sizeof(NetHeader) + 1 +
                MAX_CLIENTS * sizeof(NetPlayer) + 1 +
                MAX_HELICOPTERS * sizeof(NetHelicopter) + 1 +
                MAX_BUGGIES * sizeof(NetBuggy) + 1];
    for (int i = 1; i < MAX_CLIENTS; i++) {
        if (!slots[i].active || !slots[i].welcomed) continue;
        int recipient_scene = local_state.players[i].scene_id;
        int cursor = 0;

        NetHeader head;
        head.type = PACKET_SNAPSHOT;
        head.client_id = 0;
        head.sequence = local_state.server_tick;
        head.timestamp = get_server_time();
        head.scene_id = (unsigned char)(recipient_scene < 0 ? 0 : recipient_scene);
        head.entity_count = 0;
        memcpy(buffer + cursor, &head, sizeof(NetHeader)); cursor += (int)sizeof(NetHeader);

        /* Reserve the count byte; fill it after serialization so it matches actual written count. */
        int count_offset = cursor; cursor += 1;
        unsigned char count = 0;

        for (int pi = 1; pi < MAX_CLIENTS; pi++) {
            PlayerState *p = &local_state.players[pi];
            if (!p->active || p->scene_id != recipient_scene) continue;
            if (!p->is_bot && !(slots[pi].active && slots[pi].welcomed)) continue;
            NetPlayer np;
            np.id = (unsigned char)pi;
            np.scene_id = (unsigned char)p->scene_id;
            np.is_bot = (unsigned char)(p->is_bot ? 1 : 0);
            np.team_id = (signed char)p->team_id;
            np.last_seq = client_last_seq[pi];
            np.x = p->x; np.y = p->y; np.z = p->z;
            np.yaw = norm_yaw_deg(p->yaw); np.pitch = clamp_pitch_deg(p->pitch);
            np.current_weapon = (unsigned char)p->current_weapon;
            np.state = (unsigned char)p->state;
            np.health = (unsigned char)p->health;
            np.shield = (unsigned char)p->shield;
            np.is_shooting = (unsigned char)p->is_shooting;
            np.crouching = (unsigned char)p->crouching;
            np.reward_feedback = p->accumulated_reward;
            np.ammo = (unsigned char)p->ammo[p->current_weapon];
            np.in_vehicle = (unsigned char)p->in_vehicle;
            np.carried_flag_team_id = (signed char)p->carried_flag_team_id;
            np.hit_feedback = (unsigned char)p->hit_feedback;
            np.storm_charges = (unsigned char)p->storm_charges;
            np.kills = (unsigned short)(p->kills < 0 ? 0 : p->kills);
            np.deaths = (unsigned short)(p->deaths < 0 ? 0 : p->deaths);
            unsigned int death_elapsed = 0;
            if (p->state == STATE_DEAD && p->death_time_ms > 0 && head.timestamp >= p->death_time_ms) {
                death_elapsed = head.timestamp - p->death_time_ms;
            }
            if (death_elapsed > 65535u) death_elapsed = 65535u;
            np.death_elapsed_ms = (unsigned short)death_elapsed;
            np.death_duration_ms = (unsigned short)(p->death_duration_ms > 65535u ? 65535u : p->death_duration_ms);
            np.death_dir_x = p->death_dir_x;
            np.death_dir_z = p->death_dir_z;
            np.reload_timer = (unsigned short)(p->reload_timer < 0 ? 0 : (p->reload_timer > 65535 ? 65535 : p->reload_timer));
            np.ability_cooldown = (unsigned short)(p->ability_cooldown < 0 ? 0 : (p->ability_cooldown > 65535 ? 65535 : p->ability_cooldown));
            np.kill_streak = (unsigned char)(p->kill_streak < 0 ? 0 : (p->kill_streak > 255 ? 255 : p->kill_streak));
            p->accumulated_reward = 0;
            memcpy(buffer + cursor, &np, sizeof(NetPlayer)); cursor += (int)sizeof(NetPlayer);
            count++;
        }

        /* Patch count byte and header with actual serialized count. */
        buffer[count_offset] = count;
        ((NetHeader*)buffer)->entity_count = count;

        g_net_diag.snapshots_tx++;
        g_net_diag.snapshot_ents_total += count;
        if (!g_net_diag.first_snapshot_logged[i]) {
            g_net_diag.first_snapshot_logged[i] = 1;
            unsigned int now_ms = get_server_time();
            unsigned int dt_ms = g_net_diag.connect_ms[i] ? (now_ms - g_net_diag.connect_ms[i]) : 0;
            NET_SERVER_LOG("SNAPSHOT_TX_FIRST client_id=%d ents=%u scene=%d dt_since_connect=%u",
                           i, count, recipient_scene, dt_ms);
            (void)dt_ms;
        }

        int heli_count_offset = cursor; cursor += 1;
        unsigned char heli_count = 0;
        for (int hi = 0; hi < MAX_HELICOPTERS; hi++) {
            HelicopterState *h = &local_state.helicopters[hi];
            if (!h->active || h->scene_id != recipient_scene) continue;
            NetHelicopter nh;
            memset(&nh, 0, sizeof(nh));
            nh.id = (unsigned char)h->id;
            nh.scene_id = (unsigned char)h->scene_id;
            nh.active = (unsigned char)h->active;
            nh.grounded = (unsigned char)h->grounded;
            nh.x = h->x; nh.y = h->y; nh.z = h->z;
            nh.vx = h->vx; nh.vy = h->vy; nh.vz = h->vz;
            nh.yaw = h->yaw;
            nh.pitch_visual = h->pitch_visual;
            nh.roll_visual = h->roll_visual;
            nh.rotor_angle = h->rotor_angle;
            nh.rotor_speed = h->rotor_speed;
            nh.health = (unsigned char)(h->health < 0 ? 0 : (h->health > 255 ? 255 : h->health));
            nh.occupant_player_id = (signed char)h->occupant_player_id;
            memcpy(buffer + cursor, &nh, sizeof(NetHelicopter)); cursor += (int)sizeof(NetHelicopter);
            heli_count++;
        }
        buffer[heli_count_offset] = heli_count;

        int buggy_count_offset = cursor; cursor += 1;
        unsigned char buggy_count = 0;
        for (int bi = 0; bi < MAX_BUGGIES; bi++) {
            BuggyState *b = &local_state.buggies[bi];
            if (!b->active || b->scene_id != recipient_scene) continue;
            NetBuggy nb;
            memset(&nb, 0, sizeof(nb));
            nb.id = (unsigned char)b->id;
            nb.scene_id = (unsigned char)b->scene_id;
            nb.active = (unsigned char)b->active;
            nb.grounded = (unsigned char)b->grounded;
            nb.x = b->x; nb.y = b->y; nb.z = b->z;
            nb.vx = b->vx; nb.vy = b->vy; nb.vz = b->vz;
            nb.yaw = b->yaw;
            nb.pitch = b->pitch;
            nb.roll = b->roll;
            nb.steer = b->steer;
            nb.occupant_player_id = (signed char)b->occupant_player_id;
            memcpy(buffer + cursor, &nb, sizeof(NetBuggy)); cursor += (int)sizeof(NetBuggy);
            buggy_count++;
        }
        buffer[buggy_count_offset] = buggy_count;
#if HELI_NET_DEBUG
        printf("[VEH SNAPSHOT][TX] client=%d scene=%d heli_count=%u buggy_count=%u players=%u\n", i, recipient_scene, heli_count, buggy_count, count);
#endif
        if (slots[i].active) {
            sendto(sock, buffer, cursor, 0,
                   (struct sockaddr*)&slots[i].addr,
                   sizeof(struct sockaddr_in));
        }
    }
}

int main(int argc, char *argv[]) {
    int server_port = 6969;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--record") == 0) {
            recorder.enabled = 1;
        } else if (strcmp(argv[i], "--record-target") == 0 && i + 1 < argc) {
            recorder.target_id = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--record-file") == 0 && i + 1 < argc) {
            recorder.enabled = 1;
            recorder_init_file(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            server_port = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--fast-forward") == 0) {
            // S459-48, real training-pipeline prerequisite. Mirrors ECOWAR's own real
            // apps/arena_server/src/main.c --fast-forward exactly (itself mirrored into
            // BRAWLPIT's own bin/brawlpit_server, per BRAWLPIT/docs/RL_TRAINING_NORTHSTAR.md §2):
            // skips the real-time usleep(16000) tick pacing below so a training env can run
            // thousands of ticks per wall-clock second instead of the real 60Hz cap a live human
            // match needs. Never affects a normal (non-flagged) server -- the live production
            // shankpit-server.service unit does not pass this flag.
            g_fast_forward = 1;
        }
    }

    if (recorder.enabled && !recorder.file) {
        recorder_init_file("shankpit_recording.lispasm");
    }

    server_net_init(server_port);
    int mode = parse_server_mode(argc, argv);
    NET_SERVER_LOG("MODE_SELECTED mode=%d", mode);
    local_init_match(1, mode);
    if (mode == MODE_TDMO) {
        tdmo_activate_match(get_server_time());
    } else if (mode == MODE_DEATHMATCH || mode == MODE_TDM) {
        g_server_match_scene = g_dm_rotation[g_dm_rotation_idx];
        scene_load(g_server_match_scene);
        g_round_start_ms = get_server_time();
        if (g_server_match_scene == SCENE_VOXWORLD) {
            printf("[HELI] authoritative voxworld spawn count=%d\n", server_scene_heli_count(SCENE_VOXWORLD));
        }
    } else {
        g_server_match_scene = SCENE_GARAGE_OSAKA;
    }

    // --level <path> -- a level authored in NOCK's SHANKPIT level editor (EMILY/BACKLOG.md
    // SECTION 459, founder real-time: "get the level loading to work"). Deliberately the LAST
    // word on scene selection (after mode/rotation above), matching every other real, deliberate
    // override pattern in this file -- loading real, honest fallback on any failure (missing/
    // malformed file), never silently falls back to a garbled half-loaded level.
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--level") == 0 && i + 1 < argc) {
            CustomLevelData lvl;
            if (level_boxes_load_from_file(argv[i + 1], &lvl)) {
                server_apply_custom_level(&lvl);
                NET_SERVER_LOG("CUSTOM_LEVEL_LOADED name=%s boxes=%d path=%s", lvl.name, lvl.count, argv[i + 1]);
            } else {
                NET_SERVER_LOG("CUSTOM_LEVEL_LOAD_FAILED path=%s -- falling back to scene=%d", argv[i + 1], g_server_match_scene);
            }
            i++;
        }
    }

    local_state.players[0].active = 0;
    local_state.players[0].health = 0;
    local_state.players[0].state = STATE_DEAD;
    NET_SERVER_LOG("SCENE_SELECTED scene=%d", g_server_match_scene);
    if (mode == MODE_TDM) {
        NET_SERVER_LOG("SERVER_MODE TEAM_DEATHMATCH");
    } else if (mode == MODE_TDMO) {
        NET_SERVER_LOG("SERVER_MODE ONLINE_TEAM_DEATHMATCH");
    } else {
        NET_SERVER_LOG("SERVER_MODE DEATHMATCH");
    }

    int running = 1;
    unsigned int tick = 0;

    while(running) {
        char buffer[1024];
        struct sockaddr_in sender;
        socklen_t slen = sizeof(sender);

        int len = recvfrom(sock, buffer, 1024, 0, (struct sockaddr*)&sender, &slen);
        while (len > 0) {
            server_handle_packet(&sender, buffer, len);
            len = recvfrom(sock, buffer, 1024, 0, (struct sockaddr*)&sender, &slen);
        }

        unsigned int now = get_server_time();
        if (local_state.game_mode == MODE_TDMO) {
            tdmo_ensure_population(now);
        }
        double now_sec = now_seconds();
        for (int i = 1; i < MAX_CLIENTS; i++) {
            if (slots[i].active && now_sec - slots[i].last_heard > 5.0) {
                NET_WARN_LOG("SLOT_TIMEOUT client_id=%d idle_s=%.2f", i, now_sec - slots[i].last_heard);
                free_slot(i);
            }
        }

        int active_count = 0;

        for(int i=0; i<MAX_CLIENTS; i++) {
            PlayerState *p = &local_state.players[i];

            if (local_state.game_mode == MODE_TDMO && p->active && p->is_bot && p->state != STATE_DEAD) {
                float b_fwd = 0.0f;
                float b_yaw = p->yaw;
                int b_btns = 0;
                bot_think(i, local_state.players, SHANKPIT_NET_FIXED_DT, &b_fwd, &b_yaw, &b_btns);
                p->yaw = b_yaw;
                float brad = b_yaw * 3.14159f / 180.0f;
                float bx = sinf(brad) * b_fwd;
                float bz = cosf(brad) * b_fwd;
                accelerate(p, bx, bz, MAX_SPEED, ACCEL);
                p->in_shoot = (b_btns & BTN_ATTACK) ? 1 : 0;
                p->in_jump = (b_btns & BTN_JUMP) ? 1 : 0;
                p->in_reload = (b_btns & BTN_RELOAD) ? 1 : 0;
                p->crouching = (b_btns & BTN_CROUCH) ? 1 : 0;
                p->in_ability = 0;
                if (p->in_jump && p->on_ground) {
                    p->y += 0.1f;
                    p->vy += JUMP_FORCE;
                }
            }

            // S459-48, real, found-live bug: SHANKPIT had no void/out-of-bounds death anywhere
            // in the codebase -- a player who falls off any level's real geometry (confirmed live
            // via a real Python RL training client that wandered off NEWPIT's bounds during
            // scripts/rl_train_packet.py's own first real training run: y drifted to roughly
            // -1.6e8 and simply stayed STATE_ALIVE at full health forever, never respawning,
            // producing an unbounded, uninformative free-fall episode) just falls forever with no
            // recovery except a manual reconnect. VOID_KILL_Y is a real, generous threshold --
            // every built-in scene's own real floor/geometry sits well above -400 (the deepest,
            // SCENE_STORY_CAVE's own spawn, is only -1180 on Z, not Y), so this only ever fires
            // for a genuine fall-through, never a real, intentional low point in any level.
            #define VOID_KILL_Y -400.0f
            if (i > 0 && p->active && p->state == STATE_ALIVE && p->y < VOID_KILL_Y) {
                phys_enter_death_state(NULL, p, now, mode_respawn_delay_ms(local_state.game_mode), p->x, p->z);
            }

            if (i > 0 && p->active && p->state == STATE_DEAD) {
                if (p->in_vehicle && p->vehicle_type == VEH_BUGGY) {
                    for (int bi = 0; bi < MAX_BUGGIES; bi++) {
                        if (local_state.buggies[bi].active && local_state.buggies[bi].occupant_player_id == i) {
                            local_state.buggies[bi].occupant_player_id = -1;
                        }
                    }
                    p->in_vehicle = 0;
                    p->vehicle_type = VEH_NONE;
                }
                if (local_state.game_mode != MODE_SURVIVAL && p->respawn_time != 0 && now >= p->respawn_time) {
                    phys_respawn(p, now);
                    p->yaw = 0.0f;
                    p->pitch = 0.0f;
                }
            }

            if (p->active && p->state != STATE_DEAD) {
                phys_set_scene(p->scene_id);
                int use_pressed = p->in_use && !p->use_was_down;
                int portal_id = -1;
                if (use_pressed && now >= p->portal_cooldown_until_ms &&
                    scene_portal_active(p->scene_id) && scene_portal_triggered(p, &portal_id)) {
                    int dest_scene = -1;
                    float sx = 0.0f, sy = 0.0f, sz = 0.0f;
                    if (portal_resolve_destination(p->scene_id, portal_id, p->id,
                                                   &dest_scene, &sx, &sy, &sz)) {
                        int from_scene = p->scene_id;
                        p->scene_id = dest_scene;
                        phys_set_scene(p->scene_id);
                        p->x = sx; p->y = sy; p->z = sz;
                        p->vx = 0.0f; p->vy = 0.0f; p->vz = 0.0f;
                        p->in_vehicle = 0;
                        p->vehicle_type = VEH_NONE;
                        for (int bi = 0; bi < MAX_BUGGIES; bi++) {
                            if (local_state.buggies[bi].active && local_state.buggies[bi].occupant_player_id == i) {
                                local_state.buggies[bi].occupant_player_id = -1;
                            }
                        }
                        p->portal_cooldown_until_ms = now + 1000;
                        p->in_use = 0;
                        printf("PORTAL_TRAVEL client=%d from=%d to=%d\n", i, from_scene, dest_scene);
                    }
                } else if (use_pressed && p->vehicle_cooldown == 0) {
                    if (p->in_vehicle && p->vehicle_type == VEH_HELICOPTER) {
                        for (int hi = 0; hi < MAX_HELICOPTERS; hi++) {
                            HelicopterState *h = &local_state.helicopters[hi];
                            if (!h->active || h->occupant_player_id != i) continue;
                            if (!server_try_exit_heli(p, h)) {
                                printf("[HELI] exit failed client=%d\n", i);
                            }
                            break;
                        }
                        p->vehicle_cooldown = 30;
                    } else if (p->in_vehicle && p->vehicle_type == VEH_BUGGY) {
                        for (int bi = 0; bi < MAX_BUGGIES; bi++) {
                            BuggyState *b = &local_state.buggies[bi];
                            if (!b->active || b->occupant_player_id != i) continue;
                            buggy_try_exit(p, b);
                            break;
                        }
                        p->vehicle_cooldown = 30;
                    } else {
                        HelicopterState *h = server_find_nearby_heli(p->scene_id, p->x, p->y, p->z, g_heli_tuning.enter_radius);
                        if (h && h->occupant_player_id < 0) {
                            h->occupant_player_id = i;
                            p->in_vehicle = 1;
                            p->vehicle_type = VEH_HELICOPTER;
                            p->x = h->x; p->y = h->y; p->z = h->z;
                            p->vx = p->vy = p->vz = 0.0f;
                            p->vehicle_cooldown = 30;
                            printf("[HELI] enter client=%d heli=%d\n", i, h->id);
                        } else {
                            BuggyState *b = buggy_find_nearby(p->scene_id, p->x, p->y, p->z, 14.0f);
                            if (b && b->occupant_player_id < 0) {
                                buggy_try_enter(p, b);
                                p->vehicle_cooldown = 30;
                            }
                        }
                    }
                }
                p->use_was_down = p->in_use;
                if (p->vehicle_cooldown > 0) p->vehicle_cooldown--;

                if (!(p->in_vehicle && p->vehicle_type == VEH_HELICOPTER) &&
                    !(p->in_vehicle && p->vehicle_type == VEH_BUGGY)) {
                    shankpit_simulate_movement_tick(p, now);
                }
            } else {
                update_entity(p, SHANKPIT_NET_FIXED_DT, NULL, now);
            }
        }

        for (int hi = 0; hi < MAX_HELICOPTERS; hi++) {
            HelicopterState *h = &local_state.helicopters[hi];
            if (!h->active) continue;
            if (h->occupant_player_id >= 0 && h->occupant_player_id < MAX_CLIENTS) {
                PlayerState *occ = &local_state.players[h->occupant_player_id];
                h->scene_id = occ->scene_id;
                h->input.forward = occ->in_fwd;
                h->input.yaw = occ->in_strafe;
                h->input.strafe = occ->in_ability ? -1.0f : (occ->in_bike ? 1.0f : 0.0f);
                h->input.ascend = occ->in_jump;
                h->input.descend = occ->crouching;
            } else {
                h->occupant_player_id = -1;
                h->input.forward = 0.0f; h->input.yaw = 0.0f; h->input.strafe = 0.0f;
                h->input.ascend = 0; h->input.descend = 0;
            }
            phys_set_scene(h->scene_id);
            heli_simulate_step(h, SHANKPIT_NET_FIXED_DT);
            if (h->occupant_player_id >= 0 && h->occupant_player_id < MAX_CLIENTS) {
                PlayerState *occ = &local_state.players[h->occupant_player_id];
                occ->x = h->x; occ->y = h->y; occ->z = h->z;
                occ->yaw = h->yaw;
                occ->vx = occ->vy = occ->vz = 0.0f;
            }
        }
        buggy_tick_all();

        if ((local_state.game_mode == MODE_DEATHMATCH || local_state.game_mode == MODE_TDM) && server_scene_is_dm_map(g_server_match_scene)) {
            int top_frags = 0;
            for (int i = 1; i < MAX_CLIENTS; i++) {
                PlayerState *p = &local_state.players[i];
                if (!p->active || p->scene_id != g_server_match_scene) continue;
                if (p->kills > top_frags) top_frags = p->kills;
            }
            if ((now - g_round_start_ms) >= SERVER_DM_ROUND_MS || top_frags >= SERVER_DM_FRAG_LIMIT) {
                server_advance_dm_rotation(now);
            }
        }

        if (local_state.game_mode == MODE_QUEUE) {
            int top_frags = 0;
            for (int i = 1; i < MAX_CLIENTS; i++) {
                PlayerState *p = &local_state.players[i];
                if (!p->active || p->scene_id != g_server_match_scene) continue;
                if (p->kills > top_frags) top_frags = p->kills;
            }
            if ((now - g_round_start_ms) >= SERVER_QUEUE_ROUND_MS || top_frags >= SERVER_QUEUE_FRAG_LIMIT) {
                server_advance_queue_round(now);
            }
        }

        update_projectiles(now);
        if (server_team_mode_enabled(local_state.game_mode)) {
            for (int i = 0; i < MAX_CLIENTS; i++) {
                PlayerState *pp = &local_state.players[i];
                int prev = tdmb_last_kills[i];
                if (pp->kills > prev && team_id_is_valid(pp->team_id)) {
                    int delta = pp->kills - prev;
                    local_state.team_scores[pp->team_id] += delta;
                    if (!local_state.match_over &&
                        local_state.score_limit > 0 &&
                        local_state.team_scores[pp->team_id] >= local_state.score_limit) {
                        local_state.match_over = 1;
                        local_state.winning_team = pp->team_id;
                    }
                }
                tdmb_last_kills[i] = pp->kills;
            }
        }
        recorder_write_frame(tick, now);
        if ((tick % SERVER_SNAPSHOT_INTERVAL_TICKS) == 0) {
            server_broadcast();
        }
        net_server_emit_summary(now);

        int connected = 0;
        for (int i = 1; i < MAX_CLIENTS; i++) {
            if (slots[i].active && slots[i].welcomed && slots[i].cmd_seen && local_state.players[i].active) connected++;
        }
        active_count = connected;
        
        if (tick % 60 == 0 && (active_count > 0 || tick % 600 == 0)) {
            printf("[STATUS] Tick: %u | Clients: %d\n", tick, active_count);
            for (int i = 1; i < MAX_CLIENTS; i++) {
                if (!slots[i].active && !slots[i].welcomed && !slots[i].cmd_seen) continue;
                printf("  slot=%d active=%d welcomed=%d cmd_seen=%d player_active=%d last_heard_ms=%u\n",
                    i,
                    slots[i].active,
                    slots[i].welcomed,
                    slots[i].cmd_seen,
                    local_state.players[i].active,
                    local_state.client_meta[i].last_heard_ms);
            }
        }

        local_state.server_tick++;

        if (!g_fast_forward) {
            #ifdef _WIN32
            Sleep(16);
            #else
            usleep(16000);
            #endif
        }

        tick++;
    }

    return 0;
}
