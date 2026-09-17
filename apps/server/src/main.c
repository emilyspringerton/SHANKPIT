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
#include "../../../packages/world/story_doors.h"
#include "../../../packages/simulation/story_buttons.h"

/* cutscene handshake globals — defined in lobby/main.c for the client;
   server sim uses local_game.h but never renders cutscenes, so stub to 0. */
int g_story_cutscene_done   = 0;
int g_story_outro_requested = 0;
int g_shankpit_is_server    = 1; /* see local_game.h's own doc comment on this flag */

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
    // last_status_ms -- S483 follow-up, real, found-live fix: the [STATUS] Tick print below used
    // to throttle by TICK COUNT (tick % 600 == 0), a real, reasonable ~10-second cadence at a
    // normal server's own real ~60 ticks/sec -- but --fast-forward decouples tick count from wall
    // time entirely (confirmed live: ~500K ticks/sec), so the exact same modulo condition instead
    // fires hundreds of times PER SECOND, unconditionally, even with zero clients connected.
    // Founder real-time: a real, multi-hour Colab RL training run (which always runs
    // --fast-forward) silently broke after this print's own output started being captured to a
    // real log file instead of discarded (a separate, real fix in rl_train_packet.py's own
    // _spawn_server) -- writing that firehose continuously, especially to a slower/network-backed
    // --output-dir (e.g. a Colab Drive mount), is a real, plausible way to stall the server's own
    // tick loop badly enough to explain a client connecting but never seeing a snapshot in time.
    // Real, same fix shape as last_summary_ms/net_server_emit_summary immediately above -- wall-
    // clock throttled, not tick-count throttled, so it behaves identically regardless of how fast
    // ticks are actually advancing.
    unsigned int last_status_ms;
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
// S459-99, founder real-time: "matchmaking for queue is down... if you start a game it stays
// open... when you get 10 kills it resets to 0 like a new game maybe that game is still stuck
// open." Root cause: server_advance_queue_round used to fire the SAME tick the frag limit was
// hit, silently resetting stats in place with zero match-over signal -- indistinguishable from a
// match that never actually ends. SERVER_QUEUE_INTERMISSION_MS gives QUEUE a real, brief,
// observable match_over window (mirroring the team-mode match_over flag already used by
// MODE_TDMO/CTF, just auto-clearing instead of waiting on a player's own 'R' keypress, since
// QUEUE has no single "owner" to press it) before the next round actually starts.
#define SERVER_QUEUE_INTERMISSION_MS (5 * 1000)

static const int g_dm_rotation[] = { SCENE_STADIUM, SCENE_VOXWORLD, SCENE_OIL_TANKER, SCENE_POO_POO_ISLAND };
static int g_dm_rotation_idx = 0;
static int g_server_match_scene = SCENE_GARAGE_OSAKA;
static unsigned int g_round_start_ms = 0;
static unsigned int g_queue_intermission_start_ms = 0; // S459-99, 0 = not currently in intermission
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
// precedent of just resetting stats and continuing).
//
// S459-105, real correction to this comment's own prior claim (2026-09-17, founder real-time:
// "i switched the queue level and then tried to join queue and it think it tried to join me into
// like both levels or something"): this function used to deliberately skip reloading the level
// on the stated assumption that the admin-flagged default never changes mid-session -- false the
// moment NOCK's own level editor lets someone change it live, which left the server running
// stale collision geometry against a client that had already re-fetched the new one. Now calls
// queue_load_default_level() every round (see its own doc comment) -- a real, deliberately
// unconditional re-fetch, not a wasted round-trip: it's the ONLY way this server process ever
// finds out the flag changed.
static void queue_load_default_level(void);

