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
#include "../../../packages/simulation/proc_dungeon.h"
#include "../../../packages/simulation/zone_registry.h"
#include "../../../packages/simulation/zone_rules_host.h"
#include "../../../packages/simulation/zone_sync.h"
#include "../../../packages/simulation/empire_sync.h"
#include "../../../packages/common/physics.h"
/* AFTER physics.h: this installs into physics' own player-stats hooks (stats_apply.h's own note). */
#include "../../../packages/simulation/stats_apply.h" /* --job: a GFD job -> authoritative health/ROF */
#include "../../../packages/common/shared_movement.h"
#include "../../../packages/common/net_sim.h"
#include "../../../packages/simulation/local_game.h"
#include "../../../packages/simulation/zombie_values.h" /* ZOMBIE_MOOD_HUNTING, for the citizen wire test's planted pair */
#include "../../../packages/simulation/tyler_coldopen.h"
#include "../../../packages/world/level_boxes.h"
#include "../../../packages/world/story_doors.h"
#include "../../../packages/simulation/story_buttons.h"
#include "../../../packages/simulation/brick_world.h"
#include <stddef.h>
#include "../../../packages/simulation/shield_packs.h"
#include "../../../packages/simulation/buggy_rules_host.h"
#include "../../../packages/simulation/horse_rules_host.h"
#include "../../../packages/simulation/horse_goals_host.h"  /* what a loose horse decides (PARENA) */
#include "../../../packages/simulation/horse_poop_host.h"   /* what comes out the other end (PARENA) */
#include "../../../packages/simulation/citizen_actor_rules_host.h" /* citizen bodies: jump/slide/fight (PARENA) */
#include "../../../packages/simulation/horse_sync.h"        /* PACKET_HORSE_STATE / PACKET_HORSE_EVENTS */
#include "../../../packages/simulation/citizen_sync.h"      /* PACKET_CITIZEN_EVENTS: the citizen REFLUX family */
#include "../../../packages/simulation/horse_interact.h"    /* the request channel's own policy */
#include "../../../packages/reflux/reflux_runtime.h"         /* the horse tick dispatches its family here */
#include "../../../packages/simulation/vehicle_salvage_rules_host.h" /* PARENA car components */
#include "../../../packages/simulation/garage_rules_host.h" /* PARENA garage rules */
#include "../../../packages/simulation/heli_rules_host.h"
#include "../../../packages/simulation/rocket_car_rules_host.h"   /* the rocket car's PARENA model */
#include "../../../packages/simulation/kit_call.h"   /* PACKET_KIT_CALL: the kit's vehicle gadgets */
#include "../../../packages/protocol/wow_server.h"   /* PACKET_WOW_WIRE: targets, casts, macro bytecode */
#include "../../../packages/simulation/vendor_serve.h"  /* the ice cream truck's window, server-authoritative */
#include "../../../packages/common/units_rules.h"        /* the cast range's yards -> metres conversion */
#include "../../../packages/common/third_person.h"   /* tp_yaw_pitch_to_point: aim a cast at its target */
#include "../../../packages/protocol/wow_ticket.h"   /* the IDUNA player ticket, verified locally */

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

/* Per-client target tables, macro definitions and cast queues. Begin()'d lazily on the first wire
   frame, which also sets require_auth=1: until an IDUNA verifier is installed every op is refused. */
static WowServer g_wow_server;
static int g_wow_server_inited;

/* THE IDUNA VERIFIER, installed. IDUNA mints a 36-byte player ticket with HMAC-SHA256 over
 * player_id||expires_at and a 5-minute TTL; the server verifies it locally with the shared
 * SHANKPIT_TICKET_SECRET -- no HTTP client, no crypto library, no clock but the one passed in here. */
static unsigned char g_ticket_secret[128];
static int g_ticket_secret_len;

static int wow_ticket_auth(void *ctx, const unsigned char *ticket, int len, char *player_id, int cap) {
    (void)ctx;
    if (len != WOW_TICKET_BYTES) return 0;
    return wow_ticket_verify(g_ticket_secret, g_ticket_secret_len, ticket,
                             (uint32_t)time(NULL), player_id, cap);
}
static int g_server_sock = -1;   /* captured where the listening socket is created, for wire ACKs */

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

/* BUILT-IN MAP WALLS (2026-11-05). The custom-level path federates the level's authored boxes to AI sight;
   the built-in maps have no level file, so they kept open sightlines and a clerk in the garage could shoot
   through its wall. They do have geometry -- physics' own map_geo_* arrays, the ones collision and hitscan
   already use -- so this installs THOSE. Copied, because story_ai keeps its own array and the scene's may
   be rebuilt on the next load. An empty list (a scene with no boxes) still clears, which is the same
   "no geometry means no blockers" rule the custom path follows. */
static void server_install_builtin_sight(void) {
    static SightBox sight[LEVEL_BOXES_MAX];
    int n = 0;
    const Box *boxes = phys_scene_boxes(&n);
    if (!boxes || n <= 0) { story_ai_set_sight(NULL, 0); return; }
    if (n > LEVEL_BOXES_MAX) n = LEVEL_BOXES_MAX;
    for (int i = 0; i < n; i++) {
        sight[i].x = boxes[i].x; sight[i].y = boxes[i].y; sight[i].z = boxes[i].z;
        sight[i].w = boxes[i].w; sight[i].h = boxes[i].h; sight[i].d = boxes[i].d;
        sight[i].solid = 1;
    }
    story_ai_set_sight(sight, n);
    NET_SERVER_LOG("BUILTIN_LEVEL_SIGHT_BOXES count=%d scene=%d", n, g_server_match_scene);
}
static unsigned int g_round_start_ms = 0;
static unsigned int g_queue_intermission_start_ms = 0; // S459-99, 0 = not currently in intermission
static int g_fast_forward = 0; // S459-48 -- see --fast-forward's own doc comment in main() for the real rationale
static int g_drop_cast_acks = 0; /* TEST ONLY -- see --drop-cast-acks; never set by a production unit */
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
    /* Built-in map: give the AIs the map's OWN collision boxes (see server_install_builtin_sight). */
    server_install_builtin_sight();
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
    shield_packs_reset(); // card #541: a new round starts without last round's packs
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
    server_install_builtin_sight();   /* same built-in geometry the rotation load uses, see above */
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

/* S536, TYLER VALHANNA cold open (episodes/vh01_valhanna_coldopen.md) -- real, deliberate reset
 * on every level load, same convention g_story_level_exit_count above already uses, so a level
 * swap away from TYLER_VALHANNA_ICELAND_1986 never leaves a stale coordinator ticking. */