static void server_advance_queue_round(unsigned int now_ms) {
    g_round_start_ms = now_ms;
    // S459-105: self-heal onto whatever level is CURRENTLY flagged default-for-queue, every
    // round -- see queue_load_default_level's own doc comment for the real "joined into both
    // levels" bug this closes. A no-op cost-wise if nothing changed (server_apply_custom_level
    // just re-applies the same real box list), so this runs unconditionally rather than trying
    // to detect "did it actually change" first.
    queue_load_default_level();
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

// S473, STORY_LEVEL_SEQUENCING_NORTHSTAR.md Phase 1 -- the CURRENTLY loaded level's own real exit
// trigger volumes + next_level_id, captured every time server_apply_custom_level runs (any mode --
// harmless when not MODE_STORY, since story_check_level_exits below only ever reads these under
// that gate). A real, deliberate reset on every load (not just append) so a level swap never
// leaves a stale exit from the PREVIOUS level active.
#define STORY_LEVEL_EXIT_MAX LEVEL_BOXES_MAX_LEVEL_EXITS
static LevelExit g_story_level_exits[STORY_LEVEL_EXIT_MAX];
static int g_story_level_exit_count = 0;
static int g_story_next_level_id = 0; /* 0 = none, matches CustomLevelData's own sentinel */
static unsigned int g_story_last_level_transition_ms = 0; /* real debounce -- see its own use below */

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
    float mat_friction[LEVEL_BOXES_MAX_MATERIALS];
    for (int mi = 0; mi < lvl->material_count; mi++) {
        strncpy(mat_names[mi], lvl->materials[mi].name, CUSTOM_LEVEL_MATERIAL_NAME_LEN - 1);
        mat_names[mi][CUSTOM_LEVEL_MATERIAL_NAME_LEN - 1] = '\0';
        strncpy(mat_shaders[mi], lvl->materials[mi].shader_name, CUSTOM_LEVEL_MATERIAL_NAME_LEN - 1);
        mat_shaders[mi][CUSTOM_LEVEL_MATERIAL_NAME_LEN - 1] = '\0';
        mat_specular[mi] = lvl->materials[mi].specular;
        mat_shininess[mi] = lvl->materials[mi].shininess;
        mat_friction[mi] = lvl->materials[mi].friction;
    }
    phys_set_custom_level_materials(mat_names, mat_shaders, mat_specular, mat_shininess, mat_friction, lvl->material_count);
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

    // Story System Phase 1 -- dlopen this level's own real door scripts now that
    // g_custom_level_box_authored_y is populated (phys_set_custom_level above).
    story_doors_init(lvl);

    // S485, REFLUX pub/sub buttons -- real, placed interact triggers for this level.
    story_buttons_init(lvl);

    // S461-01/S464 -- real, author-placed waypoint/cover graph, if any (empty is a real, honest
    // "not authored for this level yet" state, not an error). Flat-array unpack, same pattern
    // the spawner block above already uses for phys_set_custom_level_spawners.
    {
        float nn_x[LEVEL_BOXES_MAX_NAV_NODES], nn_y[LEVEL_BOXES_MAX_NAV_NODES], nn_z[LEVEL_BOXES_MAX_NAV_NODES];
        int nn_is_cover[LEVEL_BOXES_MAX_NAV_NODES];
        float nn_cover_dir_x[LEVEL_BOXES_MAX_NAV_NODES], nn_cover_dir_z[LEVEL_BOXES_MAX_NAV_NODES];
        int nn_neighbor_counts[LEVEL_BOXES_MAX_NAV_NODES];
        int nn_neighbors_flat[LEVEL_BOXES_MAX_NAV_NODES * LEVEL_BOXES_MAX_NAV_NEIGHBORS];
        for (int ni = 0; ni < lvl->nav_node_count; ni++) {
            nn_x[ni] = lvl->nav_nodes[ni].x; nn_y[ni] = lvl->nav_nodes[ni].y; nn_z[ni] = lvl->nav_nodes[ni].z;
            nn_is_cover[ni] = lvl->nav_nodes[ni].is_cover;
            nn_cover_dir_x[ni] = lvl->nav_nodes[ni].cover_dir_x;
            nn_cover_dir_z[ni] = lvl->nav_nodes[ni].cover_dir_z;
            nn_neighbor_counts[ni] = lvl->nav_nodes[ni].neighbor_count;
            for (int k = 0; k < LEVEL_BOXES_MAX_NAV_NEIGHBORS; k++) {
                /* LEVEL_BOXES_MAX_NAV_NEIGHBORS and STORY_AI_NAV_NEIGHBORS_STRIDE are both 4,
                   hand-kept in sync across the level_boxes.h/story_ai.h boundary -- same real
                   "no shared header, document the invariant" convention SHANKPIT_GRID_CELL_SIZE
                   already uses across the Go/C/TS boundary. */
                nn_neighbors_flat[ni * LEVEL_BOXES_MAX_NAV_NEIGHBORS + k] =
                    (k < lvl->nav_nodes[ni].neighbor_count) ? lvl->nav_nodes[ni].neighbors[k] : -1;
            }
        }
        story_ai_load_nav_graph(lvl->nav_node_count, nn_x, nn_y, nn_z, nn_is_cover,
                                 nn_cover_dir_x, nn_cover_dir_z, nn_neighbor_counts, nn_neighbors_flat);
    }

    // S467 (STORY_SYSTEM_NORTHSTAR.md Phase 2's "character" kind, founder real-time: "continue
    // filling in the gaps in our level editor scriptable env characters etc") -- real, author-
    // placed story_ai NPCs.
    //
    // S480, real fix (founder real-time: "can we make the characters stuff work outside of story
    // mode? ... theres no way for me to test the story mode unless we were to build a level
    // select interface... which is what the levels menu already is"). This used to be hard-gated
    // to MODE_STORY/MODE_STORY_CAVE only, for a real, genuine safety reason: the full
    // story_ai_reset deactivates every player slot 1..MAX_CLIENTS-1 unconditionally, which would
    // silently disconnect real connected humans (or the QUEUE bot pool) if it ever ran while this
    // function's OTHER real call site (queue_load_default_level, via server_advance_queue_round)
    // re-fires every MODE_QUEUE round with players already connected. That real risk is still
    // real -- but it's a reason to use a SAFER reset outside MODE_STORY, not a reason characters
    // can never spawn there at all. story_ai_despawn_all_characters (story_ai.c) only ever
    // deactivates the specific player slots g_story_ai itself spawned into (tracked via each
    // AIController's own player_id) -- every other slot, human or otherwise-bot, is left
    // completely untouched. MODE_STORY/MODE_STORY_CAVE keep using the original full
    // story_ai_reset unchanged (safe there -- single-hero, no other real connected players to
    // protect).
    if (local_state.game_mode == MODE_STORY || local_state.game_mode == MODE_STORY_CAVE) {
        story_ai_reset(&local_state);
    } else {
        story_ai_despawn_all_characters(&local_state);
    }
    for (int ci = 0; ci < lvl->character_count; ci++) {
        const LevelCharacter *lc = &lvl->characters[ci];
        if (lc->role < AI_ROLE_RIFT_HOUND || lc->role > AI_ROLE_BLIND_STALKER) {
            NET_SERVER_LOG("CUSTOM_LEVEL_CHARACTER_SKIPPED reason=invalid_role role=%d", lc->role);
            continue;
        }
        story_ai_spawn_enemy(&local_state, (AIRole)lc->role, lc->x, lc->y, lc->z);
    }
    if (lvl->character_count > 0) {
        NET_SERVER_LOG("CUSTOM_LEVEL_CHARACTERS_SPAWNED count=%d", lvl->character_count);
    }

    // S473/S477, STORY_LEVEL_SEQUENCING_NORTHSTAR.md -- real, unconditional capture (not
    // mode-gated like characters above): a level's own exits/next_level_id are real for ANY
    // mode as of S477 (story_check_level_exits' own exit_count/next_level_id check is a real,
    // sufficient gate on its own now, not a MODE_STORY check), and capturing them unconditionally
    // here means this state is always current for whatever level is actually loaded, matching
    // g_server_match_scene's own real "always current" convention a few lines above. Real,
    // deliberate full reset (not append) every call.
    g_story_level_exit_count = lvl->level_exit_count < STORY_LEVEL_EXIT_MAX ? lvl->level_exit_count : STORY_LEVEL_EXIT_MAX;
    for (int ei = 0; ei < g_story_level_exit_count; ei++) g_story_level_exits[ei] = lvl->level_exits[ei];
    g_story_next_level_id = lvl->next_level_id;
}

// story_check_level_exits -- S473, STORY_LEVEL_SEQUENCING_NORTHSTAR.md Phase 1 real, live level
// transition. Reuses the ALREADY-PROVEN-SAFE server_apply_custom_level mid-game level-swap path
// (the exact mechanism server_advance_queue_round already uses every real QUEUE round, not a new
// primitive) to fetch and apply next_level_id. Full 3D distance (not story_ai.c's own XZ-only
// convention) -- a placed exit volume should not fire for a player merely near the same X/Z on a
// different floor/level in Y, a real, deliberate difference from ai_len2's own 2D scope.
//
// S477, real fix (founder real-time, live playtest: "i walk over to the block where the level
// exit should be... i say out loud beam me up scotty and then nothing happens... i am on
// nextown"). This used to be gated to MODE_STORY + player 0 ("hero") only -- but exits are real,
// author-authored level data, not a story-mode-only concept, and the founder's own real
// "nextown" level (a plain level-select/deathmatch load, is_story_start=false) never ran this
// check at all. g_story_level_exit_count/g_story_next_level_id are already a real, sufficient
// gate on their own (a level with no exits authored is a real, honest no-op in ANY mode) -- no
// separate mode check needed. Now checks EVERY real active player (not just slot 0, which the
// dedicated server never even uses for a real connected client -- server main() explicitly
// deactivates it), and respawns EVERY active player after a real transition, not just whoever
// triggered it -- the whole world's own geometry just changed under everyone, matching (and
// improving on) server_advance_queue_round's own existing "the whole server moves together on a
// level change" precedent.
#define STORY_LEVEL_TRANSITION_DEBOUNCE_MS 3000U
static void story_check_level_exits(unsigned int now_ms) {
    if (g_story_level_exit_count <= 0 || g_story_next_level_id <= 0) return;
    if (now_ms - g_story_last_level_transition_ms < STORY_LEVEL_TRANSITION_DEBOUNCE_MS) return;

    int triggered = 0;
    for (int pi = 0; pi < MAX_CLIENTS && !triggered; pi++) {
        PlayerState *p = &local_state.players[pi];
        if (!p->active || p->state == STATE_DEAD) continue;
        for (int i = 0; i < g_story_level_exit_count; i++) {
            LevelExit *ex = &g_story_level_exits[i];
            float dx = p->x - ex->x, dy = p->y - ex->y, dz = p->z - ex->z;
            float dist2 = dx * dx + dy * dy + dz * dz;
            if (dist2 <= ex->radius * ex->radius) { triggered = 1; break; }
        }
    }
    if (!triggered) return;

    int next_id = g_story_next_level_id;
    CustomLevelData lvl;
    if (!level_boxes_fetch_export(next_id, &lvl)) {
        NET_SERVER_LOG("STORY_LEVEL_TRANSITION_FAILED next_level_id=%d -- export fetch failed", next_id);
        /* Real debounce even on failure -- an unreachable next level must not be hammered with a
           real curl fetch every single tick a player stands in the exit volume. */
        g_story_last_level_transition_ms = now_ms;
        return;
    }
    server_apply_custom_level(&lvl);
    for (int pi = 0; pi < MAX_CLIENTS; pi++) {
        PlayerState *p = &local_state.players[pi];
        if (!p->active) continue;
        p->scene_id = g_server_match_scene;
        phys_respawn(p, now_ms);
    }
    g_story_last_level_transition_ms = now_ms;
    NET_SERVER_LOG("STORY_LEVEL_TRANSITION next_level_id=%d name=%s", next_id, lvl.name);
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
// queue_load_default_level -- real, found-live fix (2026-09-17, founder real-time: "i switched
// the queue level and then tried to join queue and it think it tried to join me into like both
// levels or something"). Root cause: this fetch-and-apply sequence used to run ONLY once, inside
// queue_activate_match, at the very first transition into MODE_QUEUE for the server process's
// entire lifetime -- changing a level's own real "default for queue" flag in NOCK's level editor
// had zero effect on an already-running QUEUE match, forever, until the server process itself
// was restarted. Meanwhile the CLIENT independently re-fetches the current default level on its
// own join path (client_load_queue_level) -- so a mid-session level change left the client
// rendering the NEW level's geometry while the server kept simulating collision against the OLD
// one, reading as "joined into both levels" (new visuals, old invisible walls). Factored out so
// server_advance_queue_round (S459-100's own real, observable "a new round is starting" boundary
// -- see its own doc comment) can call this too, self-healing QUEUE onto whatever level is
// CURRENTLY flagged default at the start of every round, not just the server's own first ever
// activation.
static void queue_load_default_level(void) {
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

static void queue_activate_match(unsigned int now_ms) {
    local_init_match(1, MODE_QUEUE);
    local_state.game_mode = MODE_QUEUE;
    local_state.match_over = 0;
    local_state.players[0].active = 0;
    g_round_start_ms = now_ms;
    g_queue_intermission_start_ms = 0;
    queue_load_default_level();
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
        } else if (strcmp(argv[i], "--story") == 0) {
            // S465 follow-up: MODE_STORY was real in the enum and in local_init_match's own
            // switch, but genuinely unreachable on the dedicated server -- this parser never
            // recognized any flag for it, confirmed by reading this function before adding the
            // flag, not assumed. Without this, story_ai_tick's own new server-side wiring
            // (above) has no real way to be exercised at all.
            mode = MODE_STORY;
        } else if (strcmp(argv[i], "--story-cave") == 0) {
            mode = MODE_STORY_CAVE;
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
            np.anim_override = (unsigned char)p->anim_override;
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
            np.vx = p->vx; np.vy = p->vy; np.vz = p->vz; // S459-69: real velocity on the wire, see NetPlayer's own doc comment
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

    // S473, STORY_LEVEL_SEQUENCING_NORTHSTAR.md Phase 1 (founder real-time: "lets not work on
    // voxworld this is a legacy world ... we dont need the text cutscene in the beginning we
    // just need to spawn into the first map that the story is"). A real, deliberate override
    // AFTER local_init_match's own default VOXWORLD setup above -- same "deliberately the LAST
    // word on scene selection" pattern the --level CLI flag's own doc comment below already
    // establishes, positioned BEFORE that flag so --level (if the operator gives one) still wins,
    // matching that flag's own explicit intent. A level flagged is_story_start replaces VOXWORLD
    // entirely: server_apply_custom_level's own MODE_STORY branch spawns THIS level's own real
    // Characters (story_ai_reset already clears whatever local_init_match seeded), the legacy
    // boss is explicitly disabled (it belongs to the VOXWORLD encounter this level is replacing,
    // not a NOCK-authored one), and the intro cutscene never runs (STORY_PHASE_PLAYING is already
    // set unconditionally for the server, see local_state.story_phase's own g_shankpit_is_server
    // branch in local_game.h -- the "skip the cutscene" ask was always already true server-side;
    // this real override is what actually replaces VOXWORLD's own content). With no such level
    // authored yet, this is a real, honest no-op and VOXWORLD's own existing path runs unchanged.
    if (mode == MODE_STORY) {
        LevelRegistryEntry story_entries[LEVEL_REGISTRY_MAX_ENTRIES];
        int story_entry_count = level_boxes_fetch_registry_list(story_entries, LEVEL_REGISTRY_MAX_ENTRIES);
        int story_start_id = -1;
        for (int i = 0; i < story_entry_count; i++) {
            if (story_entries[i].is_story_start) { story_start_id = story_entries[i].id; break; }
        }
        if (story_start_id >= 0) {
            CustomLevelData lvl;
            if (level_boxes_fetch_export(story_start_id, &lvl)) {
                server_apply_custom_level(&lvl);
                local_state.story_boss.active = 0;
                local_state.story_phase = STORY_PHASE_PLAYING;
                NET_SERVER_LOG("STORY_START_LEVEL_LOADED id=%d name=%s", story_start_id, lvl.name);
            } else {
                NET_SERVER_LOG("STORY_START_LEVEL_FETCH_FAILED id=%d -- falling back to VOXWORLD", story_start_id);
            }
        }
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
        // S465 follow-up, founder real-time: "wire story_ai_tick into the server's own tick
        // loop." Real, found-live gap this closes: story_ai_tick was only ever called from
        // local_update, itself only invoked by the LOBBY binary's own local single-player input
        // path (apps/lobby/src/main.c) -- confirmed by grep before this fix, not assumed -- so
        // every S461/S462/S465 NPC behavior (squads, flee, scripted sequences, solo archetypes,
        // the NOCK-authored waypoint graph) never actually ran on the dedicated multiplayer
        // server, under any game mode. story_ai_tick already self-gates on game_mode ==
        // MODE_STORY && story_phase == STORY_PHASE_PLAYING (a real no-op for every other mode,
        // same discipline story_doors_tick's own unconditional per-tick call already relies on)
        // -- calling it here unconditionally is safe. Placed BEFORE the per-player movement loop
        // below so any AI-set in_fwd/in_strafe/yaw this tick are already in place when
        // shankpit_simulate_movement_tick (packages/common/net_sim.h) reads them for every
        // active player slot -- that function is already generic across real players AND bots
        // (no is_bot branch), so no separate movement-application step is needed here, unlike
        // local_update's own bespoke MODE_STORY accelerate() branch.
        //
        // Real, deliberate scope limit, not attempted here: story_boss_tick/story_swarm_tick/
        // mechanism_tick/story_cave_endure_tick (VOXWORLD's own specific boss-fight content,
        // single-"hero"-targeted) stay lobby-only for now -- this fix covers the general,
        // NOCK-placeable story_ai NPC system, not that specific single-player boss encounter.
        story_ai_tick(&local_state, now);
        story_check_level_exits(now);
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
            // S459-99: a real match_over intermission instead of an instant, invisible
            // in-place reset -- see SERVER_QUEUE_INTERMISSION_MS's own comment for the bug this
            // closes.
            if (!local_state.match_over) {
                int top_frags = 0;
                for (int i = 1; i < MAX_CLIENTS; i++) {
                    PlayerState *p = &local_state.players[i];
                    if (!p->active || p->scene_id != g_server_match_scene) continue;
                    if (p->kills > top_frags) top_frags = p->kills;
                }
                if ((now - g_round_start_ms) >= SERVER_QUEUE_ROUND_MS || top_frags >= SERVER_QUEUE_FRAG_LIMIT) {
                    local_state.match_over = 1;
                    g_queue_intermission_start_ms = now;
                    NET_SERVER_LOG("QUEUE_MATCH_OVER top_frags=%d", top_frags);
                }
            } else if ((now - g_queue_intermission_start_ms) >= SERVER_QUEUE_INTERMISSION_MS) {
                server_advance_queue_round(now);
                local_state.match_over = 0;
                g_queue_intermission_start_ms = 0;
            }
        }

        update_projectiles(now);
        // Story System Phase 1 (docs/STORY_SYSTEM_NORTHSTAR.md Part 2) -- real, once-per-tick
        // door script evaluation, right after the rest of the tick's own physics/state updates
        // and before the snapshot broadcast, so a door's new state is reflected in the very
        // snapshot this tick sends out.
        story_doors_tick(local_state.players, MAX_CLIENTS);
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
        
        // Real wall-clock throttle (see last_status_ms's own doc comment above for the full
        // --fast-forward rationale) -- once per real second while someone's connected, once per
        // real 10 seconds otherwise, regardless of how fast ticks are actually advancing.
        if (net_should_log_every(&g_net_diag.last_status_ms, active_count > 0 ? 1000 : 10000, now)) {
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