static TylerColdOpenState g_tyler_coldopen;
static void tyler_apply_phase_override(const char *level_name, unsigned int now_ms); /* fwd decl -- defined below, called from both server_apply_custom_level and story_force_level_transition */

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
    {   /* oriented boxes / ramps (obb.h): static scratch keeps the 4 extra arrays off the stack */
        static float orx[LEVEL_BOXES_MAX], ory[LEVEL_BOXES_MAX], orz[LEVEL_BOXES_MAX];
        static unsigned char oramp[LEVEL_BOXES_MAX];
        for (int bi = 0; bi < lvl->count; bi++) {
            orx[bi] = lvl->boxes[bi].rot_x; ory[bi] = lvl->boxes[bi].rot_y; orz[bi] = lvl->boxes[bi].rot_z;
            oramp[bi] = (unsigned char)(lvl->boxes[bi].ramp ? 1 : 0);
        }
        phys_set_custom_level_orient(orx, ory, orz, oramp, lvl->count);
    }
    {   /* #464: author-placed buggies */
        float bx[LEVEL_BOXES_MAX_BUGGY_SPAWNS], by[LEVEL_BOXES_MAX_BUGGY_SPAWNS], bz[LEVEL_BOXES_MAX_BUGGY_SPAWNS], byaw[LEVEL_BOXES_MAX_BUGGY_SPAWNS];
        for (int bi = 0; bi < lvl->buggy_spawn_count; bi++) {
            bx[bi] = lvl->buggy_spawns[bi].x; by[bi] = lvl->buggy_spawns[bi].y;
            bz[bi] = lvl->buggy_spawns[bi].z; byaw[bi] = lvl->buggy_spawns[bi].yaw;
        }
        phys_set_custom_level_buggy_spawns(bx, by, bz, byaw, lvl->buggy_spawn_count);
    }
    {   /* author-placed HORSES (level_boxes.h horse_spawns): the farm pads. scene_load places the
           horses themselves; this only carries where each one stands and which way it faces. */
        float hx[LEVEL_BOXES_MAX_HORSE_SPAWNS], hy[LEVEL_BOXES_MAX_HORSE_SPAWNS], hz[LEVEL_BOXES_MAX_HORSE_SPAWNS], hyaw[LEVEL_BOXES_MAX_HORSE_SPAWNS];
        for (int hi = 0; hi < lvl->horse_spawn_count; hi++) {
            hx[hi] = lvl->horse_spawns[hi].x; hy[hi] = lvl->horse_spawns[hi].y;
            hz[hi] = lvl->horse_spawns[hi].z; hyaw[hi] = lvl->horse_spawns[hi].yaw;
        }
        phys_set_custom_level_horse_spawns(hx, hy, hz, hyaw, lvl->horse_spawn_count);
    }
    {   /* author-placed VEHICLES (level_boxes.h vehicle_spawns): the buggy tile generalised to the
           whole roster. scene_load (below) places them; the server owns their hull and fire. */
        int vk[CUSTOM_LEVEL_MAX_VEHICLE_SPAWNS];
        float vx[CUSTOM_LEVEL_MAX_VEHICLE_SPAWNS], vy[CUSTOM_LEVEL_MAX_VEHICLE_SPAWNS];
        float vz[CUSTOM_LEVEL_MAX_VEHICLE_SPAWNS], vya[CUSTOM_LEVEL_MAX_VEHICLE_SPAWNS];
        for (int vi = 0; vi < lvl->vehicle_spawn_count; vi++) {
            vk[vi] = lvl->vehicle_spawns[vi].kind;
            vx[vi] = lvl->vehicle_spawns[vi].x; vy[vi] = lvl->vehicle_spawns[vi].y;
            vz[vi] = lvl->vehicle_spawns[vi].z; vya[vi] = lvl->vehicle_spawns[vi].yaw;
        }
        phys_set_custom_level_vehicle_spawns(vk, vx, vy, vz, vya, lvl->vehicle_spawn_count);
        {   /* provenance travels with the spawns, in its own setter */
            int vag[CUSTOM_LEVEL_MAX_VEHICLE_SPAWNS];
            for (int vi = 0; vi < lvl->vehicle_spawn_count; vi++) vag[vi] = lvl->vehicle_spawns[vi].origin;
            phys_set_custom_level_vehicle_origins(vag, lvl->vehicle_spawn_count);
        }
    }
    brick_world_init_from_level(lvl); // destructible brick: pick the carvable boxes, install the weapon/blast hooks

    // S459-58: real, author-placed spawn points, team/FFA-aware.
    float sp_x[LEVEL_BOXES_MAX_SPAWNERS], sp_y[LEVEL_BOXES_MAX_SPAWNERS], sp_z[LEVEL_BOXES_MAX_SPAWNERS];
    int sp_team[LEVEL_BOXES_MAX_SPAWNERS];
    int sp_id[LEVEL_BOXES_MAX_SPAWNERS]; // S491: threads each spawner's own author-assigned id through
    for (int si = 0; si < lvl->spawner_count; si++) {
        sp_x[si] = lvl->spawners[si].x; sp_y[si] = lvl->spawners[si].y; sp_z[si] = lvl->spawners[si].z;
        sp_team[si] = lvl->spawners[si].team;
        sp_id[si] = lvl->spawners[si].id;
    }
    phys_set_custom_level_spawners(sp_x, sp_y, sp_z, sp_team, sp_id, lvl->spawner_count);

    g_server_match_scene = SCENE_CUSTOM_LEVEL;
    scene_load(g_server_match_scene);

    /* LINE OF SIGHT (packages/simulation/sight.h). The AI used to see through every wall there is; the
       client learned better first and the server -- which is where the clerks actually decide -- still
       did not. Same boxes phys_set_custom_level just took above, copied INTO story_ai (not referenced),
       so a mid-game level swap cannot leave a sight world pointing at freed level memory. Unlimited
       arrays live for the match here, exactly like the converted level data they come from. */
    {
        static SightBox sight[LEVEL_BOXES_MAX];
        int sn = lvl->count < LEVEL_BOXES_MAX ? lvl->count : LEVEL_BOXES_MAX;
        for (int si = 0; si < sn; si++) {
            sight[si].x = lvl->boxes[si].x; sight[si].y = lvl->boxes[si].y; sight[si].z = lvl->boxes[si].z;
            sight[si].w = lvl->boxes[si].w; sight[si].h = lvl->boxes[si].h; sight[si].d = lvl->boxes[si].d;
            sight[si].solid = 1;
        }
        story_ai_set_sight(sight, sn);
        NET_SERVER_LOG("CUSTOM_LEVEL_SIGHT_BOXES count=%d", sn);
    }

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
    /* S536, TYLER VALHANNA -- real, deliberate reset every level load (same convention as
     * g_story_level_exit_count above): a level swap away from TYLER_VALHANNA_ICELAND_1986 must
     * never leave the coordinator still ticking against stale player slots. */
    g_tyler_coldopen.active = 0;
    int tyler_coldopen_slots[2] = { -1, -1 };
    int tyler_coldopen_slot_count = 0;
    for (int ci = 0; ci < lvl->character_count; ci++) {
        const LevelCharacter *lc = &lvl->characters[ci];
        // S492, real, found-live gap: this range check (and its Go-side mirror,
        // internal/shankpit.validateCharacters) both stopped at AI_ROLE_BLIND_STALKER, one enum
        // value short of AI_ROLE_WANDERING_BOT -- the exact role a designer would want for
        // "walking around the city... even just standing there and having their head turn and
        // look at you." A level author who managed to author role=10 anyway (e.g. hand-edited
        // JSON, before NOCK's own dropdown gap was also fixed) would have silently lost that
        // character here, logged as an "invalid role," not spawned as the real, working
        // non-hostile ambient bot it actually is.
        // The range itself now lives in story_ai.h's own story_ai_role_is_valid: writing it out
        // here (and in apps/lobby, and in story_ai_spawn_unit_role, and in IDUNA) is how it shipped a
        // value short last time. The civic boss made it worth one function instead of four copies.
        int role_ok = story_ai_role_is_valid(lc->role);
        /* An EMPIRE unit (archetype > 0) may author no role at all (-1, level_boxes.h) and keep its
           archetype's own brain; every other character still needs a valid one. */
        if (!role_ok && lc->archetype <= 0) {
            NET_SERVER_LOG("CUSTOM_LEVEL_CHARACTER_SKIPPED reason=invalid_role role=%d", lc->role);
            continue;
        }
        // S492: an out-of-range authored kit falls back to AI_KIT_AUTO rather than skipping the
        // whole character -- same "a stale/bad reference mustn't break the level" convention
        // Door/NavNode cross-references already follow, just for a cosmetic field instead of a
        // structural one, so it earns a fallback instead of a skip.
        int kit = story_ai_kit_is_valid(lc->kit) ? lc->kit : AI_KIT_AUTO;
        /* An authored EMPIRE unit (archetype > 0) goes through the order layer; everything else keeps
           spawning exactly as it did. */
        /* An authored `role` overrides the archetype's brain (a sniper clerk, not a sixth guard);
           -1 keeps the archetype's own. */
        int spawned_id = lc->archetype > 0
                             ? story_ai_spawn_unit_role(&local_state, lc->archetype,
                                                        role_ok ? lc->role : -1,
                                                        kit, lc->faction, lc->orders,
                                                        lc->x, lc->y, lc->z)
                             : story_ai_spawn_enemy(&local_state, (AIRole)lc->role, kit,
                                                    lc->x, lc->y, lc->z);
        /* S536: TYLER_VALHANNA_ICELAND_1986 authors exactly two characters, Tyler first then Hana
         * (cmd/nock_gen_tyler_levels/main.go) -- capture whichever real player slots they actually
         * landed in, in authored order, same "author order is actor identity" convention this
         * level's own JSON already establishes. Harmless on every other level: tyler_coldopen_
         * slots is only ever read below, gated on MODE_TYLER. */
        if (local_state.game_mode == MODE_TYLER && spawned_id > 0 && tyler_coldopen_slot_count < 2) {
            tyler_coldopen_slots[tyler_coldopen_slot_count++] = spawned_id;
        }
    }
    /* EMPIRE: the level's own contested ground (resource_nodes / claim_cells), replacing the default
       ring. Unpacked here rather than inside story_ai so story_ai.h keeps its no-packages/world boundary;
       a level that authors nothing keeps the default ring (that fallback lives in
       story_ai_empire_load_level, so both hosts behave the same way). Loaded after the characters so the
       first tick already has something to gather and something to take. */
    {
        float eu_nx[LEVEL_BOXES_MAX_RESOURCE_NODES], eu_nz[LEVEL_BOXES_MAX_RESOURCE_NODES];
        int eu_amt[LEVEL_BOXES_MAX_RESOURCE_NODES];
        float eu_cx[LEVEL_BOXES_MAX_CLAIM_CELLS], eu_cz[LEVEL_BOXES_MAX_CLAIM_CELLS];
        int eu_rad[LEVEL_BOXES_MAX_CLAIM_CELLS];
        for (int ni = 0; ni < lvl->resource_node_count; ni++) {
            eu_nx[ni] = lvl->resource_nodes[ni].x; eu_nz[ni] = lvl->resource_nodes[ni].z;
            eu_amt[ni] = lvl->resource_nodes[ni].amount;
        }
        for (int ci = 0; ci < lvl->claim_cell_count; ci++) {
            eu_cx[ci] = lvl->claim_cells[ci].x; eu_cz[ci] = lvl->claim_cells[ci].z;
            eu_rad[ci] = lvl->claim_cells[ci].radius;
        }
        story_ai_empire_load_level(lvl->resource_node_count, eu_nx, eu_nz, eu_amt,
                                   lvl->claim_cell_count, eu_cx, eu_cz, eu_rad);
    }
    if (lvl->character_count > 0) {
        NET_SERVER_LOG("CUSTOM_LEVEL_CHARACTERS_SPAWNED count=%d", lvl->character_count);
    }
    if (local_state.game_mode == MODE_TYLER && tyler_coldopen_slot_count == 2) {
        tyler_coldopen_start(&g_tyler_coldopen, tyler_coldopen_slots[0], tyler_coldopen_slots[1], get_server_time());
        NET_SERVER_LOG("TYLER_COLDOPEN_STARTED tyler_slot=%d hana_slot=%d", tyler_coldopen_slots[0], tyler_coldopen_slots[1]);
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

    /* S536: covers MODE_TYLER's initial load (a fresh connect / --level arg into
     * TYLER_VALHANNA_ICELAND_1986, before any respawn has run) -- redundant-but-harmless during a
     * mid-game story_force_level_transition, which re-applies this same override itself right
     * after ITS OWN phys_respawn loop runs (phys_respawn unconditionally resets state to
     * STATE_ALIVE, which would otherwise silently undo a spectator override applied only here). */
    tyler_apply_phase_override(lvl->name, get_server_time());
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

/* tyler_apply_phase_override -- S536, TYLER VALHANNA. MODE_TYLER's two phases (wisp in Iceland,
 * Duck in CONSTRUCT, see packages/common/protocol.h's own MODE_TYLER doc comment) share one game
 * mode number -- this is the one real place that actually changes what a real (non-bot) player IS
 * once the level itself has changed, keyed on the just-loaded level's own name rather than a
 * separate phase enum, since the level name is already real, unique, authoritative data
 * (cmd/nock_gen_tyler_levels/main.go names them exactly "TYLER_VALHANNA_ICELAND_1986"/
 * "CONSTRUCT") -- no new state to keep in sync. Deliberately only touches REAL, non-bot players
 * (Tyler/Hana are is_bot AI_ROLE_STORY_ALLY NPCs, already handled by the ordinary character-spawn
 * path above; this function would be a harmless no-op against them anyway since neither branch
 * applies to a bot in any other mode, but the is_bot check makes the intent explicit). */
static void tyler_apply_phase_override(const char *level_name, unsigned int now_ms) {
    if (local_state.game_mode != MODE_TYLER) return;
    int is_construct = (strcmp(level_name, "CONSTRUCT") == 0);
    for (int pi = 0; pi < MAX_CLIENTS; pi++) {
        PlayerState *p = &local_state.players[pi];
        if (!p->active || p->is_bot) continue;
        if (is_construct) {
            /* The Duck -- third-person, SHANKPIT physics, Leela kit (packages/simulation/
             * story_ai.h's own AIKit enum -- AI_KIT_LEELA). apps/lobby/src/main.c's own
             * draw_player_3rd gains a matching MODE_TYLER branch so a forced_kit this player sets
             * on itself (not just story_ai-spawned bots) actually renders. */
            p->state = STATE_ALIVE;
            p->forced_kit = AI_KIT_LEELA;
            /* third_person is a real, separate flag from forced_kit as of this pass (see
             * PlayerState's own doc comment, protocol.h) -- the Duck phase sets both together,
             * but the camera itself now keys off this field alone, not the skin choice. */
            p->third_person = 1;
        } else {
            /* The wisp -- real STATE_SPECTATOR free-fly (packages/common/protocol.h,
             * packages/simulation/local_game.h's update_entity), no forced skin at all (nothing
             * should render a body for a camera-only orb), and no third-person camera either. */
            p->state = STATE_SPECTATOR;
            p->forced_kit = AI_KIT_AUTO;
            p->third_person = 0;
        }
    }
    (void)now_ms;
}

/* story_force_level_transition -- S536, TYLER VALHANNA's own new, real "exit" primitive
 * (episodes/vh01_valhanna_coldopen.md: "the exit needs to be a new primitive... scriptable...
 * call it when tyler is done feeding the paper back in and hits the button"). Factored straight
 * out of story_check_level_exits' own real, already-proven transition body (S473/S477/S491) --
 * every real line below (fetch export, apply, respawn everyone, target-spawner override, debounce
 * + log) is byte-identical to what a proximity exit already does, just callable directly instead
 * of gated behind a radius check. This is the genuinely new part: a scripted-sequence coordinator
 * (packages/simulation/tyler_coldopen.c) can now fire a real level transition the instant its own
 * final beat completes, with no LevelExit volume authored at all -- exactly the "spawn but no
 * exit until the scripted event fires" shape the cold open needs, since a non-colliding
 * STATE_SPECTATOR wisp has no reliable way to "walk into" a trigger volume to begin with. Returns
 * 1 on a real, successful transition, 0 on a fetch failure (still debounced either way, matching
 * story_check_level_exits' own existing failure-debounce behavior). */
static int story_force_level_transition(int next_id, int target_spawner_id, unsigned int now_ms) {
    if (next_id <= 0) return 0;
    if (now_ms - g_story_last_level_transition_ms < STORY_LEVEL_TRANSITION_DEBOUNCE_MS) return 0;

    CustomLevelData lvl;
    if (!level_boxes_fetch_export(next_id, &lvl)) {
        NET_SERVER_LOG("STORY_LEVEL_TRANSITION_FAILED next_level_id=%d -- export fetch failed", next_id);
        /* Real debounce even on failure -- an unreachable next level must not be hammered with a
           real curl fetch every single tick this keeps getting called. */
        g_story_last_level_transition_ms = now_ms;
        return 0;
    }
    server_apply_custom_level(&lvl);
    for (int pi = 0; pi < MAX_CLIENTS; pi++) {
        PlayerState *p = &local_state.players[pi];
        if (!p->active) continue;
        p->scene_id = g_server_match_scene;
        phys_respawn(p, now_ms);
        // S491, founder real-time -- GTA-style building interiors: "how can i specify which
        // spawner the exit leads to for the seamless experience of exiting the building." A real,
        // surgical override: phys_respawn already did its full normal reset (health/state/ammo/
        // team-or-FFA spawn selection); if the caller named a specific target spawner AND it
        // still exists in the destination level, just overwrite the resulting position with that
        // exact spot. A stale/missing target (custom_level_pick_spawner_by_id returns 0) is a
        // real, honest, non-fatal miss -- the normal spawn position phys_respawn already computed
        // stands, never a crash or silent mis-teleport.
        if (target_spawner_id > 0) {
            float tx, ty, tz;
            if (custom_level_pick_spawner_by_id(target_spawner_id, &tx, &ty, &tz)) {
                p->x = tx; p->y = ty; p->z = tz;
            }
        }
    }
    tyler_apply_phase_override(lvl.name, now_ms);
    g_story_last_level_transition_ms = now_ms;
    NET_SERVER_LOG("STORY_LEVEL_TRANSITION next_level_id=%d name=%s target_spawner_id=%d", next_id, lvl.name, target_spawner_id);
    return 1;
}

static void story_check_level_exits(unsigned int now_ms) {
    if (g_story_level_exit_count <= 0 || g_story_next_level_id <= 0) return;
    if (now_ms - g_story_last_level_transition_ms < STORY_LEVEL_TRANSITION_DEBOUNCE_MS) return;

    int triggered = 0;
    int triggered_target_spawner_id = 0; /* S491: which exit fired, if any, carries its own target */
    for (int pi = 0; pi < MAX_CLIENTS && !triggered; pi++) {
        PlayerState *p = &local_state.players[pi];
        if (!p->active || p->state == STATE_DEAD) continue;
        for (int i = 0; i < g_story_level_exit_count; i++) {
            LevelExit *ex = &g_story_level_exits[i];
            float dx = p->x - ex->x, dy = p->y - ex->y, dz = p->z - ex->z;
            float dist2 = dx * dx + dy * dy + dz * dz;
            if (dist2 <= ex->radius * ex->radius) {
                triggered = 1;
                triggered_target_spawner_id = ex->target_spawner_id;
                break;
            }
        }
    }
    if (!triggered) return;
    story_force_level_transition(g_story_next_level_id, triggered_target_spawner_id, now_ms);
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
static int g_queue_level_id = -1; // registry id QUEUE is simulating (-1 = oil-tanker fallback); broadcast as PACKET_QUEUE_LEVEL, card #481
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
        g_queue_level_id = found_id;
        NET_SERVER_LOG("QUEUE_LEVEL_LOADED name=%s id=%d boxes=%d", lvl.name, found_id, lvl.count);
        return;
    }
    NET_SERVER_LOG("QUEUE_LEVEL_FALLBACK reason=%s -- using SCENE_OIL_TANKER",
                   found_id < 0 ? "level_44_not_found" : "export_fetch_failed");
    g_server_match_scene = SCENE_OIL_TANKER;
    g_queue_level_id = -1;
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

/* server_relay_spray_decal -- card #659, see PACKET_SPRAY_DECAL in protocol.h. The datagram is untrusted:
 * only a live, welcomed slot may send one, every float must be finite and inside a sane world box, the
 * normal must be roughly unit length, and a sender is limited to ~8 decals/second. The relayed copy
 * carries the server's own scene_id for the sender. */
static void server_relay_spray_decal(int from, const char *buffer, int size) {
    if (from <= 0 || from >= MAX_CLIENTS || size < (int)sizeof(NetSprayDecal)) return;
    if (!slots[from].active || !slots[from].welcomed || !local_state.players[from].active) return;
    NetSprayDecal d;
    memcpy(&d, buffer, sizeof(d));
    float v[6] = { d.x, d.y, d.z, d.nx, d.ny, d.nz };
    for (int k = 0; k < 6; k++) {
        if (!(v[k] == v[k]) || v[k] > 100000.0f || v[k] < -100000.0f) return; /* NaN / inf / absurd */
    }
    float nl = d.nx * d.nx + d.ny * d.ny + d.nz * d.nz;
    if (nl < 0.8f || nl > 1.2f) return;
    if (d.spray_id < 0 || d.spray_id > 100000000) return;
    static unsigned int last_ms[MAX_CLIENTS];
    unsigned int now_ms = get_server_time();
    if (last_ms[from] != 0 && now_ms - last_ms[from] < 125) return;
    last_ms[from] = now_ms;
    int scene = local_state.players[from].scene_id;
    d.hdr.type = PACKET_SPRAY_DECAL;
    d.hdr.client_id = (unsigned char)from;
    d.hdr.scene_id = (unsigned char)scene;
    d.hdr.timestamp = now_ms;
    for (int i = 1; i < MAX_CLIENTS; i++) {
        if (i == from || !slots[i].active || !slots[i].welcomed) continue;
        if (local_state.players[i].scene_id != scene) continue;
        sendto(sock, &d, sizeof(d), 0, (struct sockaddr*)&slots[i].addr, sizeof(struct sockaddr_in));
    }
    NET_SERVER_LOG("SPRAY_DECAL_RELAY from=%d spray=%d scene=%d", from, d.spray_id, scene);
}

static int ensure_slot_for_sender(const struct sockaddr_in *sender, int allow_alloc) {
    int slot = find_slot_by_addr(sender);
    if (slot != -1) {
        slots[slot].last_heard = now_seconds();
        local_state.client_meta[slot].last_heard_ms = get_server_time();
        char addr_buf[64];
        net_format_addr(sender, addr_buf, sizeof(addr_buf));
        NET_SERVER_LOG("SLOT_REUSE client_id=%d addr=%s", slot, addr_buf);
        return slot;
    }

    if (!allow_alloc) {
        // Real, found-live fix: a bare PACKET_USERCMD (or PACKET_DISCONNECT) from a sender with
        // no existing slot used to fall through to alloc_slot() below just like PACKET_CONNECT
        // does -- but that path never calls send_welcome(), so it silently created a permanent,
        // un-welcomed "phantom" slot that occupies a client_id forever without ever becoming a
        // real, active player (local_state.players[id].active requires welcomed && cmd_seen).
        // Reproduced live under real CPU contention (a concurrent PPO training process starving
        // the bot's own tick loop past the server's 5s SLOT_TIMEOUT): the server frees the slot,
        // but the bot's next already-in-flight UserCmd packet arrives before its own "no packet
        // in 5s" reconnect check fires, lands here, and used to mint a fresh phantom slot instead
        // of being dropped -- so the client's real reconnect CONNECT (once it did fire) landed on
        // a DIFFERENT/new slot than the phantom one still quietly occupying a client_id, and the
        // RL env's own _wait_for_alive_snapshot() could spin forever never finding itself welcomed.
        // A stray USERCMD/DISCONNECT with no real CONNECT behind it should just be dropped --
        // only a real PACKET_CONNECT is allowed to mint a new slot.
        char addr_buf[64];
        net_format_addr(sender, addr_buf, sizeof(addr_buf));
        NET_WARN_LOG("UNKNOWN_SENDER_DROPPED addr=%s", addr_buf);
        return -1;
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
        } else if (strcmp(argv[i], "--tyler") == 0) {
            /* S536, TYLER VALHANNA -- same real gap --story's own comment above already named
             * and closed for MODE_STORY: without a flag, MODE_TYLER is real in the enum but
             * unreachable on the dedicated server. */
            mode = MODE_TYLER;
        } else if (strcmp(argv[i], "--zombies") == 0) {
            /* Same gap, one mode further along: MODE_ZOMBIES is real in the enum, real in the
             * lobby (`lobby --zombies`), and reachable on a dedicated server only through this
             * flag. Without it the citizen bodies (citizen_actor.c) had nowhere to be simulated
             * except one machine's local match, which is exactly the "syncs to the network" half
             * the citizens were missing. */
            mode = MODE_ZOMBIES;
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
    g_server_sock = sock;
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
    /* WOW WIRE: a cast accepted since the last command becomes THIS command's trigger pull, once.
       Folding it into the usercmd is deliberate -- the shot happens on a client tick, so it is
       ordered against that client's own movement exactly like a mouse click, and it cannot fire
       from the receive path (outside the simulation). The weapon gate is the engine's own: a
       weapon this player does not own is not equipped by a macro either. */
    if (g_wow_server_inited && client_id >= 0 && client_id < WOW_SERVER_CLIENTS) {
        WowCastIntent cast;
        if (wow_server_take_cast(&g_wow_server, client_id, &cast)) {
            int w = (int)cast.weapon;
            if (w >= 0 && w < MAX_WEAPONS) {
                p->current_weapon = w;
                /* Aim, not just fire. The entity id the client's target slot holds is the player slot
                   here, which is the same pool the simulation ticks, so a cast at [@target] points the
                   shot at that entity instead of wherever the player's own crosshair was. Anything the
                   simulation cannot vouch for (out of range, dead, other scene, self) is left alone:
                   the shot fires along the player's own aim rather than a crosshair that teleports. */
                if (cast.token == WOW_TOKEN_TARGET || cast.token == WOW_TOKEN_FOCUS) {
                    int slot = (cast.token == WOW_TOKEN_FOCUS) ? 1 : 0;
                    int e = wow_targets_get(&g_wow_server.c[client_id].targets, slot);
                    if (e > 0 && e < MAX_CLIENTS && e != client_id) {
                        PlayerState *t = &local_state.players[e];
                        if (t->active && t->state != STATE_DEAD && t->scene_id == p->scene_id) {
                            /* Range is a GAME RULE written in yards, PER WEAPON (PARENA
                               wow_cast_range_yd_for) and converted through the unit layer -- no metre
                               value is restated here. The comparison is 3D on purpose, so elevation is
                               paid out of the same budget: a target 12 m up and 10 m away is 15.6 m
                               from the muzzle, inside a rifle's 55 yd (50.29 m), while one straight
                               overhead at 40 m is out. Past range the shot fires along the player's
                               own aim: a client's word must not swing the server's crosshair. */
                            float range_m = units_author_length_m(
                                (float)wow_cast_range_yd_for((int)cast.weapon), 5);
                            float ddx = t->x - p->x, ddy = t->y - p->y, ddz = t->z - p->z;
                            if (ddx * ddx + ddy * ddy + ddz * ddz <= range_m * range_m) {
                                tp_yaw_pitch_to_point(p->x, p->y + EYE_HEIGHT, p->z,
                                                      t->x, t->y + EYE_HEIGHT, t->z,
                                                      &p->yaw, &p->pitch);
                            } else {
                                NET_SERVER_LOG("WOW_CAST client_id=%d target=%d out of range (%.2f m > %.2f m)",
                                               client_id, e,
                                               sqrtf(ddx * ddx + ddy * ddy + ddz * ddz), range_m);
                            }
                        }
                    }
                }
                p->in_shoot = 1;
                NET_SERVER_LOG("WOW_CAST client_id=%d weapon=%d token=%d", client_id, w, (int)cast.token);
            }
        }
    }
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

/* ---- VENDING (PARENA icecream_rules.prn, packages/simulation/vendor_serve.h) ----
 * The window is SERVER-AUTHORITATIVE, because this file already owns every player's health: the cone is a
 * health change like any other, so it belongs where the health lives rather than on whichever client is
 * standing closest. The client runs the same state machine only to draw the progress ring, and applies
 * nothing (apps/lobby gates its own heal on sock < 0, i.e. no server to ask).
 *
 * Why this is not a packet: a buy REQUEST would need the server to re-check reach, the truck being stopped
 * and the buyer's cooldown anyway -- and it can see all three itself. So a client cannot buy a cone it did
 * not stand for, cannot buy two, and cannot buy one from a truck that is driving off; a reconnect does not
 * matter either, because the health it heals is the state the snapshot already carries.
 *
 * dt is clamped: a server stall must not hand out a cone (or five) in one tick. */
static VendorServe g_vendor_serve[MAX_CLIENTS];
static int g_vendor_serve_ready;
static unsigned int g_vendor_serve_last_ms;

static void server_vendor_tick(unsigned int now_ms) {
    if (!g_vendor_serve_ready) {
        for (int i = 0; i < MAX_CLIENTS; i++) vendor_serve_init(&g_vendor_serve[i]);
        g_vendor_serve_ready = 1;
    }
    int dt_ms = g_vendor_serve_last_ms ? (int)(now_ms - g_vendor_serve_last_ms) : 16;
    if (dt_ms < 0 || dt_ms > 200) dt_ms = 16;
    g_vendor_serve_last_ms = now_ms;

    for (int cid = 0; cid < MAX_CLIENTS; cid++) {
        PlayerState *p = &local_state.players[cid];
        if (!p->active || p->is_bot || p->state != STATE_ALIVE) {
            /* a buyer who left, died or was never a person banks nothing and keeps no queue */
            if (g_vendor_serve[cid].truck_id != VENDOR_SERVE_NONE || g_vendor_serve[cid].held_ms) {
                vendor_serve_init(&g_vendor_serve[cid]);
            }
            continue;
        }
        /* WHO the nearest truck is and WHAT the cone does are one function (vendor_serve_authority_tick),
           next to the state machine, so the wiring -- the scene filter, the nearest vendor, the heal landing
           on the AUTHORITATIVE health -- is covered headless by `make test-vendor-sale` rather than only
           compiled. This file keeps the part that is genuinely the server's: who counts as a buyer. */
        int truck = VENDOR_SERVE_NONE;
        int before = p->health;
        int served = vendor_serve_authority_tick(&g_vendor_serve[cid], &p->health, 100,
                                                 p->x, p->z, p->scene_id,
                                                 local_state.buggies, MAX_BUGGIES, dt_ms, &truck);
        if (served) {
            NET_SERVER_LOG("VENDOR_SALE client_id=%d truck=%d health=%d->%d", cid, truck, before, p->health);
            (void)before;   /* read only by the log above, which the macro strips when verbose is off */
        }
    }
}

void server_handle_packet(struct sockaddr_in *sender, char *buffer, int size) {
    if (size < (int)sizeof(NetHeader)) {
        g_net_diag.malformed++;
        NET_WARN_LOG("SHORT_PACKET size=%d min=%zu", size, sizeof(NetHeader));
        return;
    }
    NetHeader *head = (NetHeader*)buffer;
    int client_id = -1;

    if (head->type == PACKET_CONNECT || head->type == PACKET_USERCMD || head->type == PACKET_DISCONNECT || head->type == PACKET_SPRAY_DECAL || head->type == PACKET_KIT_CALL || head->type == PACKET_WOW_WIRE || head->type == PACKET_HORSE_INTERACT) {
        client_id = ensure_slot_for_sender(sender, head->type == PACKET_CONNECT);
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
        (void)mode_before;   /* read only by the CONNECT_ACCEPT log below, which the logging macro strips
                                when verbose is off; the capture must survive either way, so this is a
                                use, not a dead variable to delete */
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
        /* TEST HOOKS -- named, off by default, the convention this project already uses for scripted runs
           (SHANKPIT_TEST_ZOMBIE_PAIR). A scripted client has no keyboard, so it cannot walk to a truck, and a
           fresh player spawns at 100/100, so a 25-point cone would be invisible in the snapshot. These two
           knobs are what let a REAL client observe a REAL sale over a REAL wire:
             SHANKPIT_TEST_START_HEALTH=N   sets this player's health at CONNECT
             SHANKPIT_TEST_VENDOR_AT=dx,dz  moves the vendor dx,dz from this player and holds its brakes
           Both are read once, at CONNECT, and change nothing at all when unset. */
        {
            const char *sh = getenv("SHANKPIT_TEST_START_HEALTH");
            if (sh && sh[0]) {
                int v = atoi(sh);
                if (v < 1) v = 1;
                if (v > 100) v = 100;
                p->health = v;
                /* and make the scripted buyer a LEGAL buyer: this path does not go through phys_respawn in
                   every mode, so without this the cone would be refused for a reason the test is not about */
                if (p->state != STATE_ALIVE) p->state = STATE_ALIVE;
                NET_SERVER_LOG("TEST_START_HEALTH client_id=%d health=%d", client_id, p->health);
            }
            const char *vend = getenv("SHANKPIT_TEST_VENDOR_AT");
            if (vend && vend[0]) {
                float dx = 2.0f, dz = 0.0f;
                if (sscanf(vend, "%f,%f", &dx, &dz) != 2) { dx = 2.0f; dz = 0.0f; }
                BuggyState *b = NULL;
                for (int bi = 0; bi < MAX_BUGGIES && !b; bi++)
                    if (local_state.buggies[bi].active && local_state.buggies[bi].ice_cream)
                        b = &local_state.buggies[bi];
                /* A test world may have no vendor in it at all (the default arena spawns three plain buggies
                   and no level vehicles, so the LEVEL's own [VENDOR] block never runs). The knob therefore
                   puts the truck there itself rather than betting the proof on a level this test does not
                   own -- the real spawn path is covered by local_game's block and by test-vendor-sale. */
                if (!b) {
                    for (int bi = 0; bi < MAX_BUGGIES && !b; bi++) {
                        if (local_state.buggies[bi].active) continue;
                        b = &local_state.buggies[bi];
                        buggy_spawn_defaults(b, bi, p->scene_id, p->x + dx, p->z + dz, 0.0f);
                        b->ice_cream = 1;
                        b->song = 1;
                        b->rocket = 1;
                        b->tank = phys_rocket_tank_max();
                        NET_SERVER_LOG("TEST_VENDOR_SPAWN client_id=%d truck=%d", client_id, b->id);
                    }
                }
                if (b) {
                    b->x = p->x + dx;
                    b->z = p->z + dz;
                    b->y = p->y;
                    b->vx = b->vy = b->vz = 0.0f;
                    /* the buyer stands in the TRUCK's scene, whatever the connect default happened to be */
                    p->scene_id = b->scene_id;
                    /* no lane, and the test hold: the truck stands still instead of lapsing into a ring */
                    police_patrol_clear_lane(b->id);
                    police_patrol_set_anchor(b->id, b->x, b->z, 2.0f);
                    g_vendor_test_hold = 1;
                    NET_SERVER_LOG("TEST_VENDOR_AT client_id=%d truck=%d at=%.2f,%.2f", client_id, b->id,
                                   (double)b->x, (double)b->z);
                }
            }
        }
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

    if (client_id != -1 && head->type == PACKET_SPRAY_DECAL) {
        server_relay_spray_decal(client_id, buffer, size);
        return;
    }

    /* --- KIT CALLS ---
     * Card N1 (docs/NAMED_NOT_DONE.md): the kit's vehicle gadget keys were local/sandbox only, so in a
     * networked match pressing `` ` `` did nothing at all. The client sends which call item it pressed and
     * the server decides -- it is the one that owns every other vehicle, and the spawn it makes reaches
     * every client through the ordinary snapshot. kit_call_parse refuses anything that is not a
     * well-formed call for a vehicle item, and kit_call_server_apply refuses a dead or spectating caller
     * (plus every refusal vehicle_call_spawn already makes: already driving, on cooldown, garage full). */
    if (client_id != -1 && head->type == PACKET_KIT_CALL) {
        slots[client_id].last_heard = now_seconds();
        local_state.client_meta[client_id].last_heard_ms = get_server_time();
        if (!slots[client_id].welcomed) {
            g_net_diag.malformed++;
            NET_WARN_LOG("KIT_CALL_REJECTED client_id=%d reason=not_welcomed", client_id);
            return;
        }
        int item = kit_call_parse(buffer, size);
        if (item == KIT_ITEM_NONE) {
            g_net_diag.malformed++;
            NET_WARN_LOG("KIT_CALL_MALFORMED client_id=%d size=%d", client_id, size);
            return;
        }
        int spawned = kit_call_server_apply(&local_state.players[client_id], item);
        (void)spawned;   /* read only by the KIT_CALL log, which compiles out when verbose is off */
        NET_SERVER_LOG("KIT_CALL client_id=%d item=%s -> veh=%d scene=%d", client_id, kit_item_name(item),
                       spawned, local_state.players[client_id].scene_id);
        return;
    }

    /* --- HORSE INTERACT (PACKET_HORSE_INTERACT) ---
     * A REQUEST against one horse: the server owns the horse, so it decides. Four verbs share the
     * channel -- mount and dismount (the player's own E key) and SET_GOAL / FEED (what a MOD asks for,
     * so "lead this horse home" and "give it a carrot" are reachable from a client at all). The whole
     * policy is packages/simulation/horse_interact.h's horse_interact_apply, which the headless test
     * asserts against: reach, scene, occupancy and goal range are checked there, in one place, so a
     * remote request and a local key press cannot diverge. Spawning is deliberately NOT here -- a
     * client must not be able to mint animals; that stays authority-side (horse_api_spawn). A refusal
     * is silent on the wire -- the horse does not move and the next PACKET_HORSE_STATE says so, which
     * is why there is no ack to lose. Jump and carrot-in-the-saddle are not here either:
     * BTN_JUMP/BTN_ABILITY_2 already travel in UserCmd and the horse tick reads them off its
     * occupant. */
    if (client_id != -1 && head->type == PACKET_HORSE_INTERACT) {
        slots[client_id].last_heard = now_seconds();
        local_state.client_meta[client_id].last_heard_ms = get_server_time();
        if (!slots[client_id].welcomed || size < (int)sizeof(NetHorseInteract)) {
            g_net_diag.malformed++;
            NET_WARN_LOG("HORSE_INTERACT_REJECTED client_id=%d reason=%s", client_id,
                         slots[client_id].welcomed ? "short" : "not_welcomed");
            return;
        }
        NetHorseInteract *hi = (NetHorseInteract *)buffer;
        PlayerState *hp = &local_state.players[client_id];
        int verdict = horse_interact_apply(&local_state, hp, (int)hi->slot, (int)hi->action,
                                           (int)hi->goal, get_server_time());
        if (verdict > 0) {
            NET_SERVER_LOG("HORSE_INTERACT client_id=%d horse=%u action=%u goal=%u verdict=ok",
                           client_id, (unsigned)hi->slot, (unsigned)hi->action, (unsigned)hi->goal);
        } else {
            /* A request naming a slot or a verb that does not exist is a malformed datagram, not a
               policy refusal -- count it the way every other bad packet is counted. */
            if (hi->slot >= MAX_HORSES || hi->action >= HORSE_INTERACT_ACTION_COUNT)
                g_net_diag.malformed++;
            NET_SERVER_LOG("HORSE_INTERACT client_id=%d horse=%u action=%u goal=%u verdict=%d",
                           client_id, (unsigned)hi->slot, (unsigned)hi->action,
                           (unsigned)hi->goal, verdict);
        }
        return;
    }

    // --- WOW WIRE: per-client targets, casts, macro bytecode (packages/protocol/wow_server.h) ---
    if (client_id != -1 && head->type == PACKET_WOW_WIRE) {
        NetWowWire *w = (NetWowWire *)buffer;
        uint8_t ack[WOW_WIRE_ACK_BYTES];
        int r;
        if (!g_wow_server_inited) {
            wow_server_begin(&g_wow_server);
            /* Fail closed unless IDUNA's shared secret is actually present: with no secret there is no
               verifier and wow_server_rx refuses every op. Never invent one, never accept unauthenticated. */
            const char *secret = getenv("SHANKPIT_TICKET_SECRET");
            int sl = secret ? (int)strlen(secret) : 0;
            if (sl > (int)sizeof g_ticket_secret) {
                NET_SERVER_LOG("WOW_AUTH SHANKPIT_TICKET_SECRET is longer than %d bytes: refusing to install",
                               (int)sizeof g_ticket_secret);
            } else if (sl > 0) {
                memcpy(g_ticket_secret, secret, (size_t)sl);
                g_ticket_secret_len = sl;
                g_wow_server.auth = wow_ticket_auth;
                NET_SERVER_LOG("WOW_AUTH IDUNA ticket verifier installed (HMAC-SHA256, %d-byte secret)", sl);
            } else {
                NET_SERVER_LOG("WOW_AUTH no SHANKPIT_TICKET_SECRET: every wire op is refused (fail closed)");
            }
            g_wow_server_inited = 1;
        }
        if (client_id >= WOW_SERVER_CLIENTS) {
            static int warned = 0;
            if (!warned) { NET_WARN_LOG("WOW_WIRE ignored: slot %d is beyond the %d wire clients", client_id, WOW_SERVER_CLIENTS); warned = 1; }
            return;
        }
        if (size < (int)(sizeof(NetHeader) + 1) || w->len > (int)sizeof(w->bytes) ||
            (int)(sizeof(NetHeader) + 1 + w->len) > size) {
            g_net_diag.malformed++;
            return;
        }
        r = wow_server_rx(&g_wow_server, client_id, w->bytes, w->len, ack, (int)sizeof ack);
        if (r == -2) {
            /* -2 means "refused", and there are two different reasons: no verifier is installed, or an
               installed verifier rejected the ticket (bad MAC, or past its 5-minute expiry). They used to
               share one message that always blamed the verifier's absence, so a genuinely rejected ticket
               -- an expired one, say -- read as a server misconfiguration. Say which one it was. */
            if (!g_wow_server.auth) {
                static int warned = 0;
                if (!warned) { NET_WARN_LOG("WOW_WIRE refused: no IDUNA ticket verifier installed (fail closed)"); warned = 1; }
            } else {
                static unsigned int refused_auth = 0;
                refused_auth++;
                if (refused_auth <= 3 || (refused_auth % 100) == 0)
                    NET_WARN_LOG("WOW_AUTH_REFUSED client_id=%d ticket rejected by the installed verifier (bad MAC or expired) total=%u",
                                 client_id, refused_auth);
            }
            return;
        }
        if (r == 0) {
            /* Adjudicated and refused: the cast queue was full, the macro id does not exist, the weapon
               name is not one of ours, or the target intent was older than the one already applied. The
               wire stays SILENT on purpose -- an ACK means the server took the op, and ACKing a macro it
               did not store would be a lie -- so the client retries and then reports "never
               acknowledged", which is literally true and reads like packet loss. Log it here, or the
               refusal leaves no trace on either side but that misleading message. Counted, not spammed:
               a retrying client would otherwise fill the log with its own retransmissions. */
            static unsigned int refused = 0;
            refused++;
            if (refused <= 3 || (refused % 100) == 0)
                NET_WARN_LOG("WOW_REFUSED client_id=%d op=%u total=%u (silent on the wire by design)",
                             client_id, (unsigned)w->bytes[1], refused);
            return;
        }
        if (r > 0 && g_server_sock >= 0) {
            NetWowWire out;
            WowAck parsed;
            /* Loss injection for the E2E only. A real server drops an ACK only when the network does;
               `--drop-cast-acks N` makes the CLIENT's retry path observable by swallowing the next N
               CAST ACKs, so `make test-wow-e2e` proves the resend against a real socket instead of
               calling wow_client_retry() in a unit test and calling that "the wire". Default 0. */
            if (g_drop_cast_acks > 0 &&
                wow_ack_decode(ack, (int)sizeof ack, &parsed) == WOW_WIRE_ACK_BYTES &&
                parsed.kind == WOW_OP_CAST) {
                g_drop_cast_acks--;
                NET_WARN_LOG("WOW_ACK dropped on purpose (--drop-cast-acks), %d left", g_drop_cast_acks);
                return;
            }
            memset(&out, 0, sizeof out);
            out.hdr.type = PACKET_WOW_WIRE;
            out.len = (unsigned char)sizeof ack;
            memcpy(out.bytes, ack, sizeof ack);
            sendto(g_server_sock, (const char *)&out, (int)(sizeof(NetHeader) + 1 + sizeof ack), 0,
                   (struct sockaddr *)sender, sizeof *sender);
        }
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
            np.forced_kit = (unsigned char)p->forced_kit;
            np.third_person = (unsigned char)(p->third_person ? 1 : 0);
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

/* server_broadcast_world_clock -- EMILY/BACKLOG.md SECTION 536 follow-up ("server-authoritative
 * day/night sync"). Real, separate packet from server_broadcast's own PACKET_SNAPSHOT on purpose
 * -- does not touch that function's buffer sizing/count bytes/entity serialization at all, zero
 * risk to the live deathmatch snapshot path. MODE_STORY/MODE_STORY_CAVE only; every other mode
 * returns immediately, sending nothing. Not yet live-deployed anywhere (see this repo's own
 * CLAUDE.md: shankpit-server.service runs --deathmatch only), but real, complete, and ready the
 * moment a dedicated --story server is. */
void server_broadcast_world_clock(void) {
    if (local_state.game_mode != MODE_STORY && local_state.game_mode != MODE_STORY_CAVE) return;
    NetWorldClock wc;
    memset(&wc, 0, sizeof(wc));
    wc.hdr.type = PACKET_WORLD_CLOCK;
    wc.hdr.client_id = 0;
    wc.hdr.timestamp = get_server_time();
    wc.minutes = local_state.story_clock.minutes;
    wc.start_minute = local_state.story_clock.start_minute;
    wc.weather = (unsigned char)local_state.story_clock.weather;
    wc.weather_ends = local_state.story_clock.weather_ends;
    for (int i = 1; i < MAX_CLIENTS; i++) {
        if (!slots[i].active || !slots[i].welcomed) continue;
        sendto(sock, &wc, sizeof(wc), 0, (struct sockaddr*)&slots[i].addr, sizeof(struct sockaddr_in));
    }
}

/* server_broadcast_queue_level -- card #481, see PACKET_QUEUE_LEVEL in protocol.h. MODE_QUEUE only;
 * throttled to 1 Hz (the level only changes at a round boundary, the repeat heals a dropped datagram
 * and a late joiner). Separate packet from the snapshot, same zero-risk reasoning as the others. */
void server_broadcast_queue_level(void) {
    if (local_state.game_mode != MODE_QUEUE) return;
    static unsigned int last_ms = 0;
    unsigned int now_ms = get_server_time();
    if (last_ms != 0 && (now_ms - last_ms) < 1000) return;
    last_ms = now_ms;
    NetQueueLevel pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.hdr.type = PACKET_QUEUE_LEVEL;
    pkt.hdr.client_id = 0;
    pkt.hdr.timestamp = now_ms;
    pkt.level_id = g_queue_level_id;
    for (int i = 1; i < MAX_CLIENTS; i++) {
        if (!slots[i].active || !slots[i].welcomed) continue;
        sendto(sock, &pkt, sizeof(pkt), 0, (struct sockaddr*)&slots[i].addr, sizeof(struct sockaddr_in));
    }
}

/* ---- shared procedural zones, server side -------------------------------------------------------
 * Founder: "the first person to enter it it generates and then everyone else enters the same one ...
 * generated and then they exist until everyone leaves + 15 minutes". The server owns the zone and the
 * clock (packages/simulation/zone_registry.h); clients get its IDENTITY over PACKET_ZONE_STATE and
 * rebuild the identical layout from the seed (zone_sync.h), so no geometry crosses the wire. Occupancy is
 * the count of connected players: the first one in generates, the last one out starts the countdown. */
/* LAN discovery (packages/simulation/zone_beacon.h): where friends find this world. Not part of
   the game protocol -- its own port, identity only, no trust -- and absent when --beacon-port 0. */
#include "zone_beacon.h"
static int g_server_beacon_sock = -1;
static int g_server_beacon_port = ZONE_BEACON_PORT;
static int g_server_game_port = 6969;

static ZoneRegistry g_server_zones;
static int g_server_zones_ready = 0;
static char g_server_zone_id[NET_ZONE_ID_MAX] = "";
static unsigned int g_server_zone_seed = 0;
static int g_server_zone_seen_players = 0;

static void server_zone_expired(void *user, const char *zone_id) {
    char path[256];
    (void)user;
    snprintf(path, sizeof path, "var/maps/proc_%s.json", zone_id ? zone_id : "zone");
    remove(path);
    printf("[ZONE] '%s' was empty past its grace period -- layout deleted, next entrant regenerates\n",
           zone_id ? zone_id : "?");
}

static void server_zone_generate_and_apply(const char *zone_id, unsigned int seed) {
    static CustomLevelData lvl;
    char path[256];
    FILE *probe;
    proc_dungeon_generate(&lvl, zone_id, seed);
    probe = fopen("var/maps", "rb");
    if (probe) fclose(probe);
    else {
        /* The save below reports its own failure, but not why: without this the operator cannot tell a
           missing directory from an unwritable one, and "could not write" is the only clue. */
        int rc = system("mkdir -p var/maps");
        if (rc != 0) printf("[ZONE] could not create var/maps (system() returned %d)\n", rc);
    }
    snprintf(path, sizeof path, "var/maps/proc_%s.json", zone_id);
    if (!proc_dungeon_save(path, &lvl)) printf("[ZONE] could not write %s\n", path);
    server_apply_custom_level(&lvl);
    /* Copy only when the source is NOT the destination: snprintf with overlapping buffers is undefined,
       and the first version did exactly that (g_server_zone_id into itself), which glibc turned into an
       empty id -- a server that generated a zone and then advertised ''. */
    if (zone_id != g_server_zone_id) snprintf(g_server_zone_id, sizeof g_server_zone_id, "%s", zone_id);
    g_server_zone_seed = seed;
    printf("[ZONE] generated '%s' seed %08x (%d boxes, difficulty %d)\n",
           zone_id, seed, lvl.count, g_zone_rules.difficulty((int)seed));
}

static void server_zone_begin(const char *zone) {
    const char *id = (zone && zone[0]) ? zone : "dungeon_1";
    unsigned int seed = 0;
    int created;
    if (!g_server_zones_ready) {
        zone_registry_init(&g_server_zones, (unsigned int)get_server_time() | 1u);
        g_server_zones_ready = 1;
    }
    created = zone_registry_enter(&g_server_zones, id, (int)get_server_time(), &seed);
    if (created < 0) { printf("[ZONE] no free slot for '%s'\n", id); g_server_zone_id[0] = 0; return; }
    g_server_zone_seen_players = 1;   /* the first tick with nobody connected releases it to idle */
    server_zone_generate_and_apply(id, seed);
}

void server_broadcast_zone_state(void) {
    static unsigned int last_ms = 0;
    unsigned int now_ms = get_server_time();
    int connected = 0, state;
    NetZoneState pkt;

    if (!g_server_zones_ready || !g_server_zone_id[0]) return;
    for (int i = 1; i < MAX_CLIENTS; i++)
        if (slots[i].active && slots[i].welcomed && slots[i].cmd_seen && local_state.players[i].active) connected++;

    state = zone_registry_state(&g_server_zones, g_server_zone_id);
    if (connected > 0 && state == ZONE_STATE_UNINIT) {
        unsigned int seed = 0;
        if (zone_registry_enter(&g_server_zones, g_server_zone_id, (int)now_ms, &seed) == 1)
            server_zone_generate_and_apply(g_server_zone_id, seed);
    } else if (connected > g_server_zone_seen_players) {
        for (int n = g_server_zone_seen_players; n < connected; n++)
            zone_registry_enter(&g_server_zones, g_server_zone_id, (int)now_ms, NULL);
    } else if (connected < g_server_zone_seen_players) {
        for (int n = connected; n < g_server_zone_seen_players; n++)
            zone_registry_leave(&g_server_zones, g_server_zone_id, (int)now_ms);
    }
    g_server_zone_seen_players = connected;
    zone_registry_tick(&g_server_zones, (int)now_ms, server_zone_expired, NULL);
    state = zone_registry_state(&g_server_zones, g_server_zone_id);

    if (last_ms != 0 && (now_ms - last_ms) < 1000) return;
    last_ms = now_ms;
    zone_sync_fill(&pkt, g_server_zone_id, g_server_zone_seed, connected, state,
                   (unsigned int)zone_registry_idle_ms(&g_server_zones, g_server_zone_id, (int)now_ms), now_ms);
    for (int i = 1; i < MAX_CLIENTS; i++) {
        if (!slots[i].active || !slots[i].welcomed) continue;
        sendto(sock, &pkt, sizeof(pkt), 0, (struct sockaddr*)&slots[i].addr, sizeof(struct sockaddr_in));
    }

    /* Same 1 Hz beat as the packet above, so a finder hears about a world at most a second after it
       exists. Announced only while the zone is live: an expired zone has nothing to advertise, and a
       beacon for a dead world would send allies to a host that cannot generate it. */
    if (g_server_beacon_sock >= 0 && state != ZONE_STATE_UNINIT && g_server_zone_id[0]) {
        ZoneBeacon beacon;
        memset(&beacon, 0, sizeof beacon);
        snprintf(beacon.zone, sizeof beacon.zone, "%s", g_server_zone_id);
        beacon.seed = g_server_zone_seed;
        beacon.players = connected;
        beacon.port = g_server_game_port; /* the port this process really bound, not a constant */
        zone_beacon_announce(g_server_beacon_sock, g_server_beacon_port, &beacon);
    }
}

/* server_broadcast_empire_state -- the EMPIRE shared world's mutable state (per-faction stockpiles, each
 * node's remaining amount, who holds which ground), server -> client, 1 Hz, on the same beat as the zone
 * identity above. empire_sync.h owns the packet layout; this function owns only "who is still connected"
 * and "is there any EMPIRE in this world at all". The host is authoritative -- a client mirrors what it
 * hears and never gathers or claims the shared world itself. */
void server_broadcast_empire_state(void) {
    static unsigned int last_ms = 0;
    unsigned int now_ms = get_server_time();
    NetEmpireState pkt;

    if (story_ai_empire_unit_count() <= 0) return; /* nothing to report: a pre-EMPIRE world stays silent */
    if (last_ms != 0 && (now_ms - last_ms) < 1000) return;
    last_ms = now_ms;
    empire_sync_fill(&pkt, story_ai_empire_world(), now_ms);
    for (int i = 1; i < MAX_CLIENTS; i++) {
        if (!slots[i].active || !slots[i].welcomed) continue;
        sendto(sock, &pkt, sizeof(pkt), 0, (struct sockaddr*)&slots[i].addr, sizeof(struct sockaddr_in));
    }
}

/* server_shield_packs_tick -- card #541. MODE_QUEUE only. A player dying has a SHIELD_PACK_DROP_PERCENT chance
 * to leave a pack where they fell; any living player standing on a pack whose shield is below full takes it and
 * is refilled to full. Shields still do not regenerate (founder: "leave it like that") -- a pack is the only way
 * back up. The server rolls and grants; clients only render PACKET_SHIELD_PACKS. */
static void server_shield_packs_tick(void) {
    if (local_state.game_mode != MODE_QUEUE || local_state.match_over) return;
    for (int i = 1; i < MAX_CLIENTS; i++) {
        PlayerState *p = &local_state.players[i];
        if (!p->active || p->scene_id != g_server_match_scene) { shield_packs_note_player(i, 1, 0, 0, 0, 100); continue; }
        int alive = (p->state != STATE_DEAD);
        shield_packs_note_player(i, alive, p->x, p->y, p->z, rand() % 100);
        if (alive && p->shield < 100 && shield_packs_take(p->x, p->y, p->z)) {
            p->shield = 100;
            NET_SERVER_LOG("SHIELD_PACK_TAKEN player=%d", i);
        }
    }
}

/* server_broadcast_shield_packs -- card #541, see PACKET_SHIELD_PACKS in protocol.h. Whole set, every snapshot
 * interval, MODE_QUEUE only; the repeat is the heal for a dropped datagram or a late joiner. */
void server_broadcast_shield_packs(void) {
    if (local_state.game_mode != MODE_QUEUE) return;
    NetShieldPacks pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.hdr.type = PACKET_SHIELD_PACKS;
    pkt.hdr.client_id = 0;
    pkt.hdr.timestamp = get_server_time();
    int n = 0;
    for (int i = 0; i < SHIELD_PACK_MAX && n < NET_SHIELD_PACK_MAX; i++) {
        const ShieldPack *sp = shield_packs_get(i);
        if (!sp || !sp->active) continue;
        pkt.p[n].x = sp->x; pkt.p[n].y = sp->y; pkt.p[n].z = sp->z;
        n++;
    }
    pkt.count = (unsigned char)n;
    int len = (int)(offsetof(NetShieldPacks, p) + (size_t)n * sizeof(NetShieldPack));
    for (int i = 1; i < MAX_CLIENTS; i++) {
        if (!slots[i].active || !slots[i].welcomed) continue;
        sendto(sock, (const char *)&pkt, len, 0, (struct sockaddr*)&slots[i].addr, sizeof(struct sockaddr_in));
    }
}

/* server_broadcast_brick_state -- destructible brick (packages/simulation/brick_world.h), see
 * PACKET_BRICK_STATE in protocol.h. Sends this tick's brick-cell changes, scheduled repeats and a
 * slice of the rotating refresh to every welcomed client; sends nothing when no level with
 * destructible brick is loaded or nothing is due. Separate packet from the snapshot on purpose, same
 * reason server_broadcast_world_clock is: zero risk to the live snapshot path. */
void server_broadcast_brick_state(void) {
    if (!brick_world_is_active()) return;
    NetBrickState pkt;
    memset(&pkt, 0, sizeof(pkt));
    unsigned int now_ms = get_server_time();
    int n = brick_world_net_collect(pkt.e, NET_BRICK_MAX_ENTRIES, now_ms);
    if (n <= 0) return;
    pkt.hdr.type = PACKET_BRICK_STATE;
    pkt.hdr.client_id = 0;
    pkt.hdr.timestamp = now_ms;
    pkt.count = (unsigned char)n;
    int len = (int)(sizeof(NetHeader) + 4 + (size_t)n * sizeof(NetBrickEntry));
    for (int i = 1; i < MAX_CLIENTS; i++) {
        if (!slots[i].active || !slots[i].welcomed) continue;
        sendto(sock, (const char *)&pkt, len, 0, (struct sockaddr*)&slots[i].addr, sizeof(struct sockaddr_in));
    }
}

/* server_broadcast_vehicle_state -- which part of each car is still there (PACKET_VEHICLE_STATE,
 * packages/simulation/vehicle_salvage.h). Server -> client, alongside the snapshot, one packet per
 * welcomed client holding only the cars in THAT client's scene (the same filter the snapshot uses, so
 * a car in another scene is never described to someone who cannot see it).
 *
 * Absolute and idempotent by construction: the mask and the per-component hull points are the whole
 * state, so a dropped datagram or a late joiner heals on the next one, and no acknowledgement traffic
 * exists. hp travels as absolute points in a byte (no component is over 120) precisely so the client's
 * component sum still equals the hull it was sent -- a fraction would round, and the client's own
 * reconcile would read the rounding as damage and invent it. */
void server_broadcast_vehicle_state(void) {
    for (int i = 1; i < MAX_CLIENTS; i++) {
        if (!slots[i].active || !slots[i].welcomed) continue;
        int scene = local_state.players[i].scene_id;
        NetVehicleState pkt;
        memset(&pkt, 0, sizeof(pkt));
        int n = 0;
        for (int bi = 0; bi < MAX_BUGGIES && n < NET_VEHICLE_MAX_ENTRIES; bi++) {
            BuggyState *b = &local_state.buggies[bi];
            if (!b->active || b->scene_id != scene) continue;
            const SalvageVehicle *sv = &g_local_buggy_salvage[bi];
            if (!sv->active) continue;
            NetVehicleEntry *e = &pkt.e[n++];
            e->id = (unsigned char)bi;
            e->kind = (unsigned char)(b->rocket ? 1 : 0);
            /* police is a KIND, the siren is a STATE, and both are the server's to describe: a remote
               cruiser has to be a cruiser on the client, lightbar and wail included. */
            e->flags = (unsigned char)((b->police ? 0x01 : 0) | (b->siren ? 0x02 : 0) |
                                       (b->ice_cream ? 0x04 : 0) | (b->song ? 0x08 : 0));
            e->alive_mask = (unsigned char)sv->alive_mask;
            e->detached_mask = (unsigned char)sv->detached_mask;
            for (int c = 0; c < NET_VEHICLE_MAX_COMPONENTS; c++) {
                int hp = (c < SALVAGE_MAX_COMPONENTS) ? sv->hp[c] : 0;
                if (hp < 0) hp = 0;
                if (hp > NET_VEHICLE_HP_MAX) hp = NET_VEHICLE_HP_MAX;
                e->hp[c] = (unsigned char)hp;
            }
            e->seed = (unsigned int)(sv->entity_id ? sv->entity_id : 1);
        }
        if (n <= 0) continue;
        pkt.hdr.type = PACKET_VEHICLE_STATE;
        pkt.hdr.client_id = 0;
        pkt.hdr.timestamp = get_server_time();
        pkt.hdr.scene_id = (unsigned char)(scene < 0 ? 0 : scene);
        pkt.hdr.entity_count = (unsigned char)n;
        pkt.count = (unsigned char)n;
        int len = (int)(sizeof(NetHeader) + 4 + (size_t)n * sizeof(NetVehicleEntry));
        sendto(sock, (const char *)&pkt, len, 0, (struct sockaddr*)&slots[i].addr, sizeof(struct sockaddr_in));
    }
}

/* server_broadcast_horse_state -- the horse actors themselves (PACKET_HORSE_STATE, horse_sync.h).
 * Server -> client, alongside the snapshot, one packet per welcomed client holding only the horses in
 * THAT client's scene (the same filter server_broadcast_vehicle_state uses). Before this, a horse was
 * simulated and drawn only on the host that loaded the level, so a second player in the same town saw
 * an empty paddock. Whole-scene and absolute by construction: a horse the packet does not mention is
 * gone, so a dropped datagram or a late joiner heals on the next one, with no ack traffic. */
void server_broadcast_horse_state(void) {
    for (int i = 1; i < MAX_CLIENTS; i++) {
        if (!slots[i].active || !slots[i].welcomed) continue;
        int scene = local_state.players[i].scene_id;
        NetHorseState pkt;
        memset(&pkt, 0, sizeof(pkt));
        int n = horse_sync_fill(&local_state, scene, pkt.e, NET_HORSE_MAX_ENTRIES);
        if (n <= 0) continue;
        pkt.hdr.type = PACKET_HORSE_STATE;
        pkt.hdr.client_id = 0;
        pkt.hdr.timestamp = get_server_time();
        pkt.hdr.scene_id = (unsigned char)(scene < 0 ? 0 : scene);
        pkt.hdr.entity_count = (unsigned char)n;
        pkt.count = (unsigned char)n;
        int len = (int)(sizeof(NetHeader) + 4 + (size_t)n * sizeof(NetHorseEntry));
        sendto(sock, (const char *)&pkt, len, 0, (struct sockaddr*)&slots[i].addr, sizeof(struct sockaddr_in));
    }
}

/* server_broadcast_horse_piles -- what the horses in each client's scene left on the ground
 * (PACKET_HORSE_PILES, horse_sync.h). Same per-scene filter and the same whole-scene, idempotent
 * shape as server_broadcast_horse_state: the packet lists every live pile in that scene and a pile
 * it does not mention is gone. Piles never move, so only a CHANGE is sent -- g_horse_pile_rev moves
 * when one lands or expires, and a scene change re-sends too (including the empty list, which is how
 * a client learns that the last pile in the paddock is gone). */
void server_broadcast_horse_piles(void) {
    static unsigned int sent_rev[MAX_CLIENTS];
    static int sent_scene[MAX_CLIENTS];
    static int initialised = 0;
    if (!initialised) {
        for (int i = 0; i < MAX_CLIENTS; i++) sent_scene[i] = -2;
        initialised = 1;
    }
    for (int i = 1; i < MAX_CLIENTS; i++) {
        if (!slots[i].active || !slots[i].welcomed) continue;
        int scene = local_state.players[i].scene_id;
        if (scene == sent_scene[i] && g_horse_pile_rev == sent_rev[i]) continue;
        NetHorsePiles pkt;
        memset(&pkt, 0, sizeof(pkt));
        int n = horse_sync_fill_piles(&local_state, scene, pkt.e, NET_HORSE_PILE_MAX);
        pkt.hdr.type = PACKET_HORSE_PILES;
        pkt.hdr.client_id = 0;
        pkt.hdr.timestamp = get_server_time();
        pkt.hdr.scene_id = (unsigned char)(scene < 0 ? 0 : scene);
        pkt.hdr.entity_count = (unsigned char)n;
        pkt.count = (unsigned char)n;
        int len = (int)(sizeof(NetHeader) + 4 + (size_t)n * sizeof(NetHorsePileEntry));
        sendto(sock, (const char *)&pkt, len, 0, (struct sockaddr*)&slots[i].addr, sizeof(struct sockaddr_in));
        sent_rev[i] = g_horse_pile_rev;
        sent_scene[i] = scene;
    }
}

/* server_broadcast_horse_events -- the horse family of REFLUX actions, relayed (PACKET_HORSE_EVENTS).
 * This is the "via reflux" half of horse sync: the authoritative tick dispatched each event at the
 * moment it happened, this drains the family past a cursor and ships the rows, and each client
 * re-dispatches them into its OWN REFLUX log on arrival -- so a mod listening on a client sees a horse
 * mounted on the server without knowing the server exists. Only sent when there is something new, and
 * the first call establishes a baseline rather than replaying server history at a fresh peer. */
void server_broadcast_horse_events(void) {
    static int cursor = -1;  /* -1: baseline on the first call (see horse_sync_collect_events) */
    /* A cursor overtaken by REFLUX's ring is the one loss this relay can suffer, and it is now
       REPORTED rather than silently skipped: horse_sync_missed says how many rows between the cursor
       and the oldest retained one are genuinely gone. Loud on purpose -- if this ever prints, the
       relay's datagram budget is too small for the event rate and should be raised, and it should be
       visible in a live log rather than inferred later from a gap. */
    int lost = horse_sync_missed(cursor);
    if (lost > 0)
        printf("[HORSE] relay lost %d event(s) -- REFLUX's %d-entry ring wrapped before the drain\n",
               lost, REFLUX_LOG_CAPACITY);
    /* Up to four datagrams a snapshot (32 rows): a stampede of horse events is drained in order over
       consecutive snapshots rather than silently dropping the overflow past one packet. The cursor
       lives on the first undelivered row, so the only real bound left is REFLUX's own ring -- see
       horse_sync.h, and note it is absolute so wrapping no longer stops the relay. */
    for (int batch = 0; batch < 4; batch++) {
        NetHorseEvents pkt;
        memset(&pkt, 0, sizeof(pkt));
        int n = horse_sync_collect_events(pkt.e, NET_HORSE_EVENT_MAX, &cursor);
        if (n <= 0) break;
        pkt.hdr.type = PACKET_HORSE_EVENTS;
        pkt.hdr.client_id = 0;
        pkt.hdr.timestamp = get_server_time();
        pkt.hdr.entity_count = (unsigned char)n;
        pkt.count = (unsigned char)n;
        int len = (int)(sizeof(NetHeader) + 4 + (size_t)n * sizeof(NetHorseEvent));
        for (int i = 1; i < MAX_CLIENTS; i++) {
            if (!slots[i].active || !slots[i].welcomed) continue;
            sendto(sock, (const char *)&pkt, len, 0, (struct sockaddr*)&slots[i].addr, sizeof(struct sockaddr_in));
        }
        if (n < NET_HORSE_EVENT_MAX) break;   /* nothing left that fits */
    }
}

/* server_broadcast_citizen_events -- the citizen family of REFLUX actions, relayed
 * (PACKET_CITIZEN_EVENTS). Citizens are PlayerStates, so their BODIES already reach clients on
 * PACKET_SNAPSHOT; what never crossed the wire was the fact that one slid, slide-jumped, drew a
 * weapon or fired, which is what a client-side mod needs to react. Same shape as the horse relay:
 * an absolute cursor, drained four datagrams a snapshot, loss reported rather than silent. */
void server_broadcast_citizen_events(void) {
    static int cursor = -1;  /* -1: baseline on the first call (see citizen_sync.h) */
    int lost = citizen_sync_missed(cursor);
    if (lost > 0)
        printf("[CITIZEN] relay lost %d event(s) -- REFLUX's %d-entry ring wrapped before the drain\n",
               lost, REFLUX_LOG_CAPACITY);
    for (int batch = 0; batch < 4; batch++) {
        NetCitizenEvents pkt;
        memset(&pkt, 0, sizeof(pkt));
        int n = citizen_sync_collect_events(pkt.e, NET_CITIZEN_EVENT_MAX, &cursor);
        if (n <= 0) break;
        pkt.hdr.type = PACKET_CITIZEN_EVENTS;
        pkt.hdr.client_id = 0;
        pkt.hdr.timestamp = get_server_time();
        pkt.hdr.entity_count = (unsigned char)n;
        pkt.count = (unsigned char)n;
        int len = (int)(sizeof(NetHeader) + 4 + (size_t)n * sizeof(NetCitizenEvent));
        for (int i = 1; i < MAX_CLIENTS; i++) {
            if (!slots[i].active || !slots[i].welcomed) continue;
            sendto(sock, (const char *)&pkt, len, 0, (struct sockaddr*)&slots[i].addr, sizeof(struct sockaddr_in));
        }
        if (n < NET_CITIZEN_EVENT_MAX) break;   /* nothing left that fits */
    }
}

/* server_test_plant_citizen_pair -- SHANKPIT_TEST_ZOMBIE_PAIR, a test-only population for
 * `make test-citizen-wire`, in the same spirit as SHANKPIT_BAKED_LEVELS.
 *
 * Why it exists: a citizen's REFLUX actions can only be proved to reach a real socket by having a
 * citizen actually FIGHT, and the organic MODE_ZOMBIES population is placed tens of metres out from
 * a hero and wanders -- likely, but never certain, to meet a zombie inside a test's window. One
 * citizen and one hunting zombie six metres apart make the fight certain instead of likely.
 *
 * Why it is planted on the first welcomed client instead of at boot: the first version ran at
 * startup, and the client the net layer then seated took the very slot the citizen's body was in and
 * memset it away -- the server logged a pair that no longer existed by the time anybody could see
 * it. Planting after a seat exists also makes the body land in a slot the net layer has not handed
 * out, which is the ordering witness_ai's own spawners already rely on in a real match. */
static int g_test_pair_spawned = 0;
static void server_test_plant_citizen_pair(unsigned int now_ms) {
    if (g_test_pair_spawned) return;
    const char *want = getenv("SHANKPIT_TEST_ZOMBIE_PAIR");
    if (!want || want[0] != '1') { g_test_pair_spawned = 1; return; }
    int seated = 0;
    for (int i = 1; i < MAX_CLIENTS; i++)
        if (slots[i].active && slots[i].welcomed) { seated = 1; break; }
    if (!seated) return;
    g_test_pair_spawned = 1;
    /* TWO pairs, so one run proves both halves of what the wire is for: the trigger (a healthy
     * citizen shooting) and the fight plan (a wounded one giving ground). A single pair could only
     * ever show one of them -- a healthy citizen shoots and never covers, a wounded one covers and
     * never shoots -- which is exactly how the first version of this hook passed one check and
     * failed the other. */
    static const struct { float x; int health; float zombie_z; } pairs[2] = {
        { 4.0f, 35, 5.0f },    /* hurt + close: take cover (inside PARENA's 400 health / 6 m rungs) */
        { 14.0f, 100, 6.0f },  /* healthy, a gun's range away: draw and fire */
    };
    for (int k = 0; k < 2; k++) {
        int cid = witness_ai_spawn_citizen(&local_state, ZONE_PUBLIC, 40, 30, pairs[k].x, 8.0f, 0.0f, now_ms);
        if (cid > 0 && cid < MAX_CLIENTS) local_state.players[cid].health = pairs[k].health;
        int zid = witness_ai_spawn_zombie(&local_state, pairs[k].x, 8.0f, pairs[k].zombie_z, now_ms);
        if (zid > 0) witness_ai_force_zombie_mood(zid, ZOMBIE_MOOD_HUNTING);
        printf("[TEST] citizen pair %d: citizen=%d (%d health) zombie=%d\n", k, cid, pairs[k].health, zid);
    }
}

/* EMPIRE unit rules from PARENA -- included here because this file has no block of rules includes yet;
   the server spawns authored units, so it must read the same archetype/order table the lobby does. */
#include "../../../packages/simulation/empire_units_rules_host.h"

int main(int argc, char *argv[]) {
    buggy_rules_install(); /* PARENA buggy handling (#468) */
    horse_rules_install(); /* PARENA tölt mount handling (stamina, jump, carrot) */
    horse_goals_install(); /* PARENA horse goal decisions (graze/wander/idle/home/flee) */
    horse_poop_install();  /* PARENA digestive rules (when a grazing horse drops, how long it lasts) */
    citizen_actor_rules_install(); /* PARENA citizen bodies: jump, crouch, slide, slide-jump, weapons */
    horse_reflux_install(reflux_host_dispatch); /* the horse actor's events reach every mod, and the wire */
    vehicle_salvage_rules_install(); /* PARENA car components: the parts own the split, hull mirrors them */
    garage_rules_install(); /* PARENA garage: what this car's origin is worth, what a bay takes */
    eu_rules_install(); /* PARENA EMPIRE unit archetypes, the human baseline, order priorities */
    heli_rules_install(); /* PARENA helicopter flight model (#542) */
    rocket_car_rules_install(); /* PARENA rocket car (Bedrock Racers port) */
    /* Card #479: clients default to the baked-in levels; the shared game server keeps resolving the admin-
       flagged QUEUE default live (a NOCK flag change must still take effect at the next round), falling back
       to the baked copy when IDUNA is unreachable. SHANKPIT_BAKED_LEVELS=1 makes it use baked levels only. */
    { const char *bk = getenv("SHANKPIT_BAKED_LEVELS"); level_boxes_set_live_download(!(bk && bk[0] == '1')); }
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
        } else if (strcmp(argv[i], "--beacon-port") == 0 && i + 1 < argc) {
            /* 0 disables advertising entirely (a private world, or a test that must not be seen). */
            g_server_beacon_port = atoi(argv[i + 1]);
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
        } else if (strcmp(argv[i], "--drop-cast-acks") == 0 && i + 1 < argc) {
            /* TEST ONLY: swallow the next N CAST ACKs so the E2E can exercise the client's real retry
               path (loss -> recovery with N <= wow_retry_max, loss -> give-up above it). A normal
               server never passes this, and with it absent the ACK path below is unchanged. */
            g_drop_cast_acks = atoi(argv[i + 1]);
            i++;
        }
    }

    if (recorder.enabled && !recorder.file) {
        recorder_init_file("shankpit_recording.lispasm");
    }

    server_net_init(server_port);
    g_server_game_port = server_port;
    if (g_server_beacon_port > 0) {
        g_server_beacon_sock = zone_beacon_socket();
        if (g_server_beacon_sock < 0) NET_SERVER_LOG("BEACON_DISABLED socket failed -- this world will not be discoverable");
        else NET_SERVER_LOG("BEACON_ON port=%d game_port=%d", g_server_beacon_port, g_server_game_port);
    }
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
        }        } else if (mode == MODE_ZOMBIES) {
            /* The sandbox's own world: VOXWORLD, the scene the lobby's --zombies tile loads, so a
             * citizen on a dedicated server stands on the same ground a player does. */
            g_server_match_scene = SCENE_VOXWORLD;
            scene_load(g_server_match_scene);
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
        if (strcmp(argv[i], "--dungeon") == 0) {
            /* --dungeon [zone] -- host a shared procedural zone. It is generated once here and its
               (id, seed) is broadcast; clients rebuild the identical layout themselves. */
            const char *zone = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : "dungeon_1";
            server_zone_begin(zone);
        } else if (strcmp(argv[i], "--job") == 0 && i + 1 < argc) {
            /* --job NIN:75 -- apply that GFD job to every slot this host spawns, so an ally's health pool and
               cooldown are the server's numbers and not their own idea of them. */
            int job = -1, level = 1;
            if (stats_apply_parse(argv[++i], &job, &level)) {
                stats_apply_clear();
                stats_apply_install();
                stats_apply_set_all(job, level);
                printf("[STATS] --job %s level %d for every slot (health %d)\n",
                       stats_bridge_job_name(job), level, stats_apply_health_for(1));
            } else {
                printf("[STATS] --job '%s' is not a job (try NIN:75); ignoring\n", argv[i]);
            }
        } else if (strcmp(argv[i], "--level") == 0 && i + 1 < argc) {
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
        /* BIG_O engine merge phase 7d -- ticks the witness/zombie population
           witness_ai_seed_voxworld_encounter (local_init_match) spawns for MODE_STORY's VOXWORLD
           scene. Same "safe to call unconditionally" property story_ai_tick's own comment above
           already established -- a real no-op whenever nothing is spawned. */
        server_test_plant_citizen_pair(now);
        if (local_state.game_mode == MODE_ZOMBIES) witness_ai_zombies_tick(&local_state, now);
        witness_ai_tick(&local_state, now);
        /* VENDING: the cone is a health change, and health lives here (see server_vendor_tick). */
        server_vendor_tick(now);
        story_check_level_exits(now);
        /* S536, TYLER VALHANNA -- same "safe to call unconditionally" property story_ai_tick's
         * own comment above already established: tyler_coldopen_tick no-ops the instant
         * g_tyler_coldopen.active is 0 (every mode/level except MODE_TYLER's own
         * TYLER_VALHANNA_ICELAND_1986, right after it loads). g_story_next_level_id is already
         * real and current (captured unconditionally by server_apply_custom_level above) --
         * exactly CONSTRUCT's own id, since that level's own next_level_id was authored against
         * it (cmd/nock_gen_tyler_levels/main.go). */
        tyler_coldopen_tick(&g_tyler_coldopen, now, g_story_next_level_id, story_force_level_transition);
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
                h->input.strafe = occ->in_ability ? 1.0f : (occ->in_bike ? -1.0f : 0.0f);   /* E = right, Q = left */
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
        buggy_tick_all(get_server_time()); /* the car fire ladder needs a clock for its blast */
        /* ...and the horses. This call was MISSING, which made every horse on a dedicated server a
           statue: local_game.h's horse_tick_all only ever ran inside local_update, and only the
           lobby's local single-player path calls that. So the mounted ride, the jump, the loose
           horse's own afternoon (PARENA goals, MISHRI mood, fence steering) and now its piles ran
           on a local match and not in the town everybody actually plays in. The wire test found it
           the first time a pile was expected over a socket. */
        horse_tick_all(get_server_time());

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
        brick_world_discard_bodies();   // broken brick bodies are visual-only: the server skips them, clients spawn + step them (brick_bodies.h)
        // Story System Phase 1 (docs/STORY_SYSTEM_NORTHSTAR.md Part 2) -- real, once-per-tick
        // door script evaluation, right after the rest of the tick's own physics/state updates
        // and before the snapshot broadcast, so a door's new state is reflected in the very
        // snapshot this tick sends out.
        story_doors_tick(local_state.players, MAX_CLIENTS);

        /* EMILY/BACKLOG.md SECTION 536 follow-up ("server-authoritative day/night sync") -- the
         * real dedicated-server half: advances the SAME local_state.story_clock local_init_match
         * seeds and local_game.h's local_update advances for a local match, so a genuine networked
         * MODE_STORY/MODE_STORY_CAVE session (not yet live-deployed anywhere -- shankpit-
         * server.service runs --deathmatch only, see this repo's own CLAUDE.md) has a real,
         * server-authoritative clock ready the moment one is. Every other mode: zero cost, zero
         * behavior change -- this whole block is skipped outright. */
        if (local_state.game_mode == MODE_STORY || local_state.game_mode == MODE_STORY_CAVE || local_state.game_mode == MODE_ZOMBIES) {
            static unsigned int dnc_last_ms = 0;
            static float dnc_accum_minutes = 0.0f; /* real, found-live bug fix -- see
                local_game.h's own local_update doc comment for the full account (a bare
                (int)-cast truncated to 0 on every normal-cadence call). */
            float dnc_dt_sec = (dnc_last_ms == 0 || now < dnc_last_ms) ? 0.0f : (float)(now - dnc_last_ms) * 0.001f;
            if (dnc_dt_sec > 1.0f) dnc_dt_sec = 1.0f;
            dnc_last_ms = now;
            dnc_accum_minutes += dnc_dt_sec * DAY_NIGHT_MINUTES_PER_REAL_SEC;
            int dnc_minutes = (int)dnc_accum_minutes;
            if (dnc_minutes > 0) {
                day_night_clock_tick(&local_state.story_clock, dnc_minutes);
                dnc_accum_minutes -= (float)dnc_minutes;
            }
        }
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
        server_shield_packs_tick();
        recorder_write_frame(tick, now);
        if ((tick % SERVER_SNAPSHOT_INTERVAL_TICKS) == 0) {
            server_broadcast();
            server_broadcast_world_clock();
            server_broadcast_brick_state();
            server_broadcast_vehicle_state(); /* PACKET_VEHICLE_STATE: which part of each car is still there */
            server_broadcast_horse_state();  /* PACKET_HORSE_STATE: the horses in each client's scene */
            server_broadcast_horse_piles();  /* PACKET_HORSE_PILES: what they left on the ground */
            server_broadcast_horse_events(); /* PACKET_HORSE_EVENTS: the horse REFLUX family, relayed */
            server_broadcast_citizen_events(); /* PACKET_CITIZEN_EVENTS: the citizen REFLUX family, relayed */
            server_broadcast_queue_level();
            server_broadcast_zone_state();   /* PACKET_ZONE_STATE: the zone's id + seed, 1 Hz */
            server_broadcast_empire_state(); /* PACKET_EMPIRE_STATE: stockpiles, node amounts, ground, 1 Hz */
            server_broadcast_shield_packs();
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
