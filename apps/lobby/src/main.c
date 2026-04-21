#define SDL_MAIN_HANDLED
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <stdint.h>

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
    #include <netdb.h>
    #include <errno.h>
#endif

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <GL/glu.h>

#include "player_model.h"
#include "../../../packages/ui/turtle_text.h"
#define UI_BRIDGE_DECL static
#include "../../../packages/ui/ui_bridge.h"
#include "../../../packages/ui/ui_bridge.c"

#include "../../../packages/common/protocol.h"
#include "../../../packages/common/physics.h"
#include "../../../packages/common/shared_movement.h"
#include "../../../packages/common/net_sim.h"
#include "../../../packages/simulation/local_game.h"
#include "../../../packages/render/proc_tex.h"
#include "../../../packages/render/retro_sky.h"
#include "../../../packages/render/retro_lighting.h"

#ifndef NET_VERBOSE_LOG
#define NET_VERBOSE_LOG 1
#endif
#ifndef NET_LOG_HANDSHAKE
#define NET_LOG_HANDSHAKE 1
#endif
#ifndef NET_LOG_SNAPSHOT
#define NET_LOG_SNAPSHOT 1
#endif
#ifndef NET_LOG_USERCMD
#define NET_LOG_USERCMD 1
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

#define STATE_LOBBY 0
#define STATE_GAME_NET 1
#define STATE_GAME_LOCAL 2
#define STATE_LISTEN_SERVER 99

char SERVER_HOST[64] = "s.farthq.com";
int SERVER_PORT = 6969;

int app_state = STATE_LOBBY;
int wpn_req = 1; 
int my_client_id = -1;
int lobby_selection = 0;
int skin_menu_selection = 0;
int skin_menu_open = 0;
int skin_menu_scroll = 0;

enum { SKIN_MENU_BACK = -1 };

UiState ui_state;
int ui_use_server = 0;
unsigned int ui_last_poll = 0;
int ui_edit_index = -1;
int ui_edit_len = 0;
unsigned int ui_last_click_ms = 0;
int ui_last_click_index = -1;
char ui_edit_buffer[64];
unsigned int travel_overlay_until_ms = 0;

float cam_yaw = 0.0f;
float cam_pitch = 0.0f;
float current_fov = 75.0f;
static float death_cam_blend = 0.0f;

typedef struct VehicleStyle {
    float matte;
    float spec;
    float neon;
    float hazard;
    float glitch;
    float underglow_alpha;
    float neon_scroll;
    uint32_t seed;
} VehicleStyle;

static VehicleStyle g_vehicle_style = {0.85f, 0.15f, 0.25f, 0.45f, 0.2f, 0.18f, 0.01f, 0xC0FFEE11u};
static int vehicle_style_enabled = 1;
static int vehicle_underglow_enabled = 1;
static int vehicle_worklights_enabled = 1;
#define HELI_NET_DEBUG 0
static int vs0_art_direction_enabled = 1;
/* Flip this during tuning to quickly test authored world-light presets. */
static RetroLightingPreset g_world_lighting_preset = RETRO_LIGHTING_DAY_STATIC;
static int terrain_wireframe_debug = 0;
static int terrain_normals_debug = 0;
static int voxworld_points_debug = 0;
static unsigned int terrain_debug_last_log_ms = 0;
static ProcTexture g_vehicle_noise_tex = {0};
static ProcTexture g_vehicle_glitch_tex = {0};
static RetroSky g_retro_sky = {0};

typedef enum {
    SKIN_BAT = 0,
    SKIN_MAYRICE,
    SKIN_CYBORG,
    SKIN_PIRATE,
    SKIN_NINJA,
    SKIN_PIMP,
    SKIN_VIKING,
    SKIN_BILL,
    SKIN_GENIE,
    SKIN_WANDERER,
    SKIN_PINK,
    SKIN_GEISHA,
    SKIN_ALPINE,
    SKIN_COUNT
} PlayerSkin;

static int g_selected_skin = SKIN_BAT;
static const char *SKIN_LABELS[SKIN_COUNT] = {
    "BAT",
    "MAYRICE",
    "CYBORG",
    "PIRATE",
    "NINJA",
    "PIMP",
    "VIKING",
    "BILL",
    "GENIE",
    "WANDERER",
    "PINK",
    "GEISHA",
    "ALPINE"
};
static const char *SKIN_CONFIG_PATH = "shankpit_skin.cfg";
static void ensure_skin_selection_visible(void);

static float clamp01f(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

static float smoothstepf(float edge0, float edge1, float x) {
    float t = clamp01f((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

static float death_pose_progress(const PlayerState *p, unsigned int now_ms) {
    if (!p || p->state != STATE_DEAD || p->death_time_ms == 0) return 0.0f;
    const unsigned int hold_ms = 120;
    const float fall_ms = 300.0f;
    unsigned int elapsed = (now_ms > p->death_time_ms) ? (now_ms - p->death_time_ms) : 0;
    if (elapsed <= hold_ms) return 0.0f;
    unsigned int anim_elapsed = elapsed - hold_ms;
    float t = (float)anim_elapsed / fall_ms;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static float death_pose_twist_degrees(const PlayerState *p, unsigned int now_ms) {
    if (!p || p->state != STATE_DEAD || p->death_time_ms == 0) return 0.0f;
    const unsigned int hold_ms = 120;
    const float max_twist_deg = 6.5f;
    unsigned int elapsed = (now_ms > p->death_time_ms) ? (now_ms - p->death_time_ms) : 0;
    float hold_t = clamp01f((float)elapsed / (float)hold_ms);
    float eased = hold_t * hold_t * (3.0f - 2.0f * hold_t);
    return max_twist_deg * eased;
}

/*
 * Retro atmosphere seam:
 * Keep all cheap "fake baked" lighting/fog math centralized so we can
 * iterate style without changing world/gameplay data paths.
 */
static void retro_apply_fog_rgb(float *r, float *g, float *b, const RetroLightingState *lighting, float dist) {
    float fog_r = 0.18f, fog_g = 0.25f, fog_b = 0.36f;
    float fog_near = 420.0f, fog_far = 2800.0f;
    if (lighting) {
        fog_r = lighting->fog_r;
        fog_g = lighting->fog_g;
        fog_b = lighting->fog_b;
        fog_near = lighting->fog_near;
        fog_far = lighting->fog_far;
    }
    float haze = smoothstepf(fog_near, fog_far, dist);
    *r = lerpf(*r, fog_r, haze);
    *g = lerpf(*g, fog_g, haze);
    *b = lerpf(*b, fog_b, haze);
}

static void retro_eval_brush_lighting_rgb(const RetroLightingState *lighting, float nx, float ny, float nz,
                                          float ambient_floor, float *out_r, float *out_g, float *out_b) {
    retro_lighting_eval_surface_rgb(lighting, nx, ny, nz, ambient_floor, out_r, out_g, out_b);
}

static float voxworld_canyon_grass_mask(float world_x, float world_z) {
    float centerline = 92.0f * sinf(world_x * 0.0045f) + 58.0f * sinf((world_x + 220.0f) * 0.0108f);
    float center_band = 1.0f - smoothstepf(85.0f, 300.0f, fabsf(world_z - centerline));
    float edge_band = 1.0f - smoothstepf(420.0f, 980.0f, fabsf(world_z));
    float patch_noise = 0.5f + 0.5f * (
        0.54f * sinf(world_x * 0.0075f + world_z * 0.0041f) +
        0.31f * cosf(world_x * 0.0118f - world_z * 0.0063f) +
        0.15f * sinf(world_z * 0.0170f)
    );
    float patch_mask = smoothstepf(0.37f, 0.74f, patch_noise);
    return center_band * edge_band * patch_mask;
}

static void retro_eval_terrain_vertex_rgb(const RetroLightingState *lighting,
                                          int scene_id,
                                          float world_x, float world_z,
                                          float h, float min_h, float inv_h_range,
                                          float nx, float ny, float nz,
                                          float *out_r, float *out_g, float *out_b) {
    float h_norm = clamp01f((h - min_h) * inv_h_range);

    /* Keep terrain material identity from height/slope rules. */
    float grass_mix = smoothstepf(0.62f, 0.92f, h_norm) * (0.55f + 0.45f * ny);
    if (scene_id == SCENE_VOXWORLD) {
        /* Voxworld gets broad canyon-floor grass swaths with noisy breakup. */
        float patch_mask = voxworld_canyon_grass_mask(world_x, world_z);
        float flat_bias = smoothstepf(0.60f, 0.97f, ny);
        float valley_bias = smoothstepf(0.90f, 0.15f, h_norm);
        float canyon_grass = patch_mask * flat_bias * valley_bias;
        if (canyon_grass > grass_mix) grass_mix = canyon_grass;
    }
    float dark_mix = smoothstepf(0.0f, 0.35f, 1.0f - h_norm) * (0.55f + 0.45f * (1.0f - ny));
    float base_r = 0.58f, base_g = 0.49f, base_b = 0.37f;
    float material_r = lerpf(base_r, 0.65f, grass_mix);
    float material_g = lerpf(base_g, 0.62f, grass_mix);
    float material_b = lerpf(base_b, 0.36f, grass_mix);
    material_r = lerpf(material_r, 0.35f, dark_mix);
    material_g = lerpf(material_g, 0.33f, dark_mix);
    material_b = lerpf(material_b, 0.30f, dark_mix);

    /* Terrain lighting truth comes from shared world-light rig + sampled normal. */
    float light_r = 1.0f, light_g = 1.0f, light_b = 1.0f;
    retro_eval_brush_lighting_rgb(lighting, nx, ny, nz, 0.62f, &light_r, &light_g, &light_b);

    /* Cheap AO is secondary support only (non-directional, restrained range). */
    float slope_ao = smoothstepf(0.40f, 0.96f, 1.0f - ny) * 0.06f;
    float valley_ao = smoothstepf(0.20f, 0.0f, h_norm) * 0.05f;
    float ao = 1.0f - (slope_ao + valley_ao);
    if (ao < 0.88f) ao = 0.88f;

    if (out_r) *out_r = material_r * light_r * ao;
    if (out_g) *out_g = material_g * light_g * ao;
    if (out_b) *out_b = material_b * light_b * ao;
}

/*
 * Scene fog tuning seam:
 * Keep per-world readability decisions data-driven while preserving the
 * centralized RetroLightingState -> retro_apply_fog_rgb pipeline.
 */
static void retro_tune_world_fog(RetroLightingState *lighting, int scene_id) {
    if (!lighting) return;

    /* First pass global haze target. */
    lighting->fog_near = 220.0f;
    lighting->fog_far = 1600.0f;

    /*
     * Large outdoor worlds retain a bit more depth so portals and skyline
     * silhouettes remain readable at gameplay ranges.
     */
    if (scene_id == SCENE_VOXWORLD || scene_id == SCENE_STADIUM || scene_id == SCENE_POO_POO_ISLAND) {
        lighting->fog_near = 300.0f;
        lighting->fog_far = 1900.0f;
        lighting->fog_r *= 0.96f;
        lighting->fog_g *= 0.98f;
        lighting->fog_b = lighting->fog_b * 1.02f + 0.01f;
    }

    /* Tighter interior/garage volumes can carry denser haze. */
    if (scene_id == SCENE_GARAGE_OSAKA) {
        lighting->fog_near = 200.0f;
        lighting->fog_far = 1450.0f;
    }

    lighting->fog_r = clamp01f(lighting->fog_r);
    lighting->fog_g = clamp01f(lighting->fog_g);
    lighting->fog_b = clamp01f(lighting->fog_b);
}

#define Z_FAR 8000.0f

static void draw_box(float w, float h, float d);

int sock = -1;
struct sockaddr_in server_addr;
static int net_requested_mode = MODE_DEATHMATCH;

#define NET_CMD_HISTORY 3
#define CLIENT_USERCMD_HZ 60
#define CLIENT_USERCMD_INTERVAL_MS (1000 / CLIENT_USERCMD_HZ)
#define CLIENT_RECON_HISTORY 256
#define INTERP_DELAY_MS 220
#define RECONCILE_DECAY_LAMBDA 10.0f
#define RECONCILE_HARD_SNAP_DIST 2.0f
#define RECONCILE_HARD_SNAP_YAW 45.0f
UserCmd net_cmd_history[NET_CMD_HISTORY];
int net_cmd_history_count = 0;
int net_cmd_seq = 0;
unsigned int net_last_cmd_send_ms = 0;
UserCmd client_cmd_hist[CLIENT_RECON_HISTORY];
unsigned int net_latest_seq_sent = 0;

typedef struct {
    int has_a;
    int has_b;
    NetPlayer a;
    NetPlayer b;
    uint32_t a_time_ms;
    uint32_t b_time_ms;
} RemoteInterp;

static RemoteInterp rinterp[MAX_CLIENTS];
static float reconcile_corr_x = 0.0f;
static float reconcile_corr_y = 0.0f;
static float reconcile_corr_z = 0.0f;
static float reconcile_corr_yaw = 0.0f;
static float reconcile_corr_pitch = 0.0f;
static int net_server_time_sync = 0;
static int32_t net_server_time_offset_ms = 0;
static uint32_t net_last_snapshot_server_ts = 0;
static uint32_t net_last_reconciled_ack = 0;
static uint32_t net_last_corr_decay_ms = 0;

static int net_local_pid = -1;
static int net_spawn_protect_cmds = 0;
static int net_have_spawn_state = 0;
static int net_have_initial_local_snapshot_sync = 0;
static unsigned char net_last_life_state = STATE_DEAD;
static unsigned char net_last_scene_id = 255;
static unsigned char net_prev_is_shooting[MAX_CLIENTS];
static int last_applied_scene_id = -999;

typedef struct {
    unsigned int connect_start_ms;
    unsigned int welcome_ms;
    int connect_started;
    int got_welcome;
    int got_any_snapshot;
    int got_local_snapshot;
    int control_unlocked_logged;
    int first_cmd_sent_logged;
    unsigned int tx_cmds;
    unsigned int snapshots_rx;
    int latest_snapshot_len;
    int latest_entity_count;
    unsigned int last_usercmd_summary_ms;
    unsigned int last_snapshot_summary_ms;
    unsigned int last_welcome_timeout_log_ms;
    unsigned int last_snapshot_sync_timeout_log_ms;
    unsigned int last_blocked_input_log_ms;
    unsigned int last_dup_welcome_log_ms;
    unsigned int last_invalid_packet_log_ms;
} NetClientDiag;

static NetClientDiag net_diag;

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

static void net_emit_client_summaries(unsigned int now_ms) {
    if (net_should_log_every(&net_diag.last_usercmd_summary_ms, 1000, now_ms)) {
        NET_SUMMARY_LOG("USERCMD tx_cmds=%u last_seq=%u got_welcome=%d got_any_snapshot=%d got_local_snapshot=%d my_client_id=%d local_pid=%d",
                        net_diag.tx_cmds, net_latest_seq_sent, net_diag.got_welcome, net_diag.got_any_snapshot,
                        net_diag.got_local_snapshot, my_client_id, net_local_pid);
    }
    if (net_should_log_every(&net_diag.last_snapshot_summary_ms, 1000, now_ms)) {
        NET_SUMMARY_LOG("SNAPSHOT snapshots_rx=%u latest_len=%d latest_entity_count=%d local_seen=%d scene_id=%d last_reconciled_ack=%u",
                        net_diag.snapshots_rx, net_diag.latest_snapshot_len, net_diag.latest_entity_count,
                        net_diag.got_local_snapshot, local_state.scene_id, net_last_reconciled_ack);
    }
}

static void net_check_diag_timeouts(unsigned int now_ms) {
    if (!net_diag.connect_started) return;
#if NET_LOG_TIMEOUT
    if (!net_diag.got_welcome && now_ms - net_diag.connect_start_ms >= 1500 &&
        net_should_log_every(&net_diag.last_welcome_timeout_log_ms, 1000, now_ms)) {
        NET_WARN_LOG("WELCOME_TIMEOUT host=%s port=%d dt_ms=%u", SERVER_HOST, SERVER_PORT, now_ms - net_diag.connect_start_ms);
    }
    if (net_diag.got_welcome && !net_diag.got_local_snapshot && now_ms - net_diag.welcome_ms >= 1500 &&
        net_should_log_every(&net_diag.last_snapshot_sync_timeout_log_ms, 1000, now_ms)) {
        NET_WARN_LOG("SNAPSHOT_SYNC_TIMEOUT client_id=%d dt_ms=%u", my_client_id, now_ms - net_diag.welcome_ms);
    }
#else
    (void)now_ms;
#endif
}

#ifndef NET_PARITY_DEBUG
#define NET_PARITY_DEBUG 0
#endif

#ifndef NET_JITTER_DIAG
#define NET_JITTER_DIAG 0
#endif

#if NET_JITTER_DIAG
typedef struct {
    unsigned int window_start_ms;
    unsigned int interp_t_clamp_zero;
    unsigned int interp_t_clamp_one;
    unsigned int interp_missing_pair;
    unsigned int interp_bad_timestamp;
    int32_t offset_last_ms;
    int32_t offset_delta_abs_max_ms;
    int32_t render_to_latest_snapshot_delta_ms;
} NetInterpDiagStats;

typedef struct {
    unsigned int window_start_ms;
    unsigned int sample_count;
    float corr_sum;
    float corr_max;
    float yaw_corr_sum;
    float yaw_corr_max;
    unsigned int replay_sum;
    unsigned int replay_max;
    unsigned int last_ack;
    unsigned int last_latest;
} NetReconcileDiagStats;

static NetInterpDiagStats net_interp_diag;
static NetReconcileDiagStats net_reconcile_diag;

static void net_interp_diag_emit(unsigned int now_ms) {
    if (net_interp_diag.window_start_ms == 0) {
        net_interp_diag.window_start_ms = now_ms;
        net_interp_diag.offset_last_ms = net_server_time_offset_ms;
        return;
    }
    if (now_ms - net_interp_diag.window_start_ms < 1000) return;
    printf("[NET_DIAG_INTERP] t0=%u t1=%u missing_ab=%u bad_ts=%u offset_ms=%d offset_delta_max=%d render_vs_latest_snapshot_ms=%d\n",
           net_interp_diag.interp_t_clamp_zero,
           net_interp_diag.interp_t_clamp_one,
           net_interp_diag.interp_missing_pair,
           net_interp_diag.interp_bad_timestamp,
           net_server_time_offset_ms,
           net_interp_diag.offset_delta_abs_max_ms,
           net_interp_diag.render_to_latest_snapshot_delta_ms);
    memset(&net_interp_diag, 0, sizeof(net_interp_diag));
    net_interp_diag.window_start_ms = now_ms;
    net_interp_diag.offset_last_ms = net_server_time_offset_ms;
}

static void net_reconcile_diag_emit(unsigned int now_ms) {
    if (net_reconcile_diag.window_start_ms == 0) {
        net_reconcile_diag.window_start_ms = now_ms;
        return;
    }
    if (now_ms - net_reconcile_diag.window_start_ms < 1000) return;

    float corr_avg = net_reconcile_diag.sample_count > 0
        ? (net_reconcile_diag.corr_sum / (float)net_reconcile_diag.sample_count)
        : 0.0f;
    float yaw_avg = net_reconcile_diag.sample_count > 0
        ? (net_reconcile_diag.yaw_corr_sum / (float)net_reconcile_diag.sample_count)
        : 0.0f;
    float replay_avg = net_reconcile_diag.sample_count > 0
        ? ((float)net_reconcile_diag.replay_sum / (float)net_reconcile_diag.sample_count)
        : 0.0f;

    printf("[NET_DIAG_RECON] ack=%u latest=%u samples=%u corr_avg=%.4f corr_max=%.4f yaw_avg=%.3f yaw_max=%.3f replay_avg=%.2f replay_max=%u\n",
           net_reconcile_diag.last_ack,
           net_reconcile_diag.last_latest,
           net_reconcile_diag.sample_count,
           corr_avg,
           net_reconcile_diag.corr_max,
           yaw_avg,
           net_reconcile_diag.yaw_corr_max,
           replay_avg,
           net_reconcile_diag.replay_max);

    memset(&net_reconcile_diag, 0, sizeof(net_reconcile_diag));
    net_reconcile_diag.window_start_ms = now_ms;
}
#endif

#if NET_PARITY_DEBUG
typedef struct {
    unsigned int last_ack_seq;
    unsigned int last_local_seq;
    int last_replay_count;
    float last_pos_corr;
    float last_pos_corr_2d;
    float last_yaw_corr;
    float last_vel_delta;
    int last_ground_mismatch;
    int last_jump_mismatch;
    unsigned int window_start_ms;
    unsigned int correction_count;
    float correction_sum;
    float correction_max;
    unsigned int grounded_mismatch_count;
    unsigned int jump_mismatch_count;
    unsigned int replay_sum;
} NetParityDebugStats;

static NetParityDebugStats net_parity_stats;

static void net_parity_debug_sample(unsigned int now_ms) {
    if (net_parity_stats.window_start_ms == 0) {
        net_parity_stats.window_start_ms = now_ms;
        return;
    }
    if (now_ms - net_parity_stats.window_start_ms < 1000) return;
    float corr_avg = (net_parity_stats.correction_count > 0)
        ? (net_parity_stats.correction_sum / (float)net_parity_stats.correction_count)
        : 0.0f;
    float replay_avg = (net_parity_stats.correction_count > 0)
        ? ((float)net_parity_stats.replay_sum / (float)net_parity_stats.correction_count)
        : 0.0f;
    printf("[NET_PARITY] ack=%u latest=%u replay=%d corr3d=%.4f corr2d=%.4f yaw=%.3f vel_delta=%.4f g_mis=%d j_mis=%d cps=%u corr_avg=%.4f corr_max=%.4f replay_avg=%.2f g_mis_cnt=%u j_mis_cnt=%u\n",
           net_parity_stats.last_ack_seq,
           net_parity_stats.last_local_seq,
           net_parity_stats.last_replay_count,
           net_parity_stats.last_pos_corr,
           net_parity_stats.last_pos_corr_2d,
           net_parity_stats.last_yaw_corr,
           net_parity_stats.last_vel_delta,
           net_parity_stats.last_ground_mismatch,
           net_parity_stats.last_jump_mismatch,
           net_parity_stats.correction_count,
           corr_avg,
           net_parity_stats.correction_max,
           replay_avg,
           net_parity_stats.grounded_mismatch_count,
           net_parity_stats.jump_mismatch_count);
    memset(&net_parity_stats, 0, sizeof(net_parity_stats));
    net_parity_stats.window_start_ms = now_ms;
}
#endif

void net_connect();
void net_shutdown();

static void reset_client_render_state_for_net() {
    my_client_id = -1;
    memset(&local_state, 0, sizeof(local_state));
    memset(net_cmd_history, 0, sizeof(net_cmd_history));
    net_cmd_history_count = 0;
    net_cmd_seq = 0;
    memset(client_cmd_hist, 0, sizeof(client_cmd_hist));
    net_latest_seq_sent = 0;
    memset(rinterp, 0, sizeof(rinterp));
    reconcile_corr_x = 0.0f;
    reconcile_corr_y = 0.0f;
    reconcile_corr_z = 0.0f;
    reconcile_corr_yaw = 0.0f;
    reconcile_corr_pitch = 0.0f;
    net_server_time_sync = 0;
    net_server_time_offset_ms = 0;
    net_last_snapshot_server_ts = 0;
    net_last_reconciled_ack = 0;
    net_last_corr_decay_ms = 0;
    net_last_cmd_send_ms = 0;
    net_spawn_protect_cmds = 0;
    net_have_spawn_state = 0;
    net_have_initial_local_snapshot_sync = 0;
    net_last_life_state = STATE_DEAD;
    net_last_scene_id = 255;
    memset(net_prev_is_shooting, 0, sizeof(net_prev_is_shooting));
    memset(&net_diag, 0, sizeof(net_diag));
    travel_overlay_until_ms = 0;
    local_state.pending_scene = -1;
    local_state.scene_id = SCENE_GARAGE_OSAKA;
    phys_set_scene(local_state.scene_id);
    local_state.players[0].scene_id = local_state.scene_id;
}

static void client_apply_spawn_transition_sync(PlayerState *p, const NetPlayer *np, const char *reason_tag) {
    if (!p || !np) return;

    cam_yaw = norm_yaw_deg(np->yaw);
    cam_pitch = clamp_pitch_deg(np->pitch);

    p->x = np->x;
    p->y = np->y;
    p->z = np->z;
    p->yaw = cam_yaw;
    p->pitch = cam_pitch;

    reconcile_corr_x = 0.0f;
    reconcile_corr_y = 0.0f;
    reconcile_corr_z = 0.0f;
    reconcile_corr_yaw = 0.0f;
    reconcile_corr_pitch = 0.0f;
    memset(client_cmd_hist, 0, sizeof(client_cmd_hist));
    memset(net_cmd_history, 0, sizeof(net_cmd_history));
    net_cmd_history_count = 0;
    net_latest_seq_sent = np->last_seq;
    net_last_reconciled_ack = np->last_seq;
    net_cmd_seq = (int)np->last_seq;
    net_spawn_protect_cmds = 3;

    if (!net_have_initial_local_snapshot_sync) {
        net_have_initial_local_snapshot_sync = 1;
    }

#if NET_JITTER_DIAG
    printf("[NET_DIAG_SPAWN_SYNC] reason=%s scene=%u ack=%u yaw=%.2f pitch=%.2f\n",
           reason_tag ? reason_tag : "unknown",
           (unsigned int)np->scene_id,
           np->last_seq,
           cam_yaw,
           cam_pitch);
#else
    (void)reason_tag;
#endif
}

void draw_string(const char* str, float x, float y, float size) {
    TurtlePen pen = turtle_pen_create(x, y, size);
    turtle_draw_text(&pen, str);
}

typedef enum {
    LOBBY_JOIN = 0,
    LOBBY_TDMO,
    LOBBY_STORY,
    LOBBY_SOLO,
    LOBBY_BATTLE,
    LOBBY_TDMB,
    LOBBY_CTFB,
    LOBBY_COUNT
} LobbyAction;

char lobby_labels_mutable[LOBBY_COUNT][64];

static const char *LOBBY_LABELS[LOBBY_COUNT] = {
    "JOIN",
    "TDMO",
    "STORY",
    "SOLO",
    "TRAIN",
    "TDMB",
    "CTFB"
};

static void lobby_init_labels() {
    for (int i = 0; i < LOBBY_COUNT; i++) {
        snprintf(lobby_labels_mutable[i], sizeof(lobby_labels_mutable[i]), "%s", LOBBY_LABELS[i]);
    }
}

static int clamp_skin_id(int skin_id) {
    if (skin_id < SKIN_BAT || skin_id >= SKIN_COUNT) return SKIN_BAT;
    return skin_id;
}

static void save_skin_selection() {
    FILE *f = fopen(SKIN_CONFIG_PATH, "w");
    if (!f) return;
    fprintf(f, "%d\n", clamp_skin_id(g_selected_skin));
    fclose(f);
}

static void load_skin_selection() {
    FILE *f = fopen(SKIN_CONFIG_PATH, "r");
    if (!f) {
        g_selected_skin = SKIN_BAT;
        return;
    }
    int parsed = SKIN_BAT;
    if (fscanf(f, "%d", &parsed) == 1) {
        g_selected_skin = clamp_skin_id(parsed);
    } else {
        g_selected_skin = SKIN_BAT;
    }
    fclose(f);
}

static int lobby_menu_count() {
    if (ui_use_server && ui_state.entry_count > 0) {
        return ui_state.entry_count + 1;
    }
    return LOBBY_COUNT + 1;
}

static const char *lobby_menu_label(int idx) {
    int skins_idx = lobby_menu_count() - 1;
    if (idx == skins_idx) {
        return "SKINS";
    }
    if (ui_use_server && idx >= 0 && idx < ui_state.entry_count) {
        return ui_state.entries[idx].label;
    }
    return lobby_labels_mutable[idx];
}

static const char *lobby_menu_entry_id(int idx) {
    int skins_idx = lobby_menu_count() - 1;
    if (idx == skins_idx) {
        return "menu.skins";
    }
    if (ui_use_server && idx >= 0 && idx < ui_state.entry_count) {
        return ui_state.entries[idx].id;
    }
    return NULL;
}

static void lobby_commit_edit(int index) {
    if (index < 0) return;
    if (index == lobby_menu_count() - 1) return;
    if (ui_edit_len <= 0) return;
    ui_edit_buffer[ui_edit_len] = '\0';
    if (ui_use_server && index < ui_state.entry_count) {
        snprintf(ui_state.entries[index].label, UI_BRIDGE_LABEL_LEN, "%s", ui_edit_buffer);
    } else if (!ui_use_server && index < LOBBY_COUNT) {
        snprintf(lobby_labels_mutable[index], sizeof(lobby_labels_mutable[index]), "%s", ui_edit_buffer);
    }
}

static void lobby_start_edit(int index) {
    if (index < 0) return;
    ui_edit_index = index;
    ui_edit_len = 0;
    ui_edit_buffer[0] = '\0';
    SDL_StartTextInput();
}

static void lobby_end_edit(int commit) {
    if (commit) {
        lobby_commit_edit(ui_edit_index);
    }
    ui_edit_index = -1;
    ui_edit_len = 0;
    ui_edit_buffer[0] = '\0';
    SDL_StopTextInput();
}

static void lobby_apply_scene_id(const char *scene_id) {
    if (!scene_id || !scene_id[0]) return;
    if (strcmp(scene_id, "GARAGE_OSAKA") == 0) {
        scene_load(SCENE_GARAGE_OSAKA);
    } else if (strcmp(scene_id, "STADIUM") == 0) {
        scene_load(SCENE_STADIUM);
    } else if (strcmp(scene_id, "VOXWORLD") == 0) {
        scene_load(SCENE_VOXWORLD);
    } else if (strcmp(scene_id, "DUST_COMPOUND") == 0) {
        scene_load(SCENE_DUST_COMPOUND);
    } else if (strcmp(scene_id, "OIL_TANKER") == 0) {
        scene_load(SCENE_OIL_TANKER);
    } else if (strcmp(scene_id, "POO_POO_ISLAND") == 0) {
        scene_load(SCENE_POO_POO_ISLAND);
    } else if (strcmp(scene_id, "STORY_CAVE") == 0) {
        scene_load(SCENE_STORY_CAVE);
    }
}

static void lobby_apply_ui_state() {
    if (!ui_use_server) return;
    if (strcmp(ui_state.active_mode_id, "mode.join") == 0) {
        app_state = STATE_GAME_NET;
        reset_client_render_state_for_net();
        net_requested_mode = MODE_DEATHMATCH;
        net_connect();
        return;
    }
    if (strcmp(ui_state.active_mode_id, "mode.tdmo") == 0) {
        app_state = STATE_GAME_NET;
        reset_client_render_state_for_net();
        net_requested_mode = MODE_TDMO;
        net_connect();
        return;
    }

    if (strcmp(ui_state.active_mode_id, "mode.demo") == 0) {
        app_state = STATE_GAME_LOCAL;
        local_init_match(1, MODE_DEATHMATCH);
    } else if (strcmp(ui_state.active_mode_id, "mode.battle") == 0) {
        app_state = STATE_GAME_LOCAL;
        local_init_match(12, MODE_DEATHMATCH);
    } else if (strcmp(ui_state.active_mode_id, "mode.story") == 0) {
        app_state = STATE_GAME_LOCAL;
        local_init_match(1, MODE_STORY);
    } else if (strcmp(ui_state.active_mode_id, "mode.tdmb") == 0) {
        app_state = STATE_GAME_LOCAL;
        local_init_match(12, MODE_TDMB);
    } else if (strcmp(ui_state.active_mode_id, "mode.tdm") == 0) {
        app_state = STATE_GAME_LOCAL;
        local_init_match(12, MODE_TDM);
    } else if (strcmp(ui_state.active_mode_id, "mode.ctfb") == 0 || strcmp(ui_state.active_mode_id, "mode.ctf") == 0) {
        app_state = STATE_GAME_LOCAL;
        local_init_match(12, MODE_CTFB);
    } else if (strcmp(ui_state.active_mode_id, "mode.training") == 0) {
        app_state = STATE_GAME_LOCAL;
        local_init_match(1, MODE_DEATHMATCH);
    } else if (strcmp(ui_state.active_mode_id, "mode.recorder") == 0) {
        app_state = STATE_GAME_LOCAL;
        local_init_match(1, MODE_DEATHMATCH);
    } else if (strcmp(ui_state.active_mode_id, "mode.garage") == 0) {
        app_state = STATE_GAME_LOCAL;
        local_init_match(1, MODE_DEATHMATCH);
    } else {
        return;
    }

    if (local_state.game_mode != MODE_TDMB && local_state.game_mode != MODE_CTFB) lobby_apply_scene_id(ui_state.active_scene_id);
}

static void setup_lobby_2d() {
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, 1280, 0, 720);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void lobby_start_action(int action) {
    if (action == lobby_menu_count() - 1) {
        skin_menu_open = 1;
        skin_menu_selection = clamp_skin_id(g_selected_skin);
        skin_menu_scroll = 0;
        ensure_skin_selection_visible();
        return;
    }
    if (ui_use_server) {
        const char *entry_id = lobby_menu_entry_id(action);
        if (entry_id) {
            if (ui_bridge_send_intent_activate(entry_id, &ui_state)) {
                lobby_apply_ui_state();
                return;
            }
        }
    }
    if (action == LOBBY_JOIN) {
        app_state = STATE_GAME_NET;
        reset_client_render_state_for_net();
        net_requested_mode = MODE_DEATHMATCH;
        net_connect();
    } else if (action == LOBBY_TDMO) {
        app_state = STATE_GAME_NET;
        reset_client_render_state_for_net();
        net_requested_mode = MODE_TDMO;
        net_connect();
    } else {
        app_state = STATE_GAME_LOCAL;
        switch (action) {
            case LOBBY_STORY:
                local_init_match(1, MODE_STORY);
                break;
            case LOBBY_SOLO:
                local_init_match(1, MODE_DEATHMATCH);
                break;
            case LOBBY_BATTLE:
                local_init_match(12, MODE_DEATHMATCH);
                break;
            case LOBBY_TDMB:
                local_init_match(12, MODE_TDMB);
                break;
            case LOBBY_CTFB:
                local_init_match(12, MODE_CTFB);
                break;
            default:
                break;
        }
    }

    if (app_state != STATE_LOBBY) {
        SDL_SetRelativeMouseMode(SDL_TRUE);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        gluPerspective(75.0, 1280.0/720.0, 0.1, Z_FAR);
        glMatrixMode(GL_MODELVIEW);
        glEnable(GL_DEPTH_TEST);
    }
}

#define MAX_TRAILS 4096 
#define GRID_SIZE 50.0f
typedef struct { int cx, cz; float life; } Trail;
Trail trails[MAX_TRAILS];
int trail_head = 0;

void add_trail(int x, int z) {
    int prev = (trail_head - 1 + MAX_TRAILS) % MAX_TRAILS;
    if (trails[prev].cx == x && trails[prev].cz == z && trails[prev].life > 0.9f) return;
    trails[trail_head].cx = x; trails[trail_head].cz = z;
    trails[trail_head].life = 1.0f;
    trail_head = (trail_head + 1) % MAX_TRAILS;
}

void update_and_draw_trails() {
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for(int i=0; i<MAX_CLIENTS; i++) {
        PlayerState *p = &local_state.players[i];
        if (p->active && p->on_ground && p->scene_id == local_state.scene_id) {
            int gx = (int)floorf(p->x / GRID_SIZE) * (int)GRID_SIZE + (int)(GRID_SIZE/2);
            int gz = (int)floorf(p->z / GRID_SIZE) * (int)GRID_SIZE + (int)(GRID_SIZE/2);
            add_trail(gx, gz);
        }
    }
    glLineWidth(2.0f);
    for(int i=0; i<MAX_TRAILS; i++) {
        if (trails[i].life > 0) {
            float s = (GRID_SIZE / 2.0f) - 4.0f;
            // Trails are now HOT PINK
            glColor4f(1.0f, 0.0f, 0.8f, trails[i].life); 
            glBegin(GL_LINE_LOOP);
            glVertex3f(trails[i].cx - s, 0.2f, trails[i].cz - s);
            glVertex3f(trails[i].cx + s, 0.2f, trails[i].cz - s);
            glVertex3f(trails[i].cx + s, 0.2f, trails[i].cz + s);
            glVertex3f(trails[i].cx - s, 0.2f, trails[i].cz + s);
            glEnd();
            trails[i].life -= 0.02f;
        }
    }
    glDisable(GL_BLEND);
}

void draw_grid() {
    glLineWidth(1.0f); glBegin(GL_LINES); 
    // THE MATRIX FLOOR (Cyan)
    glColor3f(0.0f, 1.0f, 1.0f); 
    for(int i=-4000; i<=4000; i+=50) { 
        glVertex3f(i, 0.1f, -4000); glVertex3f(i, 0.1f, 4000);
        glVertex3f(-4000, 0.1f, i); glVertex3f(4000, 0.1f, i); 
    }
    glEnd();
}

// --- NEON BRUTALIST BLOCK RENDERER ---
void draw_map(const RetroLightingState *lighting) {
    if (!vs0_art_direction_enabled) {
        for(int i=1; i<map_count; i++) {
            Box b = map_geo[i];
            float nr = 0.5f + 0.5f * sinf(b.x * 0.005f + b.y * 0.01f);
            float ng = 0.5f + 0.5f * sinf(b.z * 0.005f + 2.0f);
            float nb = 0.5f + 0.5f * sinf(b.x * 0.005f + 4.0f);
            if(nr > 0.8f) nr = 1.0f;
            if(ng > 0.8f) ng = 1.0f;
            if(nb > 0.8f) nb = 1.0f;

            glPushMatrix();
            glTranslatef(b.x, b.y, b.z);
            glScalef(b.w, b.h, b.d);

            glBegin(GL_QUADS);
            glColor3f(0.02f, 0.02f, 0.02f);
            glVertex3f(-0.5,0.5,0.5); glVertex3f(0.5,0.5,0.5); glVertex3f(0.5,0.5,-0.5); glVertex3f(-0.5,0.5,-0.5);
            glVertex3f(-0.5,-0.5,0.5); glVertex3f(0.5,-0.5,0.5); glVertex3f(0.5,-0.5,-0.5); glVertex3f(-0.5,-0.5,-0.5);
            glVertex3f(-0.5,-0.5,0.5); glVertex3f(0.5,-0.5,0.5); glVertex3f(0.5,0.5,0.5); glVertex3f(-0.5,0.5,0.5);
            glVertex3f(-0.5,-0.5,-0.5); glVertex3f(0.5,-0.5,-0.5); glVertex3f(0.5,0.5,-0.5); glVertex3f(-0.5,0.5,-0.5);
            glVertex3f(-0.5,-0.5,-0.5); glVertex3f(-0.5,-0.5,0.5); glVertex3f(-0.5,0.5,0.5); glVertex3f(-0.5,0.5,-0.5);
            glVertex3f(0.5,-0.5,0.5); glVertex3f(0.5,-0.5,-0.5); glVertex3f(0.5,0.5,-0.5); glVertex3f(0.5,0.5,0.5);
            glEnd();

            glLineWidth(2.0f);
            glColor3f(nr, ng, nb);
            glBegin(GL_LINE_LOOP);
            glVertex3f(-0.5, 0.5, 0.5); glVertex3f(0.5, 0.5, 0.5); glVertex3f(0.5, 0.5, -0.5); glVertex3f(-0.5, 0.5, -0.5);
            glEnd();
            glBegin(GL_LINE_LOOP);
            glVertex3f(-0.5, -0.5, 0.5); glVertex3f(0.5, -0.5, 0.5); glVertex3f(0.5, -0.5, -0.5); glVertex3f(-0.5, -0.5, -0.5);
            glEnd();
            glBegin(GL_LINES);
            glVertex3f(-0.5, -0.5, 0.5); glVertex3f(-0.5, 0.5, 0.5);
            glVertex3f(0.5, -0.5, 0.5); glVertex3f(0.5, 0.5, 0.5);
            glVertex3f(0.5, -0.5, -0.5); glVertex3f(0.5, 0.5, -0.5);
            glVertex3f(-0.5, -0.5, -0.5); glVertex3f(-0.5, 0.5, -0.5);
            glEnd();
            glPopMatrix();
        }
        return;
    }

    int ref_id = (my_client_id >= 0 && my_client_id < MAX_CLIENTS) ? my_client_id : 0;
    const PlayerState *rp = &local_state.players[ref_id];

    for(int i=1; i<map_count; i++) {
        Box b = map_geo[i];
        float style = 0.5f + 0.5f * sinf((b.x + b.z) * 0.003f);
        float base_r = lerpf(0.28f, 0.36f, style);
        float base_g = lerpf(0.33f, 0.40f, style);
        float base_b = lerpf(0.40f, 0.49f, style);
        float top_r = base_r * 1.12f, top_g = base_g * 1.12f, top_b = base_b * 1.12f;
        float side_r = base_r * 0.82f, side_g = base_g * 0.84f, side_b = base_b * 0.90f;
        float back_r = base_r * 0.73f, back_g = base_g * 0.77f, back_b = base_b * 0.84f;

        float ground_ao = smoothstepf(12.0f, -14.0f, b.y - b.h * 0.5f);
        float top_lit_r = 1.0f, top_lit_g = 1.0f, top_lit_b = 1.0f;
        float bot_lit_r = 1.0f, bot_lit_g = 1.0f, bot_lit_b = 1.0f;
        float front_lit_r = 1.0f, front_lit_g = 1.0f, front_lit_b = 1.0f;
        float back_lit_r = 1.0f, back_lit_g = 1.0f, back_lit_b = 1.0f;
        float left_lit_r = 1.0f, left_lit_g = 1.0f, left_lit_b = 1.0f;
        float right_lit_r = 1.0f, right_lit_g = 1.0f, right_lit_b = 1.0f;
        retro_eval_brush_lighting_rgb(lighting, 0.0f, 1.0f, 0.0f, 0.72f, &top_lit_r, &top_lit_g, &top_lit_b);
        retro_eval_brush_lighting_rgb(lighting, 0.0f, -1.0f, 0.0f, 0.58f, &bot_lit_r, &bot_lit_g, &bot_lit_b);
        retro_eval_brush_lighting_rgb(lighting, 0.0f, 0.0f, 1.0f, 0.64f, &front_lit_r, &front_lit_g, &front_lit_b);
        retro_eval_brush_lighting_rgb(lighting, 0.0f, 0.0f, -1.0f, 0.62f, &back_lit_r, &back_lit_g, &back_lit_b);
        retro_eval_brush_lighting_rgb(lighting, -1.0f, 0.0f, 0.0f, 0.60f, &left_lit_r, &left_lit_g, &left_lit_b);
        retro_eval_brush_lighting_rgb(lighting, 1.0f, 0.0f, 0.0f, 0.60f, &right_lit_r, &right_lit_g, &right_lit_b);
        bot_lit_r *= 0.86f - ground_ao * 0.18f; bot_lit_g *= 0.86f - ground_ao * 0.18f; bot_lit_b *= 0.86f - ground_ao * 0.18f;
        front_lit_r *= 0.98f - ground_ao * 0.08f; front_lit_g *= 0.98f - ground_ao * 0.08f; front_lit_b *= 0.98f - ground_ao * 0.08f;
        back_lit_r *= 0.96f - ground_ao * 0.10f; back_lit_g *= 0.96f - ground_ao * 0.10f; back_lit_b *= 0.96f - ground_ao * 0.10f;
        left_lit_r *= 0.97f - ground_ao * 0.10f; left_lit_g *= 0.97f - ground_ao * 0.10f; left_lit_b *= 0.97f - ground_ao * 0.10f;
        right_lit_r *= 0.99f - ground_ao * 0.08f; right_lit_g *= 0.99f - ground_ao * 0.08f; right_lit_b *= 0.99f - ground_ao * 0.08f;

        float dx = b.x - rp->x;
        float dz = b.z - rp->z;
        float dist = sqrtf(dx * dx + dz * dz);

        top_r *= top_lit_r; top_g *= top_lit_g; top_b *= top_lit_b;
        float bot_r = back_r * 0.85f * bot_lit_r, bot_g = back_g * 0.85f * bot_lit_g, bot_b = back_b * 0.85f * bot_lit_b;
        float front_r = base_r * front_lit_r, front_g = base_g * front_lit_g, front_b = base_b * front_lit_b;
        float rear_r = back_r * back_lit_r, rear_g = back_g * back_lit_g, rear_b = back_b * back_lit_b;
        float left_r = side_r * left_lit_r, left_g = side_g * left_lit_g, left_b = side_b * left_lit_b;
        float right_r = side_r * 0.95f * right_lit_r, right_g = side_g * 0.95f * right_lit_g, right_b = side_b * 0.95f * right_lit_b;
        retro_apply_fog_rgb(&top_r, &top_g, &top_b, lighting, dist);
        retro_apply_fog_rgb(&bot_r, &bot_g, &bot_b, lighting, dist);
        retro_apply_fog_rgb(&front_r, &front_g, &front_b, lighting, dist);
        retro_apply_fog_rgb(&rear_r, &rear_g, &rear_b, lighting, dist);
        retro_apply_fog_rgb(&left_r, &left_g, &left_b, lighting, dist);
        retro_apply_fog_rgb(&right_r, &right_g, &right_b, lighting, dist);

        glPushMatrix(); 
        glTranslatef(b.x, b.y, b.z); 
        glScalef(b.w, b.h, b.d);

        glBegin(GL_QUADS);
        glColor3f(top_r, top_g, top_b);
        glVertex3f(-0.5,0.5,0.5); glVertex3f(0.5,0.5,0.5); glVertex3f(0.5,0.5,-0.5); glVertex3f(-0.5,0.5,-0.5);
        glColor3f(bot_r, bot_g, bot_b);
        glVertex3f(-0.5,-0.5,0.5); glVertex3f(0.5,-0.5,0.5); glVertex3f(0.5,-0.5,-0.5); glVertex3f(-0.5,-0.5,-0.5);
        glColor3f(front_r, front_g, front_b);
        glVertex3f(-0.5,-0.5,0.5); glVertex3f(0.5,-0.5,0.5); glVertex3f(0.5,0.5,0.5); glVertex3f(-0.5,0.5,0.5);
        glColor3f(rear_r, rear_g, rear_b);
        glVertex3f(-0.5,-0.5,-0.5); glVertex3f(0.5,-0.5,-0.5); glVertex3f(0.5,0.5,-0.5); glVertex3f(-0.5,0.5,-0.5);
        glColor3f(left_r, left_g, left_b);
        glVertex3f(-0.5,-0.5,-0.5); glVertex3f(-0.5,-0.5,0.5); glVertex3f(-0.5,0.5,0.5); glVertex3f(-0.5,0.5,-0.5);
        glColor3f(right_r, right_g, right_b);
        glVertex3f(0.5,-0.5,0.5); glVertex3f(0.5,-0.5,-0.5); glVertex3f(0.5,0.5,-0.5); glVertex3f(0.5,0.5,0.5);
        glEnd();

        glLineWidth(1.2f);
        glColor3f(rear_r * 0.76f, rear_g * 0.80f, rear_b * 0.84f);
        glBegin(GL_LINE_LOOP);
        glVertex3f(-0.5, 0.5, 0.5); glVertex3f(0.5, 0.5, 0.5); glVertex3f(0.5, 0.5, -0.5); glVertex3f(-0.5, 0.5, -0.5);
        glEnd();
        glBegin(GL_LINE_LOOP);
        glVertex3f(-0.5, -0.5, 0.5); glVertex3f(0.5, -0.5, 0.5); glVertex3f(0.5, -0.5, -0.5); glVertex3f(-0.5, -0.5, -0.5);
        glEnd();
        glBegin(GL_LINES);
        glVertex3f(-0.5, -0.5, 0.5); glVertex3f(-0.5, 0.5, 0.5);
        glVertex3f(0.5, -0.5, 0.5); glVertex3f(0.5, 0.5, 0.5);
        glVertex3f(0.5, -0.5, -0.5); glVertex3f(0.5, 0.5, -0.5);
        glVertex3f(-0.5, -0.5, -0.5); glVertex3f(-0.5, 0.5, -0.5);
        glEnd();

        if ((i % 19) == 0) {
            glColor3f(0.35f, 0.78f, 0.84f);
            glBegin(GL_LINES);
            glVertex3f(-0.5f, 0.12f, 0.51f);
            glVertex3f(0.5f, 0.12f, 0.51f);
            glEnd();
        }

        glPopMatrix();
    }
}


static int get_team_base_marker(int scene_id, int team_id, float *x, float *y, float *z, float *sx, float *sy, float *sz) {
    return scene_get_team_base_marker(scene_id, team_id, x, y, z, sx, sy, sz);
}

static void draw_team_map_markers(int scene_id, int game_mode) {
    if (game_mode != MODE_TDMB && game_mode != MODE_TDMO && game_mode != MODE_CTFB) return;
    for (int team = 0; team <= 1; team++) {
        if (game_mode == MODE_CTFB) {
            float px = 0.0f, py = 0.0f, pz = 0.0f;
            float psx = 0.0f, psy = 0.0f, psz = 0.0f;
            float ptop = 0.0f;
            if (get_ctf_pedestal_anchor(scene_id, team,
                                        &px, &py, &pz,
                                        &psx, &psy, &psz,
                                        &ptop,
                                        NULL, NULL, NULL)) {
                if (team == 0) glColor3f(0.92f, 0.22f, 0.22f);
                else glColor3f(0.22f, 0.48f, 0.95f);
                glPushMatrix(); glTranslatef(px, py, pz); draw_box(psx, psy, psz); glPopMatrix();

                if (team == 0) glColor3f(1.0f, 0.46f, 0.46f);
                else glColor3f(0.44f, 0.72f, 1.0f);
                glPushMatrix(); glTranslatef(px, ptop + 0.6f, pz); draw_box(psx * 0.82f, 1.2f, psz * 0.82f); glPopMatrix();
                continue;
            }
        }

        float x = 0.0f, y = 0.0f, z = 0.0f;
        float sx = 0.0f, sy = 0.0f, sz = 0.0f;
        if (!get_team_base_marker(scene_id, team, &x, &y, &z, &sx, &sy, &sz)) continue;

        if (team == 0) glColor3f(1.0f, 0.25f, 0.25f);
        else glColor3f(0.28f, 0.55f, 1.0f);
        glPushMatrix(); glTranslatef(x, y, z); draw_box(sx, sy, sz); glPopMatrix();

        if (team == 0) glColor3f(1.0f, 0.40f, 0.30f);
        else glColor3f(0.36f, 0.72f, 1.0f);
        glPushMatrix(); glTranslatef(x, y + sy * 0.9f, z); draw_box(sx * 0.28f, sy * 1.4f, sz * 0.28f); glPopMatrix();
    }
    if (game_mode != MODE_CTFB || !local_state.ctf.active) return;
    for (int team = 0; team <= 1; team++) {
        CtfFlagState *flag = &local_state.ctf.flags[team];
        if (team == 0) glColor3f(1.0f, 0.35f, 0.35f);
        else glColor3f(0.35f, 0.65f, 1.0f);
        float fy = flag->y;
        if (flag->state == FLAG_DROPPED) {
            glPushMatrix(); glTranslatef(flag->x, fy + 1.0f, flag->z); draw_box(10.0f, 2.0f, 10.0f); glPopMatrix();
            glPushMatrix(); glTranslatef(flag->x, fy + 7.0f, flag->z); draw_box(2.5f, 10.0f, 2.5f); glPopMatrix();
        } else if (flag->state == FLAG_CARRIED) {
            glPushMatrix(); glTranslatef(flag->x, fy + 1.0f, flag->z); draw_box(3.0f, 8.0f, 3.0f); glPopMatrix();
        } else {
            glPushMatrix(); glTranslatef(flag->x, fy, flag->z); draw_box(3.0f, 12.0f, 3.0f); glPopMatrix();
        }
    }
}

void draw_terrain(const RetroLightingState *lighting) {
    TerrainHeightfield *t = scene_active_terrain();
    if (!t || !t->active || !t->heights || t->width < 2 || t->height < 2) return;
    int ref_id = (my_client_id >= 0 && my_client_id < MAX_CLIENTS) ? my_client_id : 0;
    const PlayerState *rp = &local_state.players[ref_id];
    float min_h = terrain_get_height(t, 0, 0);
    float max_h = min_h;
    for (int gz = 0; gz < t->height; gz++) {
        for (int gx = 0; gx < t->width; gx++) {
            float h = terrain_get_height(t, gx, gz);
            if (h < min_h) min_h = h;
            if (h > max_h) max_h = h;
        }
    }
    float inv_h_range = (max_h > min_h + 0.001f) ? (1.0f / (max_h - min_h)) : 0.0f;

    glDisable(GL_LIGHTING);
    for (int gz = 0; gz < t->height - 1; gz++) {
        glBegin(GL_TRIANGLE_STRIP);
        for (int gx = 0; gx < t->width; gx++) {
            float x = t->origin_x + gx * t->cell_size;
            float z0 = t->origin_z + gz * t->cell_size;
            float z1 = t->origin_z + (gz + 1) * t->cell_size;
            float h0 = terrain_get_height(t, gx, gz);
            float h1 = terrain_get_height(t, gx, gz + 1);
            float nx0 = 0.0f, ny0 = 1.0f, nz0 = 0.0f;
            float nx1 = 0.0f, ny1 = 1.0f, nz1 = 0.0f;
            terrain_sample_normal(t, x, z0, &nx0, &ny0, &nz0);
            terrain_sample_normal(t, x, z1, &nx1, &ny1, &nz1);

            if (!vs0_art_direction_enabled) {
                float slope0 = 1.0f - ny0;
                float shade0 = 0.48f + h0 * 0.0016f - slope0 * 0.35f;
                if (shade0 < 0.26f) shade0 = 0.26f;
                if (shade0 > 0.84f) shade0 = 0.84f;
                float r0 = shade0 * 0.92f, g0 = shade0 * 0.70f, b0 = shade0 * 0.48f;
                if (local_state.scene_id == SCENE_STADIUM) {
                    float road0 = stadium_track_weight_at(x, z0);
                    if (road0 > 0.0f) {
                        float gravel_r = 0.54f + shade0 * 0.18f;
                        float gravel_g = 0.49f + shade0 * 0.12f;
                        float gravel_b = 0.39f + shade0 * 0.08f;
                        r0 = r0 * (1.0f - road0 * 0.72f) + gravel_r * road0 * 0.72f;
                        g0 = g0 * (1.0f - road0 * 0.72f) + gravel_g * road0 * 0.72f;
                        b0 = b0 * (1.0f - road0 * 0.72f) + gravel_b * road0 * 0.72f;
                    }
                }
                glColor3f(r0, g0, b0);
            } else {
                float r0 = 1.0f, g0 = 1.0f, b0 = 1.0f;
                retro_eval_terrain_vertex_rgb(lighting, local_state.scene_id, x, z0, h0, min_h, inv_h_range, nx0, ny0, nz0, &r0, &g0, &b0);
                if (local_state.scene_id == SCENE_STADIUM) {
                    float road0 = stadium_track_weight_at(x, z0) * 0.55f;
                    r0 = r0 * (1.0f - road0) + 0.60f * road0;
                    g0 = g0 * (1.0f - road0) + 0.53f * road0;
                    b0 = b0 * (1.0f - road0) + 0.43f * road0;
                }
                float dx0 = x - rp->x;
                float dz0 = z0 - rp->z;
                retro_apply_fog_rgb(&r0, &g0, &b0, lighting, sqrtf(dx0 * dx0 + dz0 * dz0));
                glColor3f(r0, g0, b0);
            }
            glVertex3f(x, h0, z0);

            if (!vs0_art_direction_enabled) {
                float slope1 = 1.0f - ny1;
                float shade1 = 0.48f + h1 * 0.0016f - slope1 * 0.35f;
                if (shade1 < 0.26f) shade1 = 0.26f;
                if (shade1 > 0.84f) shade1 = 0.84f;
                float r1 = shade1 * 0.92f, g1 = shade1 * 0.70f, b1 = shade1 * 0.48f;
                if (local_state.scene_id == SCENE_STADIUM) {
                    float road1 = stadium_track_weight_at(x, z1);
                    if (road1 > 0.0f) {
                        float gravel_r = 0.54f + shade1 * 0.18f;
                        float gravel_g = 0.49f + shade1 * 0.12f;
                        float gravel_b = 0.39f + shade1 * 0.08f;
                        r1 = r1 * (1.0f - road1 * 0.72f) + gravel_r * road1 * 0.72f;
                        g1 = g1 * (1.0f - road1 * 0.72f) + gravel_g * road1 * 0.72f;
                        b1 = b1 * (1.0f - road1 * 0.72f) + gravel_b * road1 * 0.72f;
                    }
                }
                glColor3f(r1, g1, b1);
            } else {
                float r1 = 1.0f, g1 = 1.0f, b1 = 1.0f;
                retro_eval_terrain_vertex_rgb(lighting, local_state.scene_id, x, z1, h1, min_h, inv_h_range, nx1, ny1, nz1, &r1, &g1, &b1);
                if (local_state.scene_id == SCENE_STADIUM) {
                    float road1 = stadium_track_weight_at(x, z1) * 0.55f;
                    r1 = r1 * (1.0f - road1) + 0.60f * road1;
                    g1 = g1 * (1.0f - road1) + 0.53f * road1;
                    b1 = b1 * (1.0f - road1) + 0.43f * road1;
                }
                float dx1 = x - rp->x;
                float dz1 = z1 - rp->z;
                retro_apply_fog_rgb(&r1, &g1, &b1, lighting, sqrtf(dx1 * dx1 + dz1 * dz1));
                glColor3f(r1, g1, b1);
            }
            glVertex3f(x, h1, z1);
        }
        glEnd();
    }

    if (terrain_wireframe_debug) {
        glLineWidth(1.0f);
        glColor3f(0.15f, 0.9f, 0.9f);
        glBegin(GL_LINES);
        for (int gz = 0; gz < t->height; gz++) {
            for (int gx = 0; gx < t->width - 1; gx++) {
                float x0 = t->origin_x + gx * t->cell_size;
                float x1 = t->origin_x + (gx + 1) * t->cell_size;
                float z = t->origin_z + gz * t->cell_size;
                glVertex3f(x0, terrain_get_height(t, gx, gz) + 0.05f, z);
                glVertex3f(x1, terrain_get_height(t, gx + 1, gz) + 0.05f, z);
            }
        }
        for (int gx = 0; gx < t->width; gx++) {
            for (int gz = 0; gz < t->height - 1; gz++) {
                float x = t->origin_x + gx * t->cell_size;
                float z0 = t->origin_z + gz * t->cell_size;
                float z1 = t->origin_z + (gz + 1) * t->cell_size;
                glVertex3f(x, terrain_get_height(t, gx, gz) + 0.05f, z0);
                glVertex3f(x, terrain_get_height(t, gx, gz + 1) + 0.05f, z1);
            }
        }
        glEnd();
    }

    if (terrain_normals_debug) {
        glLineWidth(1.0f);
        glColor3f(0.95f, 0.2f, 0.95f);
        glBegin(GL_LINES);
        for (int gz = 1; gz < t->height - 1; gz += 4) {
            for (int gx = 1; gx < t->width - 1; gx += 4) {
                float x = t->origin_x + gx * t->cell_size;
                float z = t->origin_z + gz * t->cell_size;
                float y = terrain_get_height(t, gx, gz);
                float nx = 0.0f, ny = 1.0f, nz = 0.0f;
                terrain_sample_normal(t, x, z, &nx, &ny, &nz);
                glVertex3f(x, y + 0.4f, z);
                glVertex3f(x + nx * 5.0f, y + 0.4f + ny * 5.0f, z + nz * 5.0f);
            }
        }
        glEnd();
    }

    if (voxworld_points_debug && local_state.scene_id == SCENE_VOXWORLD) {
        int pad_count = 0;
        const VehiclePad *pads = scene_vehicle_pads(SCENE_VOXWORLD, &pad_count);
        glPointSize(8.0f);
        glBegin(GL_POINTS);
        glColor3f(0.1f, 1.0f, 0.2f);
        for (int i = 0; i < pad_count; i++) {
            glVertex3f(pads[i].x, terrain_sample_height(t, pads[i].x, pads[i].z) + 1.5f, pads[i].z);
        }
        int flag_count = 0;
        const Vec2 *flags = voxworld_get_flag_homes(&flag_count);
        glColor3f(1.0f, 0.95f, 0.1f);
        for (int i = 0; i < flag_count; i++) {
            glVertex3f(flags[i].x, terrain_sample_height(t, flags[i].x, flags[i].y) + 1.5f, flags[i].y);
        }
        int anchor_count = 0;
        const VoxRouteAnchor *anchors = voxworld_get_route_anchors(&anchor_count);
        glColor3f(0.5f, 0.8f, 1.0f);
        for (int i = 0; i < anchor_count; i++) {
            glVertex3f(anchors[i].x, terrain_sample_height(t, anchors[i].x, anchors[i].z) + 1.5f, anchors[i].z);
        }
        glEnd();
    }
    if (voxworld_points_debug && local_state.scene_id == SCENE_DUST_COMPOUND) {
        int count = 0;
        const Vec2 *atk = dust_get_spawn_points_attack(&count);
        glPointSize(8.0f);
        glBegin(GL_POINTS);
        glColor3f(0.1f, 1.0f, 0.3f);
        for (int i = 0; i < count; i++) glVertex3f(atk[i].x, terrain_sample_height(t, atk[i].x, atk[i].y) + 1.5f, atk[i].y);
        const Vec2 *def = dust_get_spawn_points_defend(&count);
        glColor3f(0.95f, 0.35f, 0.25f);
        for (int i = 0; i < count; i++) glVertex3f(def[i].x, terrain_sample_height(t, def[i].x, def[i].y) + 1.5f, def[i].y);
        const Vec2 *dm = dust_get_spawn_points_dm(&count);
        glColor3f(0.7f, 0.7f, 0.7f);
        for (int i = 0; i < count; i++) glVertex3f(dm[i].x, terrain_sample_height(t, dm[i].x, dm[i].y) + 1.2f, dm[i].y);
        int objective_count = 0;
        const VoxRouteAnchor *objectives = dust_get_objective_anchors(&objective_count);
        glColor3f(1.0f, 0.9f, 0.1f);
        for (int i = 0; i < objective_count; i++) glVertex3f(objectives[i].x, terrain_sample_height(t, objectives[i].x, objectives[i].z) + 2.0f, objectives[i].z);
        int route_count = 0;
        const VoxRouteAnchor *routes = dust_get_route_anchors(&route_count);
        glColor3f(0.4f, 0.85f, 1.0f);
        for (int i = 0; i < route_count; i++) glVertex3f(routes[i].x, terrain_sample_height(t, routes[i].x, routes[i].z) + 2.0f, routes[i].z);
        glEnd();
    }
    if (voxworld_points_debug && local_state.scene_id == SCENE_POO_POO_ISLAND) {
        int count = 0;
        const Vec2 *spawns = poo_poo_island_get_spawn_points(&count);
        glPointSize(9.0f);
        glBegin(GL_POINTS);
        glColor3f(0.75f, 0.90f, 1.0f);
        for (int i = 0; i < count; i++) glVertex3f(spawns[i].x, terrain_sample_height(t, spawns[i].x, spawns[i].y) + 1.8f, spawns[i].y);
        int route_count = 0;
        const VoxRouteAnchor *routes = poo_poo_island_get_route_anchors(&route_count);
        glColor3f(0.35f, 0.95f, 0.88f);
        for (int i = 0; i < route_count; i++) glVertex3f(routes[i].x, terrain_sample_height(t, routes[i].x, routes[i].z) + 2.1f, routes[i].z);
        int landmark_count = 0;
        const VoxRouteAnchor *landmarks = poo_poo_island_get_landmark_anchors(&landmark_count);
        glColor3f(1.0f, 0.88f, 0.22f);
        for (int i = 0; i < landmark_count; i++) glVertex3f(landmarks[i].x, terrain_sample_height(t, landmarks[i].x, landmarks[i].z) + 2.6f, landmarks[i].z);
        int hub_count = 0;
        const VoxRouteAnchor *hubs = poo_poo_island_get_hub_anchors(&hub_count);
        glColor3f(1.0f, 0.55f, 0.34f);
        for (int i = 0; i < hub_count; i++) glVertex3f(hubs[i].x, terrain_sample_height(t, hubs[i].x, hubs[i].z) + 2.2f, hubs[i].z);
        int scenic_count = 0;
        const VoxRouteAnchor *scenic = poo_poo_island_get_scenic_anchors(&scenic_count);
        glColor3f(0.5f, 0.72f, 1.0f);
        for (int i = 0; i < scenic_count; i++) glVertex3f(scenic[i].x, terrain_sample_height(t, scenic[i].x, scenic[i].z) + 2.2f, scenic[i].z);
        int activity_count = 0;
        const VoxRouteAnchor *activity = poo_poo_island_get_activity_anchors(&activity_count);
        glColor3f(0.60f, 1.0f, 0.44f);
        for (int i = 0; i < activity_count; i++) glVertex3f(activity[i].x, terrain_sample_height(t, activity[i].x, activity[i].z) + 2.0f, activity[i].z);
        glEnd();
    }
}

static void draw_box_solid(float hx, float hy, float hz) {
    glBegin(GL_QUADS);
    glNormal3f(0, 1, 0);
    glVertex3f(-hx, hy, hz); glVertex3f(hx, hy, hz); glVertex3f(hx, hy, -hz); glVertex3f(-hx, hy, -hz);
    glNormal3f(0, -1, 0);
    glVertex3f(-hx, -hy, hz); glVertex3f(hx, -hy, hz); glVertex3f(hx, -hy, -hz); glVertex3f(-hx, -hy, -hz);
    glNormal3f(0, 0, 1);
    glVertex3f(-hx, -hy, hz); glVertex3f(hx, -hy, hz); glVertex3f(hx, hy, hz); glVertex3f(-hx, hy, hz);
    glNormal3f(0, 0, -1);
    glVertex3f(-hx, -hy, -hz); glVertex3f(hx, -hy, -hz); glVertex3f(hx, hy, -hz); glVertex3f(-hx, hy, -hz);
    glNormal3f(-1, 0, 0);
    glVertex3f(-hx, -hy, -hz); glVertex3f(-hx, -hy, hz); glVertex3f(-hx, hy, hz); glVertex3f(-hx, hy, -hz);
    glNormal3f(1, 0, 0);
    glVertex3f(hx, -hy, hz); glVertex3f(hx, -hy, -hz); glVertex3f(hx, hy, -hz); glVertex3f(hx, hy, hz);
    glEnd();
}

static void draw_single_bush(const BushProp *b) {
    float tint = b->tint;
    float base_r = lerpf(0.20f, 0.26f, tint);
    float base_g = lerpf(0.31f, 0.39f, tint);
    float base_b = lerpf(0.16f, 0.21f, tint);
    float top_r = lerpf(0.36f, 0.48f, tint);
    float top_g = lerpf(0.52f, 0.64f, tint);
    float top_b = lerpf(0.23f, 0.28f, tint);
    float stem_r = 0.28f, stem_g = 0.22f, stem_b = 0.16f;

    glPushMatrix();
    glTranslatef(b->x, b->y, b->z);
    glRotatef(b->yaw, 0.0f, 1.0f, 0.0f);
    glScalef(b->scale, b->scale, b->scale);

    glColor3f(stem_r, stem_g, stem_b);
    glPushMatrix();
    glTranslatef(0.0f, 0.65f, 0.0f);
    draw_box_solid(0.13f, 0.65f, 0.13f);
    glPopMatrix();

    glColor3f(base_r, base_g, base_b);
    glPushMatrix();
    glTranslatef(-0.56f, 1.05f, -0.20f);
    draw_box_solid(0.54f, 0.56f, 0.46f);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(0.48f, 1.16f, 0.14f);
    draw_box_solid(0.52f, 0.52f, 0.48f);
    glPopMatrix();

    glColor3f(top_r, top_g, top_b);
    glPushMatrix();
    glTranslatef(-0.12f, 1.58f, 0.10f);
    draw_box_solid(0.58f, 0.48f, 0.56f);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(0.10f, 1.22f, -0.55f);
    draw_box_solid(0.40f, 0.36f, 0.36f);
    glPopMatrix();
    glPopMatrix();
}

static float grass_hash01(float x, float z, float seed) {
    float n = sinf(x * 0.127f + z * 0.193f + seed * 13.17f) * 43758.5453f;
    return n - floorf(n);
}

static void draw_voxworld_grass_overlay(const RetroLightingState *lighting, const PlayerState *viewer) {
    if (local_state.scene_id != SCENE_VOXWORLD || !viewer) return;
    TerrainHeightfield *t = scene_active_terrain();
    if (!t || !t->heights) return;

    const float spacing = 26.0f;
    const float radius = 980.0f;
    const float start_x = floorf((viewer->x - radius) / spacing) * spacing;
    const float end_x = ceilf((viewer->x + radius) / spacing) * spacing;
    const float start_z = floorf((viewer->z - radius) / spacing) * spacing;
    const float end_z = ceilf((viewer->z + radius) / spacing) * spacing;

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glBegin(GL_QUADS);
    for (float z = start_z; z <= end_z; z += spacing) {
        for (float x = start_x; x <= end_x; x += spacing) {
            float jx = (grass_hash01(x, z, 1.0f) - 0.5f) * spacing * 0.72f;
            float jz = (grass_hash01(x, z, 2.0f) - 0.5f) * spacing * 0.72f;
            float gx = x + jx;
            float gz = z + jz;
            if (!terrain_contains_world(t, gx, gz)) continue;

            float mask = voxworld_canyon_grass_mask(gx, gz);
            if (mask < 0.34f) continue;
            if (grass_hash01(gx, gz, 3.0f) > mask) continue;

            float nx, ny, nz;
            terrain_sample_normal(t, gx, gz, &nx, &ny, &nz);
            if (ny < 0.72f) continue;

            float y = terrain_sample_height(t, gx, gz) + 0.08f;
            float dist_x = gx - viewer->x;
            float dist_z = gz - viewer->z;
            float dist = sqrtf(dist_x * dist_x + dist_z * dist_z);
            float far_fade = 1.0f - smoothstepf(640.0f, radius, dist);
            if (far_fade <= 0.02f) continue;

            float h = 0.75f + grass_hash01(gx, gz, 5.0f) * 1.05f;
            float half_w = 0.10f + grass_hash01(gx, gz, 7.0f) * 0.14f;
            float yaw = grass_hash01(gx, gz, 9.0f) * 6.2831853f;
            float dx = cosf(yaw) * half_w;
            float dz = sinf(yaw) * half_w;

            float lit_r = 1.0f, lit_g = 1.0f, lit_b = 1.0f;
            retro_eval_brush_lighting_rgb(lighting, nx, ny, nz, 0.58f, &lit_r, &lit_g, &lit_b);

            float base_r = (0.16f + mask * 0.07f) * lit_r * far_fade;
            float base_g = (0.30f + mask * 0.18f) * lit_g * far_fade;
            float base_b = (0.12f + mask * 0.06f) * lit_b * far_fade;
            float top_r = (0.28f + mask * 0.09f) * lit_r * far_fade;
            float top_g = (0.55f + mask * 0.22f) * lit_g * far_fade;
            float top_b = (0.18f + mask * 0.08f) * lit_b * far_fade;
            retro_apply_fog_rgb(&base_r, &base_g, &base_b, lighting, dist);
            retro_apply_fog_rgb(&top_r, &top_g, &top_b, lighting, dist);

            glColor3f(base_r, base_g, base_b);
            glVertex3f(gx - dx, y, gz - dz);
            glVertex3f(gx + dx, y, gz + dz);
            glColor3f(top_r, top_g, top_b);
            glVertex3f(gx + dx * 0.38f, y + h, gz + dz * 0.38f);
            glVertex3f(gx - dx * 0.38f, y + h, gz - dz * 0.38f);

            float dx2 = -dz;
            float dz2 = dx;
            glColor3f(base_r, base_g, base_b);
            glVertex3f(gx - dx2, y, gz - dz2);
            glVertex3f(gx + dx2, y, gz + dz2);
            glColor3f(top_r, top_g, top_b);
            glVertex3f(gx + dx2 * 0.38f, y + h * 0.92f, gz + dz2 * 0.38f);
            glVertex3f(gx - dx2 * 0.38f, y + h * 0.92f, gz - dz2 * 0.38f);
        }
    }
    glEnd();
}

static void draw_voxworld_bushes(void) {
    if (local_state.scene_id != SCENE_VOXWORLD) return;
    int bush_count = 0;
    const BushProp *bushes = voxworld_get_bushes(&bush_count);
    if (!bushes || bush_count <= 0) return;
    for (int i = 0; i < bush_count; i++) draw_single_bush(&bushes[i]);
}

static void draw_box_planar_uv(float hx, float hy, float hz, float scroll) {
    glBegin(GL_QUADS);
    float x, y, z;
    glNormal3f(0, 1, 0);
    x=-hx; y=hy; z=hz; glTexCoord2f(x * 0.35f + scroll, z * 0.35f); glVertex3f(x, y, z);
    x=hx; y=hy; z=hz; glTexCoord2f(x * 0.35f + scroll, z * 0.35f); glVertex3f(x, y, z);
    x=hx; y=hy; z=-hz; glTexCoord2f(x * 0.35f + scroll, z * 0.35f); glVertex3f(x, y, z);
    x=-hx; y=hy; z=-hz; glTexCoord2f(x * 0.35f + scroll, z * 0.35f); glVertex3f(x, y, z);

    glNormal3f(0, 0, 1);
    x=-hx; y=-hy; z=hz; glTexCoord2f(x * 0.35f + scroll, z * 0.35f); glVertex3f(x, y, z);
    x=hx; y=-hy; z=hz; glTexCoord2f(x * 0.35f + scroll, z * 0.35f); glVertex3f(x, y, z);
    x=hx; y=hy; z=hz; glTexCoord2f(x * 0.35f + scroll, z * 0.35f); glVertex3f(x, y, z);
    x=-hx; y=hy; z=hz; glTexCoord2f(x * 0.35f + scroll, z * 0.35f); glVertex3f(x, y, z);

    glNormal3f(0, 0, -1);
    x=-hx; y=-hy; z=-hz; glTexCoord2f(x * 0.35f + scroll, z * 0.35f); glVertex3f(x, y, z);
    x=hx; y=-hy; z=-hz; glTexCoord2f(x * 0.35f + scroll, z * 0.35f); glVertex3f(x, y, z);
    x=hx; y=hy; z=-hz; glTexCoord2f(x * 0.35f + scroll, z * 0.35f); glVertex3f(x, y, z);
    x=-hx; y=hy; z=-hz; glTexCoord2f(x * 0.35f + scroll, z * 0.35f); glVertex3f(x, y, z);
    glEnd();
}

void draw_buggy_model(PlayerState *p) {
    uint32_t pid_seed = g_vehicle_style.seed ^ (uint32_t)(p ? (p->id + 1) * 2654435761u : 0u);
    const float t_sec = SDL_GetTicks() * 0.001f;
    const float scroll = t_sec * g_vehicle_style.neon_scroll + (float)(pid_seed & 255u) * 0.0007f;
    GLfloat specular[] = {g_vehicle_style.spec * 0.35f, g_vehicle_style.spec * 0.35f, g_vehicle_style.spec * 0.35f, 1.0f};
    GLfloat ambient[] = {0.06f, 0.06f, 0.07f, 1.0f};
    GLfloat diffuse[] = {0.14f, 0.14f, 0.15f, 1.0f};
    GLfloat light_pos[] = {0.4f, 2.5f, 1.8f, 0.0f};

    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_LIGHTING_BIT | GL_TEXTURE_BIT | GL_COLOR_BUFFER_BIT | GL_LINE_BIT);

    if (!vehicle_style_enabled) {
        glDisable(GL_TEXTURE_2D);
        glColor3f(0.17f, 0.17f, 0.17f);
        glPushMatrix();
        glScalef(2.0f, 1.0f, 3.5f);
        draw_box_solid(1.0f, 1.0f, 1.0f);
        glPopMatrix();
        glPopAttrib();
        return;
    }

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glLightfv(GL_LIGHT0, GL_POSITION, light_pos);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, ambient);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, diffuse);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, specular);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 6.0f + (1.0f - g_vehicle_style.matte) * 28.0f);

    glDisable(GL_TEXTURE_2D);
    glColor4f(0.06f, 0.06f, 0.065f, 1.0f);
    glPushMatrix();
    glScalef(2.0f, 1.0f, 3.5f);
    draw_box_solid(1.0f, 1.0f, 1.0f);
    glPopMatrix();

    glDisable(GL_LIGHTING);
    glLineWidth(1.4f);
    glColor4f(0.23f, 0.23f, 0.25f, 0.9f);
    glPushMatrix();
    glScalef(2.02f, 1.02f, 3.52f);
    glBegin(GL_LINES);
    glVertex3f(-1, 1, 1); glVertex3f(1, 1, 1);
    glVertex3f(1, 1, 1); glVertex3f(1, 1, -1);
    glVertex3f(-1, 1, -1); glVertex3f(1, 1, -1);
    glVertex3f(-1, 1, 1); glVertex3f(-1, 1, -1);
    glVertex3f(-1, 0, 1); glVertex3f(1, 0, 1);
    glVertex3f(-1, 0, -1); glVertex3f(1, 0, -1);
    glEnd();
    glPopMatrix();

    glColor4f(0.9f, 0.37f, 0.08f, 0.95f);
    glPushMatrix();
    glTranslatef(0.0f, 0.45f, 2.4f);
    glScalef(1.6f, 0.12f, 0.3f);
    draw_box_solid(1.0f, 1.0f, 1.0f);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(-1.6f, 0.0f, -0.3f);
    glScalef(0.18f, 0.85f, 2.2f);
    draw_box_solid(1.0f, 1.0f, 1.0f);
    glPopMatrix();

    glEnable(GL_BLEND);
    glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_vehicle_noise_tex.tex_id);
    glColor4f(0.92f, 0.92f, 0.92f, 0.08f);
    glPushMatrix();
    glScalef(2.0f, 1.0f, 3.5f);
    draw_box_planar_uv(1.0f, 1.0f, 1.0f, scroll);
    glPopMatrix();

    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glBindTexture(GL_TEXTURE_2D, g_vehicle_glitch_tex.tex_id);
    glColor4f(0.95f, 0.15f, 0.85f, g_vehicle_style.glitch * 0.45f);
    glPushMatrix();
    glTranslatef(1.85f, 0.2f, 0.4f);
    glRotatef(90.0f, 0, 1, 0);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex3f(-0.6f, -0.45f, 0.0f);
    glTexCoord2f(1, 0); glVertex3f(0.7f, -0.45f, 0.0f);
    glTexCoord2f(1, 1); glVertex3f(0.7f, 0.4f, 0.0f);
    glTexCoord2f(0, 1); glVertex3f(-0.6f, 0.4f, 0.0f);
    glEnd();
    glPopMatrix();

    glPushMatrix();
    glTranslatef(-1.85f, 0.05f, -0.7f);
    glRotatef(-90.0f, 0, 1, 0);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex3f(-0.5f, -0.40f, 0.0f);
    glTexCoord2f(1, 0); glVertex3f(0.6f, -0.40f, 0.0f);
    glTexCoord2f(1, 1); glVertex3f(0.6f, 0.30f, 0.0f);
    glTexCoord2f(0, 1); glVertex3f(-0.5f, 0.30f, 0.0f);
    glEnd();
    glPopMatrix();

    if (vehicle_worklights_enabled) {
        glDisable(GL_TEXTURE_2D);
        glColor4f(1.0f, 0.97f, 0.88f, 0.85f);
        glPushMatrix();
        glTranslatef(0.0f, 1.2f, 1.75f);
        glBegin(GL_QUADS);
        glVertex3f(-0.7f, -0.08f, 0.0f); glVertex3f(0.7f, -0.08f, 0.0f); glVertex3f(0.7f, 0.08f, 0.0f); glVertex3f(-0.7f, 0.08f, 0.0f);
        glEnd();
        glColor4f(1.0f, 0.95f, 0.85f, 0.18f);
        glBegin(GL_QUADS);
        glVertex3f(-1.0f, -0.22f, 0.01f); glVertex3f(1.0f, -0.22f, 0.01f); glVertex3f(1.0f, 0.22f, 0.01f); glVertex3f(-1.0f, 0.22f, 0.01f);
        glEnd();
        glPopMatrix();
    }

    if (vehicle_underglow_enabled) {
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_LIGHTING);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glPushMatrix();
        glTranslatef(0.0f, -1.05f, 0.0f);
        glBegin(GL_QUADS);
        glColor4f(0.0f, 0.9f, 0.95f, g_vehicle_style.underglow_alpha * (0.8f + 0.2f * g_vehicle_style.neon));
        glVertex3f(-2.7f, 0.0f, 3.4f); glVertex3f(2.7f, 0.0f, 3.4f); glVertex3f(2.7f, 0.0f, -3.4f); glVertex3f(-2.7f, 0.0f, -3.4f);
        glColor4f(0.8f, 0.1f, 0.85f, g_vehicle_style.underglow_alpha * 0.35f);
        glVertex3f(-2.0f, 0.01f, 2.8f); glVertex3f(2.0f, 0.01f, 2.8f); glVertex3f(2.0f, 0.01f, -2.8f); glVertex3f(-2.0f, 0.01f, -2.8f);
        glEnd();
        glPopMatrix();
    }

    float wx[] = {-2.2f, 2.2f, -2.2f, 2.2f};
    float wz[] = {2.5f, 2.5f, -2.5f, -2.5f};
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
    for (int i = 0; i < 4; i++) {
        glPushMatrix();
        glTranslatef(wx[i], -0.5f, wz[i]);
        glScalef(0.8f, 1.5f, 1.5f);
        glColor3f(0.10f, 0.10f, 0.10f);
        draw_box_solid(0.5f, 0.5f, 0.5f);
        glPopMatrix();
    }

    glPopAttrib();
}

static void draw_buggy_entity(const BuggyState *b) {
    if (!b || !b->active) return;
    PlayerState style_driver;
    memset(&style_driver, 0, sizeof(style_driver));
    style_driver.id = (b->occupant_player_id >= 0) ? b->occupant_player_id : b->id;
    glPushMatrix();
    glTranslatef(b->x, b->y + 0.2f, b->z);
    glRotatef(180.0f - norm_yaw_deg(b->yaw), 0, 1, 0);
    glRotatef(b->roll, 0, 0, 1);
    glRotatef(b->pitch, 1, 0, 0);
    draw_buggy_model(&style_driver);
    glPopMatrix();
}

static void draw_thirdperson_knife_model(void) {
    glPushMatrix();
    glScalef(0.88f, 0.88f, 0.88f);

    /* handle */
    glColor3f(0.18f, 0.15f, 0.12f);
    glPushMatrix(); glTranslatef(0.0f, -0.16f, -0.34f); draw_box(0.15f, 0.09f, 0.50f); glPopMatrix();
    /* pommel */
    glColor3f(0.11f, 0.10f, 0.10f);
    glPushMatrix(); glTranslatef(0.0f, -0.15f, -0.60f); draw_box(0.12f, 0.10f, 0.14f); glPopMatrix();
    /* guard */
    glColor3f(0.44f, 0.44f, 0.46f);
    glPushMatrix(); glTranslatef(0.0f, -0.09f, -0.03f); draw_box(0.22f, 0.05f, 0.11f); glPopMatrix();
    /* blade spine (long and narrow) */
    glColor3f(0.70f, 0.73f, 0.78f);
    glPushMatrix(); glTranslatef(0.0f, -0.05f, 0.60f); draw_box(0.060f, 0.035f, 1.22f); glPopMatrix();
    /* taper section */
    glColor3f(0.74f, 0.77f, 0.82f);
    glPushMatrix(); glTranslatef(0.0f, -0.045f, 1.23f); draw_box(0.040f, 0.030f, 0.40f); glPopMatrix();
    /* point tip */
    glColor3f(0.82f, 0.84f, 0.88f);
    glPushMatrix(); glTranslatef(0.0f, -0.04f, 1.49f); glRotatef(-18.0f, 1, 0, 0); draw_box(0.020f, 0.020f, 0.24f); glPopMatrix();
    glPopMatrix();
}

void draw_gun_model(int weapon_id) {
    const float weapon_scale = 0.9f;
    if (weapon_id == WPN_KNIFE) {
        draw_thirdperson_knife_model();
        return;
    }
    if (weapon_id == WPN_KATANA) {
        glPushMatrix();
        glScalef(weapon_scale, weapon_scale, weapon_scale);
        glColor3f(0.15f, 0.12f, 0.08f);
        glPushMatrix(); glTranslatef(0.0f, -0.25f, -0.65f); glScalef(0.09f, 0.09f, 0.35f); draw_box(1.0f, 1.0f, 1.0f); glPopMatrix();
        glColor3f(0.75f, 0.78f, 0.84f);
        glPushMatrix(); glTranslatef(0.0f, 0.0f, 0.45f); glScalef(0.05f, 0.03f, 1.4f); draw_box(1.0f, 1.0f, 1.0f); glPopMatrix();
        glColor3f(0.0f, 0.9f, 1.0f);
        glBegin(GL_LINES);
        glVertex3f(0.0f, 0.02f, -0.15f); glVertex3f(0.0f, 0.02f, 1.7f);
        glEnd();
        glPopMatrix();
        return;
    }
    float base_r = 0.2f, base_g = 0.24f, base_b = 0.22f;
    switch(weapon_id) {
        case WPN_KNIFE:   base_r = 0.62f; base_g = 0.65f; base_b = 0.70f; glScalef(0.05f * weapon_scale, 0.05f * weapon_scale, 0.8f * weapon_scale); break;
        case WPN_MAGNUM:  base_r = 0.23f; base_g = 0.25f; base_b = 0.28f; glScalef(0.13f * weapon_scale, 0.18f * weapon_scale, 0.43f * weapon_scale); break;
        case WPN_AR:      base_r = 0.18f; base_g = 0.34f; base_b = 0.18f; glScalef(0.08f * weapon_scale, 0.14f * weapon_scale, 1.08f * weapon_scale); break;
        case WPN_SHOTGUN: base_r = 0.30f; base_g = 0.24f; base_b = 0.20f; glScalef(0.25f * weapon_scale, 0.15f * weapon_scale, 0.8f * weapon_scale); break;
        case WPN_SNIPER:  base_r = 0.17f; base_g = 0.19f; base_b = 0.24f; glScalef(0.06f * weapon_scale, 0.10f * weapon_scale, 2.20f * weapon_scale); break;
    }
    float cr = base_r, cg = base_g, cb = base_b;
    if (!vs0_art_direction_enabled) {
        switch(weapon_id) {
            case WPN_KNIFE:   cr = 0.8f; cg = 0.8f; cb = 0.9f; break;
            case WPN_MAGNUM:  cr = 0.4f; cg = 0.4f; cb = 0.4f; break;
            case WPN_AR:      cr = 0.2f; cg = 0.3f; cb = 0.2f; break;
            case WPN_SHOTGUN: cr = 0.5f; cg = 0.3f; cb = 0.2f; break;
            case WPN_SNIPER:  cr = 0.1f; cg = 0.1f; cb = 0.15f; break;
        }
    }

    glBegin(GL_QUADS);
    glColor3f(vs0_art_direction_enabled ? cr * 1.2f : cr, vs0_art_direction_enabled ? cg * 1.2f : cg, vs0_art_direction_enabled ? cb * 1.2f : cb);
    glVertex3f(-1,1,1); glVertex3f(1,1,1); glVertex3f(1,1,-1); glVertex3f(-1,1,-1);
    glColor3f(vs0_art_direction_enabled ? cr * 0.95f : cr, vs0_art_direction_enabled ? cg * 0.95f : cg, vs0_art_direction_enabled ? cb * 0.95f : cb);
    glVertex3f(-1,-1,1); glVertex3f(1,-1,1); glVertex3f(1,1,1); glVertex3f(-1,1,1);
    glColor3f(vs0_art_direction_enabled ? cr * 0.70f : cr, vs0_art_direction_enabled ? cg * 0.72f : cg, vs0_art_direction_enabled ? cb * 0.75f : cb);
    glVertex3f(-1,-1,-1); glVertex3f(-1,1,-1); glVertex3f(1,1,-1); glVertex3f(1,-1,-1);
    glColor3f(vs0_art_direction_enabled ? cr * 0.78f : cr, vs0_art_direction_enabled ? cg * 0.80f : cg, vs0_art_direction_enabled ? cb * 0.84f : cb);
    glVertex3f(1,-1,-1); glVertex3f(1,1,-1); glVertex3f(1,1,1); glVertex3f(1,-1,1);
    glColor3f(vs0_art_direction_enabled ? cr * 0.72f : cr, vs0_art_direction_enabled ? cg * 0.75f : cg, vs0_art_direction_enabled ? cb * 0.80f : cb);
    glVertex3f(-1,-1,1); glVertex3f(-1,1,1); glVertex3f(-1,1,-1); glVertex3f(-1,-1,-1);
    glEnd();

    glLineWidth(vs0_art_direction_enabled ? 0.9f : 1.0f);
    if (vs0_art_direction_enabled) glColor3f(0.14f, 0.16f, 0.18f);
    else glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_LINES);
    glVertex3f(-1,1,1); glVertex3f(1,1,1);
    glVertex3f(1,1,1); glVertex3f(1,1,-1);
    glVertex3f(-1,1,1); glVertex3f(-1,-1,1);
    glEnd();
}

typedef struct ViewmodelPalette {
    float body_r, body_g, body_b;
    float support_r, support_g, support_b;
    float accent_r, accent_g, accent_b;
} ViewmodelPalette;

typedef struct ViewmodelTuning {
    float base_x, base_y, base_z;
    float recoil_pitch;
    float recoil_back;
    float recoil_roll;
    float kick_scale;
    float idle_scale;
} ViewmodelTuning;

/*
 * Viewmodel art-direction contract:
 * - silhouette first (chunky 5-8 major masses)
 * - explicit 3-tone plane grouping (top / side / underside)
 * - minimal accent strips only
 * - punchy recoil + muzzle VFX carries impact
 */
static ViewmodelPalette viewmodel_palette_for_weapon(int weapon_id) {
    ViewmodelPalette p = {0.22f, 0.24f, 0.27f, 0.17f, 0.19f, 0.22f, 0.95f, 0.85f, 0.20f};
    switch (weapon_id) {
        case WPN_MAGNUM:  p = (ViewmodelPalette){0.24f, 0.26f, 0.29f, 0.15f, 0.17f, 0.20f, 0.95f, 0.84f, 0.22f}; break;
        case WPN_AR:      p = (ViewmodelPalette){0.20f, 0.33f, 0.21f, 0.17f, 0.26f, 0.18f, 0.95f, 0.82f, 0.18f}; break;
        case WPN_SHOTGUN: p = (ViewmodelPalette){0.23f, 0.22f, 0.21f, 0.31f, 0.27f, 0.24f, 0.96f, 0.83f, 0.16f}; break;
        case WPN_SNIPER:  p = (ViewmodelPalette){0.20f, 0.22f, 0.26f, 0.30f, 0.33f, 0.37f, 0.36f, 0.83f, 0.88f}; break;
        case WPN_KNIFE:   p = (ViewmodelPalette){0.34f, 0.36f, 0.40f, 0.16f, 0.17f, 0.19f, 0.34f, 0.82f, 0.86f}; break;
        default: break;
    }
    if (!vs0_art_direction_enabled) {
        p.body_r *= 1.25f; p.body_g *= 1.25f; p.body_b *= 1.25f;
        p.support_r *= 1.35f; p.support_g *= 1.35f; p.support_b *= 1.35f;
    }
    return p;
}

static ViewmodelTuning viewmodel_tuning_for_weapon(int weapon_id) {
    ViewmodelTuning t = {0.48f, -0.60f, -1.34f, 13.0f, 0.13f, 1.8f, 0.24f, 0.10f};
    switch (weapon_id) {
        case WPN_MAGNUM:  t = (ViewmodelTuning){0.62f, -0.63f, -1.24f, 17.0f, 0.17f, 2.0f, 0.22f, 0.08f}; break;
        case WPN_AR:      t = (ViewmodelTuning){0.54f, -0.64f, -1.48f, 11.0f, 0.11f, 1.4f, 0.20f, 0.11f}; break;
        case WPN_SHOTGUN: t = (ViewmodelTuning){0.59f, -0.67f, -1.34f, 22.0f, 0.22f, 3.0f, 0.28f, 0.08f}; break;
        case WPN_SNIPER:  t = (ViewmodelTuning){0.49f, -0.63f, -1.58f, 19.0f, 0.19f, 2.1f, 0.20f, 0.07f}; break;
        case WPN_KNIFE:   t = (ViewmodelTuning){0.54f, -0.56f, -1.00f, 0.0f, 0.0f, 0.0f, 0.00f, 0.07f}; break;
        case WPN_KATANA:  t = (ViewmodelTuning){0.64f, -0.58f, -1.05f, 7.0f, 0.03f, 8.0f, 0.06f, 0.14f}; break;
        default: break;
    }
    return t;
}

static int player_is_flag_carrying_viewmodel(const PlayerState *p) {
    return (local_state.game_mode == MODE_CTFB && p->carried_flag_team_id >= 0);
}

static ViewmodelTuning viewmodel_tuning_for_flag_carry(int carried_flag_team_id) {
    (void)carried_flag_team_id;
    return (ViewmodelTuning){0.30f, -0.46f, -0.98f, 0.0f, 0.0f, 0.0f, 0.0f, 0.12f};
}

static void draw_viewmodel_box_tone(const ViewmodelPalette *p, float w, float h, float d, int support_tone) {
    float base_r = support_tone ? p->support_r : p->body_r;
    float base_g = support_tone ? p->support_g : p->body_g;
    float base_b = support_tone ? p->support_b : p->body_b;
    float top = vs0_art_direction_enabled ? 1.34f : 1.08f;
    float side = vs0_art_direction_enabled ? 0.92f : 1.0f;
    float under = vs0_art_direction_enabled ? 0.58f : 0.92f;
    float back = vs0_art_direction_enabled ? 0.68f : 0.94f;

    glPushMatrix();
    glScalef(w, h, d);
    glBegin(GL_QUADS);
    glColor3f(base_r * side, base_g * side, base_b * side);
    glVertex3f(-0.5f,-0.5f,0.5f); glVertex3f(0.5f,-0.5f,0.5f); glVertex3f(0.5f,0.5f,0.5f); glVertex3f(-0.5f,0.5f,0.5f);
    glColor3f(base_r * back, base_g * back, base_b * back);
    glVertex3f(-0.5f,-0.5f,-0.5f); glVertex3f(-0.5f,0.5f,-0.5f); glVertex3f(0.5f,0.5f,-0.5f); glVertex3f(0.5f,-0.5f,-0.5f);
    glColor3f(base_r * 0.84f, base_g * 0.84f, base_b * 0.84f);
    glVertex3f(-0.5f,-0.5f,-0.5f); glVertex3f(-0.5f,-0.5f,0.5f); glVertex3f(-0.5f,0.5f,0.5f); glVertex3f(-0.5f,0.5f,-0.5f);
    glColor3f(base_r * 0.94f, base_g * 0.94f, base_b * 0.94f);
    glVertex3f(0.5f,-0.5f,-0.5f); glVertex3f(0.5f,0.5f,-0.5f); glVertex3f(0.5f,0.5f,0.5f); glVertex3f(0.5f,-0.5f,0.5f);
    glColor3f(base_r * top, base_g * top, base_b * top);
    glVertex3f(-0.5f,0.5f,0.5f); glVertex3f(0.5f,0.5f,0.5f); glVertex3f(0.5f,0.5f,-0.5f); glVertex3f(-0.5f,0.5f,-0.5f);
    glColor3f(base_r * under, base_g * under, base_b * under);
    glVertex3f(-0.5f,-0.5f,0.5f); glVertex3f(-0.5f,-0.5f,-0.5f); glVertex3f(0.5f,-0.5f,-0.5f); glVertex3f(0.5f,-0.5f,0.5f);
    glEnd();
    if (vs0_art_direction_enabled) {
        glLineWidth(1.0f);
        glColor3f(0.11f, 0.13f, 0.16f);
        glBegin(GL_LINE_LOOP);
        glVertex3f(-0.5f,0.5f,0.5f); glVertex3f(0.5f,0.5f,0.5f); glVertex3f(0.5f,0.5f,-0.5f); glVertex3f(-0.5f,0.5f,-0.5f);
        glEnd();
    }
    glPopMatrix();
}

static void draw_viewmodel_accent_strip(const ViewmodelPalette *p, float w, float h, float d) {
    glColor3f(p->accent_r, p->accent_g, p->accent_b);
    draw_box(w, h, d);
}

static void draw_viewmodel_magnum(const ViewmodelPalette *p) {
    /* Rear-biased stepped upper: heavier back mass keeps a magnum silhouette. */
    glPushMatrix(); glTranslatef(0.00f, 0.18f, -0.42f); draw_viewmodel_box_tone(p, 0.84f, 0.34f, 0.78f, 0); glPopMatrix(); /* rear upper block */
    glPushMatrix(); glTranslatef(0.00f, 0.14f, 0.24f); draw_viewmodel_box_tone(p, 0.72f, 0.28f, 0.90f, 0); glPopMatrix();   /* mid upper block */
    glPushMatrix(); glTranslatef(0.00f, 0.09f, 1.04f); draw_viewmodel_box_tone(p, 0.54f, 0.21f, 0.66f, 0); glPopMatrix();   /* front upper / muzzle body */

    /* Stronger frame + cylinder zone, separated from the upper body. */
    glPushMatrix(); glTranslatef(0.00f, -0.02f, 0.20f); draw_viewmodel_box_tone(p, 0.78f, 0.24f, 0.70f, 1); glPopMatrix();  /* frame / cylinder bulge */
    glPushMatrix(); glTranslatef(0.00f, -0.08f, 0.92f); draw_viewmodel_box_tone(p, 0.48f, 0.16f, 0.56f, 1); glPopMatrix();  /* underbarrel support */
    glPushMatrix(); glTranslatef(0.00f, -0.05f, -0.30f); draw_viewmodel_box_tone(p, 0.60f, 0.22f, 0.52f, 1); glPopMatrix(); /* rear frame block */

    /* Sighting plane: readable rear notch + front post. */
    glPushMatrix(); glTranslatef(0.00f, 0.34f, -0.30f); draw_viewmodel_box_tone(p, 0.30f, 0.09f, 0.16f, 1); glPopMatrix();  /* rear sight block */
    glPushMatrix(); glTranslatef(0.00f, 0.24f, 1.36f); draw_viewmodel_box_tone(p, 0.14f, 0.11f, 0.10f, 1); glPopMatrix();   /* front sight post */

    /* Intentional muzzle tip: narrower than the rear to avoid center-screen bloat. */
    glPushMatrix(); glTranslatef(0.00f, 0.06f, 1.42f); draw_viewmodel_box_tone(p, 0.40f, 0.17f, 0.20f, 1); glPopMatrix();   /* muzzle cap */

    /* Slimmer, longer grip with backward rake and a separate butt mass. */
    glPushMatrix(); glTranslatef(0.00f, -0.30f, -0.38f); glRotatef(-19.0f, 1, 0, 0); draw_viewmodel_box_tone(p, 0.34f, 0.74f, 0.44f, 1); glPopMatrix(); /* main grip */
    glPushMatrix(); glTranslatef(0.00f, -0.52f, -0.58f); glRotatef(-14.0f, 1, 0, 0); draw_viewmodel_box_tone(p, 0.38f, 0.20f, 0.30f, 1); glPopMatrix(); /* grip butt */

    /* Small accent to increase local density around the cylinder/frame break. */
    glPushMatrix(); glTranslatef(0.00f, 0.06f, 0.06f); draw_viewmodel_accent_strip(p, 0.52f, 0.04f, 0.34f); glPopMatrix();

    /* Optional hammer spur mass to emphasize the heavy revolver rear profile. */
    glPushMatrix(); glTranslatef(0.00f, 0.27f, -0.62f); draw_viewmodel_box_tone(p, 0.24f, 0.10f, 0.18f, 1); glPopMatrix();
}

static void draw_viewmodel_ar(const ViewmodelPalette *p) {
    glPushMatrix(); glTranslatef(0.00f, 0.12f, -0.16f); draw_viewmodel_box_tone(p, 0.90f, 0.31f, 1.38f, 0); glPopMatrix();
    glPushMatrix(); glTranslatef(0.00f, 0.08f, 1.06f); draw_viewmodel_box_tone(p, 0.46f, 0.24f, 1.46f, 1); glPopMatrix();
    glPushMatrix(); glTranslatef(0.00f, -0.18f, -0.38f); draw_viewmodel_box_tone(p, 0.58f, 0.34f, 1.02f, 1); glPopMatrix();
    glPushMatrix(); glTranslatef(0.00f, -0.38f, -0.56f); glRotatef(-8.0f, 1, 0, 0); draw_viewmodel_box_tone(p, 0.32f, 0.56f, 0.50f, 1); glPopMatrix();
    glPushMatrix(); glTranslatef(0.00f, -0.16f, 0.54f); draw_viewmodel_box_tone(p, 0.42f, 0.18f, 1.14f, 1); glPopMatrix();
    glPushMatrix(); glTranslatef(0.00f, 0.24f, 0.02f); draw_viewmodel_accent_strip(p, 0.58f, 0.05f, 0.58f); glPopMatrix();
}

static void draw_viewmodel_shotgun(const ViewmodelPalette *p) {
    glPushMatrix(); glTranslatef(0.00f, 0.12f, -0.18f); draw_viewmodel_box_tone(p, 1.04f, 0.34f, 1.44f, 0); glPopMatrix();
    glPushMatrix(); glTranslatef(0.00f, 0.16f, 0.94f); draw_viewmodel_box_tone(p, 0.22f, 0.18f, 2.00f, 0); glPopMatrix();
    glPushMatrix(); glTranslatef(0.00f, -0.04f, 0.90f); draw_viewmodel_box_tone(p, 0.18f, 0.14f, 1.86f, 1); glPopMatrix();
    glPushMatrix(); glTranslatef(0.00f, -0.22f, -0.16f); draw_viewmodel_box_tone(p, 0.78f, 0.42f, 1.18f, 1); glPopMatrix();
    glPushMatrix(); glTranslatef(0.00f, -0.34f, 0.48f); draw_viewmodel_box_tone(p, 0.56f, 0.28f, 0.96f, 1); glPopMatrix();
    glPushMatrix(); glTranslatef(0.00f, -0.32f, -0.84f); draw_viewmodel_box_tone(p, 0.88f, 0.30f, 0.66f, 1); glPopMatrix();
}

static void draw_viewmodel_sniper(const ViewmodelPalette *p) {
    glPushMatrix(); glTranslatef(0.00f, 0.02f, 0.46f); draw_viewmodel_box_tone(p, 0.54f, 0.20f, 4.40f, 0); glPopMatrix();
    glPushMatrix(); glTranslatef(0.00f, -0.14f, -0.56f); draw_viewmodel_box_tone(p, 0.60f, 0.30f, 1.36f, 1); glPopMatrix();
    glPushMatrix(); glTranslatef(0.00f, 0.30f, -0.02f); draw_viewmodel_box_tone(p, 0.40f, 0.24f, 1.76f, 1); glPopMatrix();
    glPushMatrix(); glTranslatef(0.00f, 0.34f, 0.56f); draw_viewmodel_box_tone(p, 0.26f, 0.22f, 0.88f, 1); glPopMatrix();
    glPushMatrix(); glTranslatef(0.00f, 0.00f, 2.08f); draw_viewmodel_box_tone(p, 0.18f, 0.10f, 1.04f, 1); glPopMatrix();
    glPushMatrix(); glTranslatef(0.00f, -0.22f, -1.02f); draw_viewmodel_box_tone(p, 0.72f, 0.28f, 0.82f, 1); glPopMatrix();
}

static void draw_viewmodel_knife_firstperson(void) {
    glScalef(0.86f, 0.86f, 0.86f);
    /* simple handle */
    glColor3f(0.16f, 0.14f, 0.12f);
    glPushMatrix(); glTranslatef(0.00f, -0.30f, -0.26f); draw_box(0.24f, 0.15f, 0.70f); glPopMatrix();
    /* pommel */
    glColor3f(0.10f, 0.10f, 0.11f);
    glPushMatrix(); glTranslatef(0.00f, -0.30f, -0.64f); draw_box(0.18f, 0.13f, 0.17f); glPopMatrix();
    /* guard nub */
    glColor3f(0.46f, 0.46f, 0.50f);
    glPushMatrix(); glTranslatef(0.00f, -0.20f, 0.08f); draw_box(0.32f, 0.06f, 0.16f); glPopMatrix();
    /* blade spine (thin) */
    glColor3f(0.68f, 0.72f, 0.78f);
    glPushMatrix(); glTranslatef(0.00f, -0.10f, 1.02f); glRotatef(-4.0f, 1, 0, 0); draw_box(0.065f, 0.038f, 1.76f); glPopMatrix();
    /* taper section */
    glColor3f(0.76f, 0.79f, 0.84f);
    glPushMatrix(); glTranslatef(0.00f, -0.075f, 1.90f); glRotatef(-8.0f, 1, 0, 0); draw_box(0.040f, 0.028f, 0.56f); glPopMatrix();
    /* point tip */
    glColor3f(0.84f, 0.86f, 0.90f);
    glPushMatrix(); glTranslatef(0.00f, -0.045f, 2.22f); glRotatef(-20.0f, 1, 0, 0); draw_box(0.022f, 0.020f, 0.30f); glPopMatrix();
}

static void draw_viewmodel_weapon(int weapon_id) {
    if (weapon_id == WPN_KNIFE) {
        draw_viewmodel_knife_firstperson();
        return;
    }
    if (weapon_id == WPN_KATANA) {
        draw_gun_model(weapon_id);
        return;
    }
    glScalef(0.9f, 0.9f, 0.9f);
    ViewmodelPalette p = viewmodel_palette_for_weapon(weapon_id);
    switch (weapon_id) {
        case WPN_MAGNUM: draw_viewmodel_magnum(&p); break;
        case WPN_AR: draw_viewmodel_ar(&p); break;
        case WPN_SHOTGUN: draw_viewmodel_shotgun(&p); break;
        case WPN_SNIPER: draw_viewmodel_sniper(&p); break;
        case WPN_KNIFE: draw_viewmodel_knife_firstperson(); break;
        default: draw_viewmodel_magnum(&p); break;
    }
}

static void draw_viewmodel_flag_carry(int carried_flag_team_id) {
    float cloth_r = 0.82f, cloth_g = 0.15f, cloth_b = 0.14f;
    if (carried_flag_team_id == 1) {
        cloth_r = 0.16f; cloth_g = 0.34f; cloth_b = 0.86f;
    }

    glScalef(0.86f, 0.86f, 0.86f);

    glColor3f(0.42f, 0.30f, 0.22f);
    glPushMatrix(); glTranslatef(-0.42f, -0.24f, -0.02f); glRotatef(-14.0f, 1, 0, 0); draw_box(0.24f, 0.25f, 0.96f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.18f, -0.30f, -0.16f); glRotatef(-10.0f, 1, 0, 0); draw_box(0.24f, 0.24f, 0.88f); glPopMatrix();

    glColor3f(0.10f, 0.11f, 0.13f);
    glPushMatrix(); glTranslatef(-0.44f, -0.35f, 0.38f); draw_box(0.27f, 0.16f, 0.30f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.14f, -0.38f, 0.28f); draw_box(0.26f, 0.17f, 0.30f); glPopMatrix();

    glPushMatrix();
    glTranslatef(-0.12f, -0.10f, 0.18f);
    glRotatef(24.0f, 0, 0, 1);
    glRotatef(-15.0f, 1, 0, 0);
    glColor3f(0.16f, 0.14f, 0.12f);
    glPushMatrix(); glTranslatef(0.02f, 0.02f, 0.60f); draw_box(0.09f, 0.09f, 1.78f); glPopMatrix();
    glColor3f(cloth_r, cloth_g, cloth_b);
    glPushMatrix(); glTranslatef(-0.26f, 0.46f, 1.12f); glRotatef(8.0f, 0, 1, 0); draw_box(0.70f, 0.42f, 0.12f); glPopMatrix();
    glColor3f(cloth_r * 0.85f, cloth_g * 0.85f, cloth_b * 0.85f);
    glPushMatrix(); glTranslatef(-0.20f, 0.31f, 1.08f); draw_box(0.58f, 0.08f, 0.08f); glPopMatrix();
    glPopMatrix();
}

static void draw_weapon_muzzle_flash(int weapon_id, float intensity) {
    if (weapon_id == WPN_KNIFE || weapon_id == WPN_KATANA || intensity <= 0.01f) return;
    float size = 0.28f;
    float length = 0.56f;
    switch (weapon_id) {
        case WPN_MAGNUM:  size = 0.32f; length = 0.62f; break;
        case WPN_AR:      size = 0.22f; length = 0.72f; break;
        case WPN_SHOTGUN: size = 0.44f; length = 0.80f; break;
        case WPN_SNIPER:  size = 0.19f; length = 0.86f; break;
        default: break;
    }
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_LINE_BIT);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glTranslatef(0.0f, 0.08f, 1.95f);

    float alpha_core = clamp01f(intensity) * 0.80f;
    glColor4f(1.0f, 0.95f, 0.74f, alpha_core);
    glBegin(GL_QUADS);
    glVertex3f(-size, 0.0f, 0.0f); glVertex3f(size, 0.0f, 0.0f); glVertex3f(size * 0.30f, 0.0f, length); glVertex3f(-size * 0.30f, 0.0f, length);
    glVertex3f(0.0f, -size, 0.0f); glVertex3f(0.0f, size, 0.0f); glVertex3f(0.0f, size * 0.30f, length); glVertex3f(0.0f, -size * 0.30f, length);
    glEnd();

    glColor4f(1.0f, 0.62f, 0.18f, alpha_core * 0.8f);
    glBegin(GL_TRIANGLES);
    glVertex3f(0.0f, 0.0f, length + size * 0.24f);
    glVertex3f(size * 0.28f, size * 0.18f, length * 0.52f);
    glVertex3f(-size * 0.28f, -size * 0.18f, length * 0.52f);
    glEnd();
    glPopAttrib();
}

static void draw_box(float w, float h, float d) {
    glPushMatrix();
    glScalef(w, h, d);
    glBegin(GL_QUADS);
    // Front
    glVertex3f(-0.5f,-0.5f,0.5f); glVertex3f(0.5f,-0.5f,0.5f); glVertex3f(0.5f,0.5f,0.5f); glVertex3f(-0.5f,0.5f,0.5f);
    // Back
    glVertex3f(-0.5f,-0.5f,-0.5f); glVertex3f(-0.5f,0.5f,-0.5f); glVertex3f(0.5f,0.5f,-0.5f); glVertex3f(0.5f,-0.5f,-0.5f);
    // Left
    glVertex3f(-0.5f,-0.5f,-0.5f); glVertex3f(-0.5f,-0.5f,0.5f); glVertex3f(-0.5f,0.5f,0.5f); glVertex3f(-0.5f,0.5f,-0.5f);
    // Right
    glVertex3f(0.5f,-0.5f,-0.5f); glVertex3f(0.5f,0.5f,-0.5f); glVertex3f(0.5f,0.5f,0.5f); glVertex3f(0.5f,-0.5f,0.5f);
    // Top
    glVertex3f(-0.5f,0.5f,0.5f); glVertex3f(0.5f,0.5f,0.5f); glVertex3f(0.5f,0.5f,-0.5f); glVertex3f(-0.5f,0.5f,-0.5f);
    // Bottom
    glVertex3f(-0.5f,-0.5f,0.5f); glVertex3f(-0.5f,-0.5f,-0.5f); glVertex3f(0.5f,-0.5f,-0.5f); glVertex3f(0.5f,-0.5f,0.5f);
    glEnd();
    glPopMatrix();
}

static void draw_box_outline(float w, float h, float d) {
    glPushMatrix();
    glScalef(w, h, d);
    glLineWidth(vs0_art_direction_enabled ? 1.2f : 2.0f);
    if (vs0_art_direction_enabled) glColor3f(0.14f, 0.17f, 0.21f);
    else glColor3f(1.0f, 1.0f, 0.0f);

    glBegin(GL_LINE_LOOP);
    glVertex3f(-0.5f, 0.5f, 0.5f); glVertex3f(0.5f, 0.5f, 0.5f);
    glVertex3f(0.5f, -0.5f, 0.5f); glVertex3f(-0.5f, -0.5f, 0.5f);
    glEnd();

    glBegin(GL_LINE_LOOP);
    glVertex3f(-0.5f, 0.5f, -0.5f); glVertex3f(0.5f, 0.5f, -0.5f);
    glVertex3f(0.5f, -0.5f, -0.5f); glVertex3f(-0.5f, -0.5f, -0.5f);
    glEnd();

    glBegin(GL_LINES);
    glVertex3f(-0.5f, 0.5f, 0.5f); glVertex3f(-0.5f, 0.5f, -0.5f);
    glVertex3f(0.5f, 0.5f, 0.5f); glVertex3f(0.5f, 0.5f, -0.5f);
    glVertex3f(0.5f, -0.5f, 0.5f); glVertex3f(0.5f, -0.5f, -0.5f);
    glVertex3f(-0.5f, -0.5f, 0.5f); glVertex3f(-0.5f, -0.5f, -0.5f);
    glEnd();

    glPopMatrix();
}

typedef struct PlayerAnimPose {
    float locomotion;
    float torso_bob;
    float torso_yaw;
    float left_thigh;
    float right_thigh;
    float left_knee;
    float right_knee;
    float left_arm_swing;
    float right_arm_swing;
    float left_elbow;
    float right_elbow;
} PlayerAnimPose;

static float g_player_run_phase[MAX_CLIENTS];
static unsigned int g_player_anim_last_ms[MAX_CLIENTS];

static PlayerAnimPose compute_player_anim_pose(const PlayerState *p) {
    PlayerAnimPose pose;
    memset(&pose, 0, sizeof(pose));
    if (!p) return pose;

    int anim_idx = p->id;
    if (anim_idx < 0 || anim_idx >= MAX_CLIENTS) anim_idx = 0;
    unsigned int now_ms = SDL_GetTicks();
    unsigned int prev_ms = g_player_anim_last_ms[anim_idx];
    float dt = (prev_ms > 0 && now_ms > prev_ms) ? (float)(now_ms - prev_ms) * 0.001f : (1.0f / 60.0f);
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.06f) dt = 0.06f;
    g_player_anim_last_ms[anim_idx] = now_ms;

    float speed = sqrtf(p->vx * p->vx + p->vz * p->vz);
    const float run_speed = 15.0f;
    pose.locomotion = clamp01f(speed / run_speed);

    if (p->state != STATE_DEAD && p->on_ground && pose.locomotion > 0.03f) {
        float cycle_hz = lerpf(1.0f, 2.9f, pose.locomotion);
        g_player_run_phase[anim_idx] += dt * (cycle_hz * 6.2831853f);
        if (g_player_run_phase[anim_idx] > 6.2831853f) g_player_run_phase[anim_idx] -= 6.2831853f;
    }

    float phase = g_player_run_phase[anim_idx];
    float leg = sinf(phase);
    float opp_leg = sinf(phase + 3.1415926f);
    float knee_l = fmaxf(0.0f, sinf(phase + 0.7f));
    float knee_r = fmaxf(0.0f, sinf(phase + 3.1415926f + 0.7f));
    float idle_sway = sinf(now_ms * 0.0022f) * (1.0f - pose.locomotion);

    float airborne = p->on_ground ? 0.0f : 1.0f;
    float locomotion_scale = (1.0f - 0.7f * airborne) * pose.locomotion;

    pose.left_thigh = -leg * (12.0f + locomotion_scale * 16.0f);
    pose.right_thigh = -opp_leg * (12.0f + locomotion_scale * 16.0f);
    pose.left_knee = knee_l * (8.0f + locomotion_scale * 16.0f);
    pose.right_knee = knee_r * (8.0f + locomotion_scale * 16.0f);
    pose.left_arm_swing = opp_leg * (2.0f + locomotion_scale * 8.0f) + idle_sway * 2.0f;
    pose.right_arm_swing = leg * (1.5f + locomotion_scale * 5.0f);
    pose.left_elbow = 12.0f + locomotion_scale * 10.0f;
    pose.right_elbow = 32.0f + locomotion_scale * 8.0f;
    pose.torso_bob = fabsf(leg) * 0.05f * locomotion_scale;
    pose.torso_yaw = leg * (1.0f + locomotion_scale * 2.0f);
    if (p->crouching) pose.torso_bob -= 0.10f;
    return pose;
}

static void draw_player_arm(int right_side, const PlayerAnimPose *pose, float draw_pitch, float draw_recoil, int weapon_id) {
    float side = right_side ? 1.0f : -1.0f;
    float shoulder_x = side * RONIN_SLEEVE_OFFSET;
    float shoulder_y = 1.20f;
    float shoulder_z = right_side ? 0.08f : -0.02f;
    float upper_pitch = right_side ? (28.0f + pose->right_arm_swing) : (-6.0f + pose->left_arm_swing);
    float elbow_pitch = right_side ? pose->right_elbow : pose->left_elbow;

    glPushMatrix();
    glTranslatef(shoulder_x, shoulder_y, shoulder_z);
    glRotatef(upper_pitch, 1, 0, 0);
    glRotatef(-side * 8.0f, 0, 0, 1);
    draw_box(RONIN_ARM_UPPER_W, RONIN_ARM_UPPER_H, RONIN_ARM_UPPER_D);
    draw_box_outline(RONIN_ARM_UPPER_W, RONIN_ARM_UPPER_H, RONIN_ARM_UPPER_D);

    glTranslatef(0.0f, -RONIN_ARM_UPPER_H * 0.5f, 0.0f);
    glRotatef(elbow_pitch, 1, 0, 0);
    draw_box(RONIN_ARM_LOWER_W, RONIN_ARM_LOWER_H, RONIN_ARM_LOWER_D);
    draw_box_outline(RONIN_ARM_LOWER_W, RONIN_ARM_LOWER_H, RONIN_ARM_LOWER_D);

    glTranslatef(0.0f, -RONIN_ARM_LOWER_H * 0.5f, 0.08f);
    draw_box(RONIN_HAND_W, RONIN_HAND_H, RONIN_HAND_D);
    draw_box_outline(RONIN_HAND_W, RONIN_HAND_H, RONIN_HAND_D);

    if (right_side) {
        glPushMatrix();
        glTranslatef(0.12f, 0.02f, 0.28f);
        glRotatef(draw_pitch, 1, 0, 0);
        glRotatef(-draw_recoil * 10.0f, 1, 0, 0);
        glTranslatef(0.0f, 0.0f, -draw_recoil * 0.08f);
        glScalef(0.8f, 0.8f, 0.8f);
        draw_gun_model(weapon_id);
        glPopMatrix();
    }
    glPopMatrix();
}

static void draw_player_leg(int right_side, const PlayerAnimPose *pose) {
    float side = right_side ? 1.0f : -1.0f;
    float hip_x = side * RONIN_PANTS_OFFSET;
    float hip_y = 0.30f;
    float hip_pitch = right_side ? pose->right_thigh : pose->left_thigh;
    float knee_pitch = right_side ? pose->right_knee : pose->left_knee;

    glPushMatrix();
    glTranslatef(hip_x, hip_y, 0.0f);
    glRotatef(hip_pitch, 1, 0, 0);
    draw_box(RONIN_LEG_UPPER_W, RONIN_LEG_UPPER_H, RONIN_LEG_UPPER_D);
    draw_box_outline(RONIN_LEG_UPPER_W, RONIN_LEG_UPPER_H, RONIN_LEG_UPPER_D);

    glTranslatef(0.0f, -RONIN_LEG_UPPER_H * 0.5f, 0.0f);
    glRotatef(knee_pitch, 1, 0, 0);
    draw_box(RONIN_LEG_LOWER_W, RONIN_LEG_LOWER_H, RONIN_LEG_LOWER_D);
    draw_box_outline(RONIN_LEG_LOWER_W, RONIN_LEG_LOWER_H, RONIN_LEG_LOWER_D);

    glTranslatef(0.0f, -RONIN_LEG_LOWER_H * 0.5f, 0.15f);
    draw_box(RONIN_FOOT_W, RONIN_FOOT_H, RONIN_FOOT_D);
    draw_box_outline(RONIN_FOOT_W, RONIN_FOOT_H, RONIN_FOOT_D);
    glPopMatrix();
}

static void draw_ronin_shell_articulated(const PlayerAnimPose *pose, float draw_pitch, float draw_recoil, int weapon_id) {
    if (vs0_art_direction_enabled) glColor3f(0.18f, 0.20f, 0.24f);
    else glColor3f(0.1f, 0.1f, 0.1f);

    glPushMatrix();
    glTranslatef(0.0f, 0.9f + pose->torso_bob, 0.0f);
    glRotatef(pose->torso_yaw, 0, 1, 0);
    draw_box(RONIN_TORSO_W, RONIN_TORSO_H, RONIN_TORSO_D);
    draw_box_outline(RONIN_TORSO_W, RONIN_TORSO_H, RONIN_TORSO_D);
    glPushMatrix(); glTranslatef(-RONIN_SHOULDER_PAD_OFFSET, 0.35f, 0.0f); draw_box(RONIN_SHOULDER_PAD_W, RONIN_SHOULDER_PAD_H, RONIN_SHOULDER_PAD_D); draw_box_outline(RONIN_SHOULDER_PAD_W, RONIN_SHOULDER_PAD_H, RONIN_SHOULDER_PAD_D); glPopMatrix();
    glPushMatrix(); glTranslatef(RONIN_SHOULDER_PAD_OFFSET, 0.35f, 0.0f); draw_box(RONIN_SHOULDER_PAD_W, RONIN_SHOULDER_PAD_H, RONIN_SHOULDER_PAD_D); draw_box_outline(RONIN_SHOULDER_PAD_W, RONIN_SHOULDER_PAD_H, RONIN_SHOULDER_PAD_D); glPopMatrix();

    draw_player_arm(0, pose, draw_pitch, draw_recoil, weapon_id);
    draw_player_arm(1, pose, draw_pitch, draw_recoil, weapon_id);

    if (vs0_art_direction_enabled) glColor3f(0.42f, 0.17f, 0.17f);
    else glColor3f(0.6f, 0.0f, 0.0f);
    glBegin(GL_QUADS);
    glVertex3f(-0.68f, RONIN_LINING_Y_BOTTOM, 0.39f); glVertex3f(0.68f, RONIN_LINING_Y_BOTTOM, 0.39f);
    glVertex3f(0.68f, RONIN_LINING_Y_TOP, 0.39f); glVertex3f(-0.68f, RONIN_LINING_Y_TOP, 0.39f);
    glEnd();
    glPopMatrix();

    if (vs0_art_direction_enabled) glColor3f(0.26f, 0.28f, 0.31f);
    else glColor3f(0.18f, 0.18f, 0.2f);
    draw_player_leg(0, pose);
    draw_player_leg(1, pose);
}

static void draw_ronin_shell(void) {
    // Jacket core (cropped waist, broad shoulders)
    if (vs0_art_direction_enabled) glColor3f(0.18f, 0.20f, 0.24f);
    else glColor3f(0.1f, 0.1f, 0.1f);
    glPushMatrix();
    glTranslatef(0.0f, 0.9f, 0.0f);
    draw_box(RONIN_TORSO_W, RONIN_TORSO_H, RONIN_TORSO_D);
    draw_box_outline(RONIN_TORSO_W, RONIN_TORSO_H, RONIN_TORSO_D);
    // Shoulder pads
    glPushMatrix(); glTranslatef(-RONIN_SHOULDER_PAD_OFFSET, 0.35f, 0.0f); draw_box(RONIN_SHOULDER_PAD_W, RONIN_SHOULDER_PAD_H, RONIN_SHOULDER_PAD_D); draw_box_outline(RONIN_SHOULDER_PAD_W, RONIN_SHOULDER_PAD_H, RONIN_SHOULDER_PAD_D); glPopMatrix();
    glPushMatrix(); glTranslatef(RONIN_SHOULDER_PAD_OFFSET, 0.35f, 0.0f); draw_box(RONIN_SHOULDER_PAD_W, RONIN_SHOULDER_PAD_H, RONIN_SHOULDER_PAD_D); draw_box_outline(RONIN_SHOULDER_PAD_W, RONIN_SHOULDER_PAD_H, RONIN_SHOULDER_PAD_D); glPopMatrix();
    // Sleeves
    glPushMatrix(); glTranslatef(-RONIN_SLEEVE_OFFSET, -0.25f, 0.0f); draw_box(RONIN_SLEEVE_W, RONIN_SLEEVE_H, RONIN_SLEEVE_D); draw_box_outline(RONIN_SLEEVE_W, RONIN_SLEEVE_H, RONIN_SLEEVE_D); glPopMatrix();
    glPushMatrix(); glTranslatef(RONIN_SLEEVE_OFFSET, -0.25f, 0.0f); draw_box(RONIN_SLEEVE_W, RONIN_SLEEVE_H, RONIN_SLEEVE_D); draw_box_outline(RONIN_SLEEVE_W, RONIN_SLEEVE_H, RONIN_SLEEVE_D); glPopMatrix();
    // Red satin lining at hem
    if (vs0_art_direction_enabled) glColor3f(0.42f, 0.17f, 0.17f);
    else glColor3f(0.6f, 0.0f, 0.0f);
    glBegin(GL_QUADS);
    glVertex3f(-0.68f, RONIN_LINING_Y_BOTTOM, 0.39f); glVertex3f(0.68f, RONIN_LINING_Y_BOTTOM, 0.39f);
    glVertex3f(0.68f, RONIN_LINING_Y_TOP, 0.39f); glVertex3f(-0.68f, RONIN_LINING_Y_TOP, 0.39f);
    glEnd();
    glPopMatrix();

    // Tech cargo pants (baggy)
    if (vs0_art_direction_enabled) glColor3f(0.26f, 0.28f, 0.31f);
    else glColor3f(0.18f, 0.18f, 0.2f);
    glPushMatrix(); glTranslatef(-RONIN_PANTS_OFFSET, 0.0f, 0.0f); draw_box(RONIN_PANTS_W, RONIN_PANTS_H, RONIN_PANTS_D); draw_box_outline(RONIN_PANTS_W, RONIN_PANTS_H, RONIN_PANTS_D); glPopMatrix();
    glPushMatrix(); glTranslatef(RONIN_PANTS_OFFSET, 0.0f, 0.0f); draw_box(RONIN_PANTS_W, RONIN_PANTS_H, RONIN_PANTS_D); draw_box_outline(RONIN_PANTS_W, RONIN_PANTS_H, RONIN_PANTS_D); glPopMatrix();
}

static void draw_storm_mask(void) {
    // Head base
    if (vs0_art_direction_enabled) glColor3f(0.20f, 0.22f, 0.25f);
    else glColor3f(0.06f, 0.06f, 0.06f);
    draw_box(RONIN_HEAD_W, RONIN_HEAD_H, RONIN_HEAD_D);
    draw_box_outline(RONIN_HEAD_W, RONIN_HEAD_H, RONIN_HEAD_D);
    // Faceplate
    if (vs0_art_direction_enabled) glColor3f(0.30f, 0.32f, 0.36f);
    else glColor3f(0.2f, 0.2f, 0.22f);
    glPushMatrix(); glTranslatef(0.0f, -0.05f, 0.37f); draw_box(RONIN_FACEPLATE_W, RONIN_FACEPLATE_H, RONIN_FACEPLATE_D); draw_box_outline(RONIN_FACEPLATE_W, RONIN_FACEPLATE_H, RONIN_FACEPLATE_D); glPopMatrix();
    // Cyan vents
    if (vs0_art_direction_enabled) glColor3f(0.34f, 0.82f, 0.86f);
    else glColor3f(0.0f, 1.0f, 1.0f);
    glPushMatrix(); glTranslatef(RONIN_VENT_OFFSET_X, -0.08f, 0.42f); draw_box(RONIN_VENT_W, RONIN_VENT_H, RONIN_VENT_D); draw_box_outline(RONIN_VENT_W, RONIN_VENT_H, RONIN_VENT_D); glPopMatrix();
    glPushMatrix(); glTranslatef(-RONIN_VENT_OFFSET_X, -0.08f, 0.42f); draw_box(RONIN_VENT_W, RONIN_VENT_H, RONIN_VENT_D); draw_box_outline(RONIN_VENT_W, RONIN_VENT_H, RONIN_VENT_D); glPopMatrix();
    // Broken horn silhouette (single jagged horn)
    if (vs0_art_direction_enabled) glColor3f(0.16f, 0.18f, 0.21f);
    else glColor3f(0.08f, 0.08f, 0.08f);
    glPushMatrix(); glTranslatef(RONIN_HORN_OFFSET_X, 0.52f, 0.05f); draw_box(RONIN_HORN_W, RONIN_HORN_H, RONIN_HORN_D); draw_box_outline(RONIN_HORN_W, RONIN_HORN_H, RONIN_HORN_D); glPopMatrix();
}

static void draw_mayrice_beard(void) {
    glColor3f(0.30f, 0.21f, 0.14f);
    glPushMatrix(); glTranslatef(0.0f, -0.14f, 0.38f); draw_box(0.62f, 0.32f, 0.10f); draw_box_outline(0.62f, 0.32f, 0.10f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.0f, -0.27f, 0.33f); draw_box(0.34f, 0.12f, 0.12f); draw_box_outline(0.34f, 0.12f, 0.12f); glPopMatrix();
}

static void draw_mayrice_hair(void) {
    glColor3f(0.43f, 0.30f, 0.19f);
    glPushMatrix(); glTranslatef(0.0f, 0.34f, -0.04f); draw_box(0.95f, 0.30f, 0.84f); draw_box_outline(0.95f, 0.30f, 0.84f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.0f, 0.12f, -0.27f); draw_box(0.86f, 0.26f, 0.34f); draw_box_outline(0.86f, 0.26f, 0.34f); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.54f, 0.00f, 0.03f); glRotatef(28.0f, 0, 0, 1); draw_box(0.22f, 0.76f, 0.24f); draw_box_outline(0.22f, 0.76f, 0.24f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.54f, 0.00f, 0.03f); glRotatef(-28.0f, 0, 0, 1); draw_box(0.22f, 0.76f, 0.24f); draw_box_outline(0.22f, 0.76f, 0.24f); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.30f, 0.37f, 0.34f); glRotatef(-26.0f, 0, 0, 1); draw_box(0.26f, 0.22f, 0.12f); draw_box_outline(0.26f, 0.22f, 0.12f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.30f, 0.37f, 0.34f); glRotatef(26.0f, 0, 0, 1); draw_box(0.26f, 0.22f, 0.12f); draw_box_outline(0.26f, 0.22f, 0.12f); glPopMatrix();
}

static void draw_mayrice_head(void) {
    glColor3f(0.95f, 0.86f, 0.76f);
    draw_box(0.78f, 0.86f, 0.72f);
    draw_box_outline(0.78f, 0.86f, 0.72f);

    glColor3f(0.33f, 0.21f, 0.15f);
    glPushMatrix(); glTranslatef(-0.18f, 0.08f, 0.38f); draw_box(0.16f, 0.04f, 0.06f); draw_box_outline(0.16f, 0.04f, 0.06f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.18f, 0.08f, 0.38f); draw_box(0.16f, 0.04f, 0.06f); draw_box_outline(0.16f, 0.04f, 0.06f); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.18f, -0.03f, 0.39f); draw_box(0.05f, 0.16f, 0.05f); draw_box_outline(0.05f, 0.16f, 0.05f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.18f, -0.03f, 0.39f); draw_box(0.05f, 0.16f, 0.05f); draw_box_outline(0.05f, 0.16f, 0.05f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.0f, -0.02f, 0.40f); draw_box(0.05f, 0.22f, 0.05f); draw_box_outline(0.05f, 0.22f, 0.05f); glPopMatrix();
    draw_mayrice_beard();
    draw_mayrice_hair();
}

static void draw_mayrice_body(void) {
    glColor3f(0.24f, 0.50f, 0.78f);
    glPushMatrix();
    glTranslatef(0.0f, 0.88f, 0.0f);
    draw_box(1.18f, 1.52f, 0.72f);
    draw_box_outline(1.18f, 1.52f, 0.72f);
    glPopMatrix();

    glColor3f(0.20f, 0.44f, 0.70f);
    glPushMatrix(); glTranslatef(-0.72f, 0.88f, 0.0f); draw_box(0.30f, 1.28f, 0.36f); draw_box_outline(0.30f, 1.28f, 0.36f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.72f, 0.88f, 0.0f); draw_box(0.30f, 1.28f, 0.36f); draw_box_outline(0.30f, 1.28f, 0.36f); glPopMatrix();

    glColor3f(0.94f, 0.86f, 0.77f);
    glPushMatrix(); glTranslatef(-0.72f, 0.16f, 0.12f); draw_box(0.26f, 0.24f, 0.20f); draw_box_outline(0.26f, 0.24f, 0.20f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.72f, 0.16f, 0.12f); draw_box(0.26f, 0.24f, 0.20f); draw_box_outline(0.26f, 0.24f, 0.20f); glPopMatrix();

    glColor3f(0.58f, 0.44f, 0.27f);
    glPushMatrix(); glTranslatef(-0.24f, -0.06f, 0.0f); draw_box(0.42f, 1.44f, 0.42f); draw_box_outline(0.42f, 1.44f, 0.42f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.24f, -0.06f, 0.0f); draw_box(0.42f, 1.44f, 0.42f); draw_box_outline(0.42f, 1.44f, 0.42f); glPopMatrix();
}

static void draw_cyborg_hair(void) {
    glColor3f(0.08f, 0.08f, 0.10f);
    glPushMatrix(); glTranslatef(0.00f, 0.30f, -0.04f); draw_box(0.86f, 0.34f, 0.72f); draw_box_outline(0.86f, 0.34f, 0.72f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.00f, 0.52f, -0.12f); draw_box(0.62f, 0.26f, 0.54f); draw_box_outline(0.62f, 0.26f, 0.54f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.06f, 0.72f, -0.20f); draw_box(0.34f, 0.20f, 0.34f); draw_box_outline(0.34f, 0.20f, 0.34f); glPopMatrix();
}

static void draw_cyborg_faceplate(void) {
    glColor3f(0.17f, 0.19f, 0.23f);
    glPushMatrix(); glTranslatef(-0.18f, 0.02f, 0.39f); draw_box(0.40f, 0.66f, 0.08f); draw_box_outline(0.40f, 0.66f, 0.08f); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.02f, 0.23f, 0.39f); draw_box(0.07f, 0.16f, 0.08f); draw_box_outline(0.07f, 0.16f, 0.08f); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.13f, 0.22f, 0.40f); draw_box(0.14f, 0.06f, 0.05f); draw_box_outline(0.14f, 0.06f, 0.05f); glPopMatrix();

    glColor3f(0.98f, 0.10f, 0.08f);
    glPushMatrix(); glTranslatef(-0.19f, 0.07f, 0.43f); draw_box(0.11f, 0.11f, 0.03f); glPopMatrix();

    glColor3f(0.42f, 0.06f, 0.08f);
    glPushMatrix(); glTranslatef(-0.19f, 0.07f, 0.41f); draw_box(0.12f, 0.12f, 0.02f); glPopMatrix();
}

static void draw_cyborg_head(void) {
    glColor3f(0.76f, 0.81f, 0.74f);
    glPushMatrix(); glTranslatef(0.14f, 0.00f, 0.00f); draw_box(0.48f, 0.88f, 0.72f); draw_box_outline(0.48f, 0.88f, 0.72f); glPopMatrix();

    glColor3f(0.24f, 0.27f, 0.31f);
    glPushMatrix(); glTranslatef(-0.14f, 0.00f, -0.02f); draw_box(0.50f, 0.88f, 0.72f); draw_box_outline(0.50f, 0.88f, 0.72f); glPopMatrix();

    glColor3f(0.22f, 0.25f, 0.28f);
    glPushMatrix(); glTranslatef(0.00f, -0.28f, 0.06f); draw_box(0.70f, 0.22f, 0.56f); draw_box_outline(0.70f, 0.22f, 0.56f); glPopMatrix();

    glColor3f(0.32f, 0.21f, 0.24f);
    glPushMatrix(); glTranslatef(0.18f, 0.10f, 0.38f); draw_box(0.14f, 0.04f, 0.06f); draw_box_outline(0.14f, 0.04f, 0.06f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.18f, -0.03f, 0.39f); draw_box(0.04f, 0.12f, 0.05f); draw_box_outline(0.04f, 0.12f, 0.05f); glPopMatrix();

    draw_cyborg_faceplate();
    draw_cyborg_hair();
}

static void draw_cyborg_cape(void) {
    glColor3f(0.33f, 0.12f, 0.43f);
    glPushMatrix(); glTranslatef(-0.40f, 0.98f, -0.28f); glRotatef(6.0f, 0, 1, 0); draw_box(0.50f, 1.26f, 0.24f); draw_box_outline(0.50f, 1.26f, 0.24f); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.22f, 1.22f, -0.26f); draw_box(0.62f, 0.52f, 0.20f); draw_box_outline(0.62f, 0.52f, 0.20f); glPopMatrix();

    glColor3f(0.19f, 0.07f, 0.25f);
    glPushMatrix(); glTranslatef(-0.38f, 0.90f, -0.38f); glRotatef(6.0f, 0, 1, 0); draw_box(0.42f, 1.04f, 0.14f); draw_box_outline(0.42f, 1.04f, 0.14f); glPopMatrix();
}

static void draw_cyborg_torso(void) {
    glColor3f(0.16f, 0.18f, 0.22f);
    glPushMatrix(); glTranslatef(0.0f, 0.88f, 0.0f); draw_box(1.18f, 1.52f, 0.72f); draw_box_outline(1.18f, 1.52f, 0.72f); glPopMatrix();

    glColor3f(0.12f, 0.13f, 0.16f);
    glPushMatrix(); glTranslatef(0.0f, 1.28f, 0.20f); draw_box(0.84f, 0.38f, 0.26f); draw_box_outline(0.84f, 0.38f, 0.26f); glPopMatrix();

    glColor3f(0.30f, 0.08f, 0.38f);
    glPushMatrix(); glTranslatef(0.36f, 1.14f, 0.28f); draw_box(0.20f, 0.56f, 0.18f); draw_box_outline(0.20f, 0.56f, 0.18f); glPopMatrix();

    glColor3f(0.28f, 0.42f, 0.50f);
    glPushMatrix(); glTranslatef(0.00f, 1.31f, 0.35f); draw_box(0.12f, 0.12f, 0.03f); draw_box_outline(0.12f, 0.12f, 0.03f); glPopMatrix();
}

static void draw_cyborg_arm_left(void) {
    glColor3f(0.14f, 0.16f, 0.20f);
    glPushMatrix(); glTranslatef(-0.72f, 0.88f, 0.0f); draw_box(0.30f, 1.28f, 0.36f); draw_box_outline(0.30f, 1.28f, 0.36f); glPopMatrix();
    glColor3f(0.15f, 0.17f, 0.23f);
    glPushMatrix(); glTranslatef(-0.72f, 0.16f, 0.12f); draw_box(0.26f, 0.24f, 0.20f); draw_box_outline(0.26f, 0.24f, 0.20f); glPopMatrix();
}

static void draw_cyborg_arm_right(void) {
    glColor3f(0.20f, 0.22f, 0.26f);
    glPushMatrix(); glTranslatef(0.72f, 0.88f, 0.0f); draw_box(0.30f, 1.28f, 0.36f); draw_box_outline(0.30f, 1.28f, 0.36f); glPopMatrix();
    glColor3f(0.78f, 0.82f, 0.74f);
    glPushMatrix(); glTranslatef(0.72f, 0.16f, 0.12f); draw_box(0.26f, 0.24f, 0.20f); draw_box_outline(0.26f, 0.24f, 0.20f); glPopMatrix();
}

static void draw_cyborg_leg_left(void) {
    glColor3f(0.13f, 0.14f, 0.17f);
    glPushMatrix(); glTranslatef(-0.24f, -0.06f, 0.0f); draw_box(0.42f, 1.44f, 0.42f); draw_box_outline(0.42f, 1.44f, 0.42f); glPopMatrix();
}

static void draw_cyborg_leg_right(void) {
    glColor3f(0.18f, 0.19f, 0.22f);
    glPushMatrix(); glTranslatef(0.24f, -0.06f, 0.0f); draw_box(0.42f, 1.44f, 0.42f); draw_box_outline(0.42f, 1.44f, 0.42f); glPopMatrix();
}

static void draw_player_skin_cyborg(PlayerState *p, float draw_pitch, float draw_recoil) {
    draw_cyborg_cape();
    draw_cyborg_torso();
    draw_cyborg_arm_left();
    draw_cyborg_arm_right();
    draw_cyborg_leg_left();
    draw_cyborg_leg_right();

    glPushMatrix();
    glTranslatef(0.0f, 1.95f, 0.04f);
    glRotatef(draw_pitch, 1, 0, 0);
    draw_cyborg_head();
    glPopMatrix();

    glPushMatrix(); glTranslatef(0.64f, 1.05f, 0.57f);
    glRotatef(draw_pitch, 1, 0, 0);
    glRotatef(-draw_recoil * 10.0f, 1, 0, 0);
    glTranslatef(0.0f, 0.0f, -draw_recoil * 0.08f);
    glScalef(0.8f, 0.8f, 0.8f); draw_gun_model(p->current_weapon); glPopMatrix();
}

static void draw_player_skin_bat(PlayerState *p, float draw_pitch, float draw_recoil) {
    PlayerAnimPose pose = compute_player_anim_pose(p);
    draw_ronin_shell_articulated(&pose, draw_pitch, draw_recoil, p->current_weapon);
    glPushMatrix();
    glTranslatef(0.0f, 1.85f + pose.torso_bob, 0.0f);
    glRotatef(pose.torso_yaw * 0.35f, 0, 1, 0);
    glRotatef(draw_pitch, 1, 0, 0);
    draw_storm_mask();
    glPopMatrix();
}

static void draw_player_skin_mayrice(PlayerState *p, float draw_pitch, float draw_recoil) {
    draw_mayrice_body();
    glPushMatrix();
    glTranslatef(0.0f, 1.95f, 0.04f);
    glRotatef(draw_pitch, 1, 0, 0);
    draw_mayrice_head();
    glPopMatrix();

    glPushMatrix(); glTranslatef(0.64f, 1.05f, 0.57f);
    glRotatef(draw_pitch, 1, 0, 0);
    glRotatef(-draw_recoil * 10.0f, 1, 0, 0);
    glTranslatef(0.0f, 0.0f, -draw_recoil * 0.08f);
    glScalef(0.8f, 0.8f, 0.8f); draw_gun_model(p->current_weapon); glPopMatrix();
}

static void draw_player_skin_pirate(PlayerState *p, float draw_pitch, float draw_recoil) {
    glColor3f(0.70f, 0.10f, 0.08f);
    glPushMatrix(); glTranslatef(0.0f, 0.80f, 0.0f); draw_box(1.25f, 1.40f, 0.70f); draw_box_outline(1.25f, 1.40f, 0.70f); glPopMatrix();
    glColor3f(0.38f, 0.22f, 0.10f);
    glPushMatrix(); glTranslatef(0.0f, 0.35f, 0.36f); draw_box(1.05f, 0.24f, 0.12f); draw_box_outline(1.05f, 0.24f, 0.12f); glPopMatrix();
    glColor3f(0.22f, 0.12f, 0.06f);
    glPushMatrix(); glTranslatef(-0.36f, -0.12f, 0.0f); draw_box(0.42f, 1.38f, 0.42f); draw_box_outline(0.42f, 1.38f, 0.42f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.36f, -0.12f, 0.0f); draw_box(0.42f, 1.38f, 0.42f); draw_box_outline(0.42f, 1.38f, 0.42f); glPopMatrix();
    glColor3f(0.48f, 0.21f, 0.08f);
    glPushMatrix(); glTranslatef(-0.76f, 0.82f, 0.0f); draw_box(0.30f, 1.20f, 0.34f); draw_box_outline(0.30f, 1.20f, 0.34f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.76f, 0.82f, 0.0f); draw_box(0.30f, 1.20f, 0.34f); draw_box_outline(0.30f, 1.20f, 0.34f); glPopMatrix();

    glPushMatrix();
    glTranslatef(0.0f, 1.90f, 0.0f);
    glRotatef(draw_pitch, 1, 0, 0);
    glColor3f(0.34f, 0.21f, 0.13f); draw_box(0.65f, 0.74f, 0.65f); draw_box_outline(0.65f, 0.74f, 0.65f);
    glColor3f(0.05f, 0.05f, 0.05f);
    glPushMatrix(); glTranslatef(0.0f, 0.36f, 0.0f); draw_box(1.52f, 0.22f, 0.95f); draw_box_outline(1.52f, 0.22f, 0.95f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.0f, 0.58f, 0.0f); draw_box(0.62f, 0.24f, 0.52f); draw_box_outline(0.62f, 0.24f, 0.52f); glPopMatrix();
    glColor3f(0.86f, 0.82f, 0.74f);
    glPushMatrix(); glTranslatef(0.0f, 0.39f, 0.42f); draw_box(0.24f, 0.20f, 0.06f); draw_box_outline(0.24f, 0.20f, 0.06f); glPopMatrix();
    glPopMatrix();

    glPushMatrix(); glTranslatef(0.64f, 1.03f, 0.57f);
    glRotatef(draw_pitch, 1, 0, 0);
    glRotatef(-draw_recoil * 10.0f, 1, 0, 0);
    glTranslatef(0.0f, 0.0f, -draw_recoil * 0.08f);
    glScalef(0.8f, 0.8f, 0.8f); draw_gun_model(p->current_weapon); glPopMatrix();
}

static void draw_player_skin_ninja(PlayerState *p, float draw_pitch, float draw_recoil) {
    glColor3f(0.11f, 0.45f, 0.78f);
    glPushMatrix(); glTranslatef(0.0f, 0.82f, 0.0f); draw_box(1.18f, 1.44f, 0.68f); draw_box_outline(1.18f, 1.44f, 0.68f); glPopMatrix();
    glColor3f(0.03f, 0.12f, 0.22f);
    glPushMatrix(); glTranslatef(0.0f, 0.34f, 0.35f); draw_box(1.08f, 0.14f, 0.10f); draw_box_outline(1.08f, 0.14f, 0.10f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.0f, 0.18f, 0.29f); draw_box(0.92f, 0.10f, 0.10f); draw_box_outline(0.92f, 0.10f, 0.10f); glPopMatrix();
    glColor3f(0.08f, 0.36f, 0.65f);
    glPushMatrix(); glTranslatef(-0.32f, -0.10f, 0.0f); draw_box(0.40f, 1.42f, 0.42f); draw_box_outline(0.40f, 1.42f, 0.42f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.32f, -0.10f, 0.0f); draw_box(0.40f, 1.42f, 0.42f); draw_box_outline(0.40f, 1.42f, 0.42f); glPopMatrix();
    glColor3f(0.07f, 0.40f, 0.72f);
    glPushMatrix(); glTranslatef(-0.70f, 0.84f, 0.0f); draw_box(0.30f, 1.24f, 0.34f); draw_box_outline(0.30f, 1.24f, 0.34f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.70f, 0.84f, 0.0f); draw_box(0.30f, 1.24f, 0.34f); draw_box_outline(0.30f, 1.24f, 0.34f); glPopMatrix();

    glPushMatrix();
    glTranslatef(0.0f, 1.92f, 0.0f);
    glRotatef(draw_pitch, 1, 0, 0);
    glColor3f(0.12f, 0.54f, 0.88f); draw_box(0.68f, 0.84f, 0.68f); draw_box_outline(0.68f, 0.84f, 0.68f);
    glColor3f(0.02f, 0.06f, 0.10f);
    glPushMatrix(); glTranslatef(0.0f, 0.06f, 0.37f); draw_box(0.56f, 0.28f, 0.10f); draw_box_outline(0.56f, 0.28f, 0.10f); glPopMatrix();
    glColor3f(0.78f, 0.66f, 0.56f);
    glPushMatrix(); glTranslatef(0.0f, 0.03f, 0.42f); draw_box(0.24f, 0.14f, 0.03f); draw_box_outline(0.24f, 0.14f, 0.03f); glPopMatrix();
    glPopMatrix();

    glPushMatrix(); glTranslatef(0.64f, 1.03f, 0.57f);
    glRotatef(draw_pitch, 1, 0, 0);
    glRotatef(-draw_recoil * 10.0f, 1, 0, 0);
    glTranslatef(0.0f, 0.0f, -draw_recoil * 0.08f);
    glScalef(0.8f, 0.8f, 0.8f); draw_gun_model(p->current_weapon); glPopMatrix();
}

static void draw_player_skin_pimp(PlayerState *p, float draw_pitch, float draw_recoil) {
    glColor3f(0.50f, 0.12f, 0.68f);
    glPushMatrix(); glTranslatef(0.0f, 0.80f, 0.0f); draw_box(1.28f, 1.46f, 0.72f); draw_box_outline(1.28f, 1.46f, 0.72f); glPopMatrix();
    glColor3f(0.34f, 0.08f, 0.48f);
    glPushMatrix(); glTranslatef(0.0f, 1.44f, 0.0f); draw_box(1.44f, 0.20f, 0.90f); draw_box_outline(1.44f, 0.20f, 0.90f); glPopMatrix();
    glColor3f(0.72f, 0.64f, 0.12f);
    glPushMatrix(); glTranslatef(0.0f, 0.35f, 0.37f); draw_box(0.18f, 0.56f, 0.06f); draw_box_outline(0.18f, 0.56f, 0.06f); glPopMatrix();
    glColor3f(0.35f, 0.11f, 0.52f);
    glPushMatrix(); glTranslatef(-0.72f, 0.82f, 0.0f); draw_box(0.30f, 1.20f, 0.34f); draw_box_outline(0.30f, 1.20f, 0.34f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.72f, 0.82f, 0.0f); draw_box(0.30f, 1.20f, 0.34f); draw_box_outline(0.30f, 1.20f, 0.34f); glPopMatrix();
    glColor3f(0.18f, 0.06f, 0.28f);
    glPushMatrix(); glTranslatef(-0.33f, -0.10f, 0.0f); draw_box(0.40f, 1.42f, 0.42f); draw_box_outline(0.40f, 1.42f, 0.42f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.33f, -0.10f, 0.0f); draw_box(0.40f, 1.42f, 0.42f); draw_box_outline(0.40f, 1.42f, 0.42f); glPopMatrix();

    glPushMatrix();
    glTranslatef(0.0f, 1.94f, 0.0f);
    glRotatef(draw_pitch, 1, 0, 0);
    glColor3f(0.70f, 0.53f, 0.42f); draw_box(0.68f, 0.82f, 0.68f); draw_box_outline(0.68f, 0.82f, 0.68f);
    glColor3f(0.56f, 0.12f, 0.78f);
    glPushMatrix(); glTranslatef(0.0f, 0.46f, 0.0f); draw_box(1.54f, 0.20f, 0.96f); draw_box_outline(1.54f, 0.20f, 0.96f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.0f, 0.68f, 0.0f); draw_box(0.62f, 0.22f, 0.52f); draw_box_outline(0.62f, 0.22f, 0.52f); glPopMatrix();
    glPopMatrix();

    glPushMatrix(); glTranslatef(0.64f, 1.03f, 0.57f);
    glRotatef(draw_pitch, 1, 0, 0);
    glRotatef(-draw_recoil * 10.0f, 1, 0, 0);
    glTranslatef(0.0f, 0.0f, -draw_recoil * 0.08f);
    glScalef(0.8f, 0.8f, 0.8f); draw_gun_model(p->current_weapon); glPopMatrix();
}

static void draw_player_skin_viking(PlayerState *p, float draw_pitch, float draw_recoil) {
    glColor3f(0.44f, 0.46f, 0.50f);
    glPushMatrix(); glTranslatef(0.0f, 0.82f, 0.0f); draw_box(1.26f, 1.46f, 0.74f); draw_box_outline(1.26f, 1.46f, 0.74f); glPopMatrix();
    glColor3f(0.58f, 0.40f, 0.12f);
    glPushMatrix(); glTranslatef(0.0f, 0.36f, 0.36f); draw_box(1.10f, 0.16f, 0.10f); draw_box_outline(1.10f, 0.16f, 0.10f); glPopMatrix();
    glColor3f(0.30f, 0.32f, 0.36f);
    glPushMatrix(); glTranslatef(-0.72f, 0.84f, 0.0f); draw_box(0.30f, 1.24f, 0.34f); draw_box_outline(0.30f, 1.24f, 0.34f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.72f, 0.84f, 0.0f); draw_box(0.30f, 1.24f, 0.34f); draw_box_outline(0.30f, 1.24f, 0.34f); glPopMatrix();
    glColor3f(0.22f, 0.24f, 0.29f);
    glPushMatrix(); glTranslatef(-0.34f, -0.12f, 0.0f); draw_box(0.42f, 1.40f, 0.42f); draw_box_outline(0.42f, 1.40f, 0.42f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.34f, -0.12f, 0.0f); draw_box(0.42f, 1.40f, 0.42f); draw_box_outline(0.42f, 1.40f, 0.42f); glPopMatrix();

    glPushMatrix();
    glTranslatef(0.0f, 1.94f, 0.0f);
    glRotatef(draw_pitch, 1, 0, 0);
    glColor3f(0.64f, 0.46f, 0.34f); draw_box(0.70f, 0.84f, 0.68f); draw_box_outline(0.70f, 0.84f, 0.68f);
    glColor3f(0.80f, 0.14f, 0.10f);
    glPushMatrix(); glTranslatef(0.0f, 0.58f, 0.0f); draw_box(0.92f, 0.24f, 0.88f); draw_box_outline(0.92f, 0.24f, 0.88f); glPopMatrix();
    glColor3f(0.70f, 0.58f, 0.26f);
    glPushMatrix(); glTranslatef(-0.50f, 0.62f, 0.0f); draw_box(0.14f, 0.54f, 0.14f); draw_box_outline(0.14f, 0.54f, 0.14f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.50f, 0.62f, 0.0f); draw_box(0.14f, 0.54f, 0.14f); draw_box_outline(0.14f, 0.54f, 0.14f); glPopMatrix();
    glPopMatrix();

    glPushMatrix(); glTranslatef(0.64f, 1.03f, 0.57f);
    glRotatef(draw_pitch, 1, 0, 0);
    glRotatef(-draw_recoil * 10.0f, 1, 0, 0);
    glTranslatef(0.0f, 0.0f, -draw_recoil * 0.08f);
    glScalef(0.8f, 0.8f, 0.8f); draw_gun_model(p->current_weapon); glPopMatrix();
}

static void draw_player_skin_bill(PlayerState *p, float draw_pitch, float draw_recoil) {
    glColor3f(0.22f, 0.22f, 0.26f);
    glPushMatrix(); glTranslatef(0.0f, 0.82f, 0.0f); draw_box(1.16f, 1.44f, 0.68f); draw_box_outline(1.16f, 1.44f, 0.68f); glPopMatrix();
    glColor3f(0.12f, 0.80f, 0.72f);
    glPushMatrix(); glTranslatef(0.0f, 1.02f, 0.36f); draw_box(0.64f, 0.24f, 0.06f); draw_box_outline(0.64f, 0.24f, 0.06f); glPopMatrix();
    glColor3f(0.18f, 0.18f, 0.22f);
    glPushMatrix(); glTranslatef(-0.68f, 0.84f, 0.0f); draw_box(0.30f, 1.24f, 0.34f); draw_box_outline(0.30f, 1.24f, 0.34f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.68f, 0.84f, 0.0f); draw_box(0.30f, 1.24f, 0.34f); draw_box_outline(0.30f, 1.24f, 0.34f); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.30f, -0.10f, 0.0f); draw_box(0.40f, 1.42f, 0.42f); draw_box_outline(0.40f, 1.42f, 0.42f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.30f, -0.10f, 0.0f); draw_box(0.40f, 1.42f, 0.42f); draw_box_outline(0.40f, 1.42f, 0.42f); glPopMatrix();

    glPushMatrix();
    glTranslatef(0.0f, 1.94f, 0.0f);
    glRotatef(draw_pitch, 1, 0, 0);
    glColor3f(0.72f, 0.56f, 0.45f); draw_box(0.68f, 0.84f, 0.68f); draw_box_outline(0.68f, 0.84f, 0.68f);
    glColor3f(0.04f, 0.09f, 0.12f);
    glPushMatrix(); glTranslatef(0.0f, 0.10f, 0.37f); draw_box(0.58f, 0.24f, 0.10f); draw_box_outline(0.58f, 0.24f, 0.10f); glPopMatrix();
    glColor3f(0.12f, 0.86f, 0.78f);
    glPushMatrix(); glTranslatef(0.0f, 0.54f, 0.0f); draw_box(0.78f, 0.12f, 0.74f); draw_box_outline(0.78f, 0.12f, 0.74f); glPopMatrix();
    glPopMatrix();

    glPushMatrix(); glTranslatef(0.64f, 1.03f, 0.57f);
    glRotatef(draw_pitch, 1, 0, 0);
    glRotatef(-draw_recoil * 10.0f, 1, 0, 0);
    glTranslatef(0.0f, 0.0f, -draw_recoil * 0.08f);
    glScalef(0.8f, 0.8f, 0.8f); draw_gun_model(p->current_weapon); glPopMatrix();
}

static void draw_player_skin_genie(PlayerState *p, float draw_pitch, float draw_recoil) {
    glColor3f(0.26f, 0.64f, 0.92f);
    glPushMatrix(); glTranslatef(0.0f, 0.95f, 0.0f); draw_box(1.18f, 1.20f, 0.66f); draw_box_outline(1.18f, 1.20f, 0.66f); glPopMatrix();
    glColor3f(0.88f, 0.72f, 0.12f);
    glPushMatrix(); glTranslatef(0.0f, 0.35f, 0.36f); draw_box(0.16f, 0.52f, 0.06f); draw_box_outline(0.16f, 0.52f, 0.06f); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.70f, 0.86f, 0.0f); draw_box(0.30f, 1.18f, 0.34f); draw_box_outline(0.30f, 1.18f, 0.34f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.70f, 0.86f, 0.0f); draw_box(0.30f, 1.18f, 0.34f); draw_box_outline(0.30f, 1.18f, 0.34f); glPopMatrix();
    glColor3f(0.10f, 0.48f, 0.84f);
    glPushMatrix(); glTranslatef(0.0f, -0.30f, 0.0f); draw_box(0.78f, 0.92f, 0.64f); draw_box_outline(0.78f, 0.92f, 0.64f); glPopMatrix();
    glColor3f(0.20f, 0.62f, 0.95f);
    glPushMatrix(); glTranslatef(0.0f, -0.82f, 0.0f); draw_box(0.58f, 0.46f, 0.54f); draw_box_outline(0.58f, 0.46f, 0.54f); glPopMatrix();

    glPushMatrix();
    glTranslatef(0.0f, 1.94f, 0.0f);
    glRotatef(draw_pitch, 1, 0, 0);
    glColor3f(0.64f, 0.80f, 0.92f); draw_box(0.68f, 0.84f, 0.68f); draw_box_outline(0.68f, 0.84f, 0.68f);
    glColor3f(0.06f, 0.08f, 0.12f);
    glPushMatrix(); glTranslatef(0.0f, 0.38f, 0.0f); draw_box(0.62f, 0.16f, 0.58f); draw_box_outline(0.62f, 0.16f, 0.58f); glPopMatrix();
    glColor3f(0.94f, 0.80f, 0.18f);
    glPushMatrix(); glTranslatef(0.0f, 0.62f, 0.0f); draw_box(0.34f, 0.26f, 0.32f); draw_box_outline(0.34f, 0.26f, 0.32f); glPopMatrix();
    glColor3f(0.06f, 0.12f, 0.18f);
    glPushMatrix(); glTranslatef(0.0f, 0.08f, 0.37f); draw_box(0.54f, 0.22f, 0.10f); draw_box_outline(0.54f, 0.22f, 0.10f); glPopMatrix();
    glPopMatrix();

    glPushMatrix(); glTranslatef(0.64f, 1.03f, 0.57f);
    glRotatef(draw_pitch, 1, 0, 0);
    glRotatef(-draw_recoil * 10.0f, 1, 0, 0);
    glTranslatef(0.0f, 0.0f, -draw_recoil * 0.08f);
    glScalef(0.8f, 0.8f, 0.8f); draw_gun_model(p->current_weapon); glPopMatrix();
}



static void draw_player_skin_pink(PlayerState *p, float draw_pitch, float draw_recoil) {
    // Space-marine samurai silhouette: heavy chest, layered pauldrons, plated legs.
    glColor3f(0.86f, 0.22f, 0.58f);
    glPushMatrix(); glTranslatef(0.0f, 0.82f, 0.0f); draw_box(1.32f, 1.52f, 0.78f); draw_box_outline(1.32f, 1.52f, 0.78f); glPopMatrix();
    glColor3f(0.70f, 0.14f, 0.44f);
    glPushMatrix(); glTranslatef(0.0f, 0.18f, 0.34f); draw_box(1.02f, 0.18f, 0.10f); draw_box_outline(1.02f, 0.18f, 0.10f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.0f, 0.38f, 0.37f); draw_box(0.82f, 0.14f, 0.09f); draw_box_outline(0.82f, 0.14f, 0.09f); glPopMatrix();

    // Big marine pauldrons + arm plates.
    glColor3f(0.92f, 0.34f, 0.66f);
    glPushMatrix(); glTranslatef(-0.74f, 1.04f, 0.0f); draw_box(0.44f, 0.54f, 0.52f); draw_box_outline(0.44f, 0.54f, 0.52f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.74f, 1.04f, 0.0f); draw_box(0.44f, 0.54f, 0.52f); draw_box_outline(0.44f, 0.54f, 0.52f); glPopMatrix();
    glColor3f(0.76f, 0.18f, 0.50f);
    glPushMatrix(); glTranslatef(-0.72f, 0.66f, 0.0f); draw_box(0.34f, 0.84f, 0.36f); draw_box_outline(0.34f, 0.84f, 0.36f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.72f, 0.66f, 0.0f); draw_box(0.34f, 0.84f, 0.36f); draw_box_outline(0.34f, 0.84f, 0.36f); glPopMatrix();

    // Leg armor and knee plates.
    glColor3f(0.64f, 0.12f, 0.38f);
    glPushMatrix(); glTranslatef(-0.34f, -0.14f, 0.0f); draw_box(0.44f, 1.44f, 0.44f); draw_box_outline(0.44f, 1.44f, 0.44f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.34f, -0.14f, 0.0f); draw_box(0.44f, 1.44f, 0.44f); draw_box_outline(0.44f, 1.44f, 0.44f); glPopMatrix();
    glColor3f(0.84f, 0.24f, 0.56f);
    glPushMatrix(); glTranslatef(-0.34f, -0.64f, 0.24f); draw_box(0.30f, 0.24f, 0.10f); draw_box_outline(0.30f, 0.24f, 0.10f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.34f, -0.64f, 0.24f); draw_box(0.30f, 0.24f, 0.10f); draw_box_outline(0.30f, 0.24f, 0.10f); glPopMatrix();

    // Helmet + samurai hat + horns + marine mask.
    glPushMatrix();
    glTranslatef(0.0f, 1.95f, 0.0f);
    glRotatef(draw_pitch, 1, 0, 0);

    glColor3f(0.78f, 0.44f, 0.58f);
    draw_box(0.70f, 0.82f, 0.68f); draw_box_outline(0.70f, 0.82f, 0.68f);

    // Samurai brim + top cap.
    glColor3f(0.72f, 0.16f, 0.46f);
    glPushMatrix(); glTranslatef(0.0f, 0.34f, 0.0f); draw_box(1.54f, 0.12f, 0.98f); draw_box_outline(1.54f, 0.12f, 0.98f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.0f, 0.54f, 0.0f); draw_box(0.78f, 0.22f, 0.66f); draw_box_outline(0.78f, 0.22f, 0.66f); glPopMatrix();

    // Horns / crescent crests.
    glColor3f(0.94f, 0.40f, 0.70f);
    glPushMatrix(); glTranslatef(-0.44f, 0.76f, 0.0f); draw_box(0.14f, 0.42f, 0.14f); draw_box_outline(0.14f, 0.42f, 0.14f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.44f, 0.76f, 0.0f); draw_box(0.14f, 0.42f, 0.14f); draw_box_outline(0.14f, 0.42f, 0.14f); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.32f, 0.88f, 0.0f); draw_box(0.16f, 0.12f, 0.16f); draw_box_outline(0.16f, 0.12f, 0.16f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.32f, 0.88f, 0.0f); draw_box(0.16f, 0.12f, 0.16f); draw_box_outline(0.16f, 0.12f, 0.16f); glPopMatrix();

    // Marine visor + mask.
    glColor3f(0.18f, 0.06f, 0.12f);
    glPushMatrix(); glTranslatef(0.0f, 0.18f, 0.38f); draw_box(0.58f, 0.24f, 0.10f); draw_box_outline(0.58f, 0.24f, 0.10f); glPopMatrix();
    glColor3f(0.96f, 0.80f, 0.82f);
    glPushMatrix(); glTranslatef(0.0f, 0.42f, 0.42f); draw_box(0.24f, 0.16f, 0.04f); draw_box_outline(0.24f, 0.16f, 0.04f); glPopMatrix();
    glColor3f(0.84f, 0.22f, 0.58f);
    glPushMatrix(); glTranslatef(0.0f, -0.02f, 0.40f); draw_box(0.56f, 0.24f, 0.10f); draw_box_outline(0.56f, 0.24f, 0.10f); glPopMatrix();

    glPopMatrix();

    glPushMatrix(); glTranslatef(0.64f, 1.03f, 0.57f);
    glRotatef(draw_pitch, 1, 0, 0);
    glRotatef(-draw_recoil * 10.0f, 1, 0, 0);
    glTranslatef(0.0f, 0.0f, -draw_recoil * 0.08f);
    glScalef(0.8f, 0.8f, 0.8f); draw_gun_model(p->current_weapon); glPopMatrix();
}

static void draw_player_skin_wanderer(PlayerState *p, float draw_pitch, float draw_recoil) {
    glColor3f(0.42f, 0.30f, 0.22f);
    glPushMatrix(); glTranslatef(0.0f, 0.78f, 0.0f); draw_box(1.30f, 1.58f, 0.76f); draw_box_outline(1.30f, 1.58f, 0.76f); glPopMatrix();
    glColor3f(0.30f, 0.44f, 0.28f);
    glPushMatrix(); glTranslatef(0.0f, 0.72f, 0.39f); draw_box(0.80f, 0.52f, 0.06f); draw_box_outline(0.80f, 0.52f, 0.06f); glPopMatrix();
    glColor3f(0.34f, 0.24f, 0.18f);
    glPushMatrix(); glTranslatef(-0.74f, 0.82f, 0.0f); draw_box(0.30f, 1.20f, 0.34f); draw_box_outline(0.30f, 1.20f, 0.34f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.74f, 0.82f, 0.0f); draw_box(0.30f, 1.20f, 0.34f); draw_box_outline(0.30f, 1.20f, 0.34f); glPopMatrix();
    glColor3f(0.10f, 0.10f, 0.12f);
    glPushMatrix(); glTranslatef(-0.34f, -0.10f, 0.0f); draw_box(0.42f, 1.42f, 0.42f); draw_box_outline(0.42f, 1.42f, 0.42f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.34f, -0.10f, 0.0f); draw_box(0.42f, 1.42f, 0.42f); draw_box_outline(0.42f, 1.42f, 0.42f); glPopMatrix();

    glPushMatrix();
    glTranslatef(0.0f, 1.94f, 0.0f);
    glRotatef(draw_pitch, 1, 0, 0);
    glColor3f(0.70f, 0.55f, 0.44f); draw_box(0.68f, 0.84f, 0.68f); draw_box_outline(0.68f, 0.84f, 0.68f);
    glColor3f(0.24f, 0.14f, 0.08f);
    glPushMatrix(); glTranslatef(0.0f, 0.14f, 0.37f); draw_box(0.56f, 0.22f, 0.10f); draw_box_outline(0.56f, 0.22f, 0.10f); glPopMatrix();
    glColor3f(0.16f, 0.10f, 0.06f);
    glPushMatrix(); glTranslatef(0.0f, 0.34f, 0.40f); draw_box(0.44f, 0.20f, 0.06f); draw_box_outline(0.44f, 0.20f, 0.06f); glPopMatrix();
    glColor3f(0.18f, 0.11f, 0.06f);
    glPushMatrix(); glTranslatef(0.0f, 0.58f, 0.0f); draw_box(0.86f, 0.24f, 0.82f); draw_box_outline(0.86f, 0.24f, 0.82f); glPopMatrix();
    glPopMatrix();

    glPushMatrix(); glTranslatef(0.64f, 1.03f, 0.57f);
    glRotatef(draw_pitch, 1, 0, 0);
    glRotatef(-draw_recoil * 10.0f, 1, 0, 0);
    glTranslatef(0.0f, 0.0f, -draw_recoil * 0.08f);
    glScalef(0.8f, 0.8f, 0.8f); draw_gun_model(p->current_weapon); glPopMatrix();
}

static void draw_player_skin_geisha(PlayerState *p, float draw_pitch, float draw_recoil) {
    glColor3f(0.76f, 0.08f, 0.08f);
    glPushMatrix(); glTranslatef(0.0f, 0.80f, 0.0f); draw_box(1.26f, 1.52f, 0.72f); draw_box_outline(1.26f, 1.52f, 0.72f); glPopMatrix();
    glColor3f(0.96f, 0.72f, 0.18f);
    glPushMatrix(); glTranslatef(0.0f, 0.38f, 0.37f); draw_box(1.10f, 0.14f, 0.10f); draw_box_outline(1.10f, 0.14f, 0.10f); glPopMatrix();
    glColor3f(0.58f, 0.03f, 0.05f);
    glPushMatrix(); glTranslatef(-0.74f, 0.82f, 0.0f); draw_box(0.32f, 1.30f, 0.36f); draw_box_outline(0.32f, 1.30f, 0.36f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.74f, 0.82f, 0.0f); draw_box(0.32f, 1.30f, 0.36f); draw_box_outline(0.32f, 1.30f, 0.36f); glPopMatrix();
    glColor3f(0.70f, 0.06f, 0.08f);
    glPushMatrix(); glTranslatef(-0.35f, -0.10f, 0.0f); draw_box(0.46f, 1.40f, 0.46f); draw_box_outline(0.46f, 1.40f, 0.46f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.35f, -0.10f, 0.0f); draw_box(0.46f, 1.40f, 0.46f); draw_box_outline(0.46f, 1.40f, 0.46f); glPopMatrix();

    glPushMatrix();
    glTranslatef(0.0f, 1.96f, 0.0f);
    glRotatef(draw_pitch, 1, 0, 0);
    glColor3f(0.92f, 0.92f, 0.92f); draw_box(0.68f, 0.84f, 0.68f); draw_box_outline(0.68f, 0.84f, 0.68f);
    glColor3f(0.02f, 0.02f, 0.03f);
    glPushMatrix(); glTranslatef(0.0f, 0.32f, 0.0f); draw_box(0.94f, 0.34f, 0.86f); draw_box_outline(0.94f, 0.34f, 0.86f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.0f, 0.58f, 0.0f); draw_box(0.56f, 0.20f, 0.50f); draw_box_outline(0.56f, 0.20f, 0.50f); glPopMatrix();
    glColor3f(0.72f, 0.06f, 0.10f);
    glPushMatrix(); glTranslatef(-0.22f, 0.18f, 0.39f); draw_box(0.12f, 0.06f, 0.05f); draw_box_outline(0.12f, 0.06f, 0.05f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.22f, 0.18f, 0.39f); draw_box(0.12f, 0.06f, 0.05f); draw_box_outline(0.12f, 0.06f, 0.05f); glPopMatrix();
    glColor3f(0.66f, 0.10f, 0.16f);
    glPushMatrix(); glTranslatef(0.0f, -0.08f, 0.40f); draw_box(0.20f, 0.07f, 0.04f); draw_box_outline(0.20f, 0.07f, 0.04f); glPopMatrix();
    glColor3f(0.84f, 0.70f, 0.76f);
    glPushMatrix(); glTranslatef(0.36f, 0.30f, 0.28f); draw_box(0.16f, 0.16f, 0.14f); draw_box_outline(0.16f, 0.16f, 0.14f); glPopMatrix();
    glColor3f(0.80f, 0.74f, 0.82f);
    glPushMatrix(); glTranslatef(0.46f, 0.20f, 0.28f); draw_box(0.08f, 0.34f, 0.08f); draw_box_outline(0.08f, 0.34f, 0.08f); glPopMatrix();
    glPopMatrix();

    glPushMatrix(); glTranslatef(0.64f, 1.03f, 0.57f);
    glRotatef(draw_pitch, 1, 0, 0);
    glRotatef(-draw_recoil * 10.0f, 1, 0, 0);
    glTranslatef(0.0f, 0.0f, -draw_recoil * 0.08f);
    glScalef(0.8f, 0.8f, 0.8f); draw_gun_model(p->current_weapon); glPopMatrix();
}

static void draw_player_skin_alpine(PlayerState *p, float draw_pitch, float draw_recoil) {
    glColor3f(0.26f, 0.46f, 0.24f);
    glPushMatrix(); glTranslatef(0.0f, 0.82f, 0.0f); draw_box(1.22f, 1.44f, 0.70f); draw_box_outline(1.22f, 1.44f, 0.70f); glPopMatrix();
    glColor3f(0.90f, 0.92f, 0.88f);
    glPushMatrix(); glTranslatef(0.0f, 0.94f, 0.37f); draw_box(0.92f, 0.42f, 0.08f); draw_box_outline(0.92f, 0.42f, 0.08f); glPopMatrix();
    glColor3f(0.14f, 0.30f, 0.12f);
    glPushMatrix(); glTranslatef(-0.20f, 0.34f, 0.37f); draw_box(0.10f, 1.06f, 0.08f); draw_box_outline(0.10f, 1.06f, 0.08f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.20f, 0.34f, 0.37f); draw_box(0.10f, 1.06f, 0.08f); draw_box_outline(0.10f, 1.06f, 0.08f); glPopMatrix();
    glColor3f(0.22f, 0.36f, 0.20f);
    glPushMatrix(); glTranslatef(-0.72f, 0.84f, 0.0f); draw_box(0.30f, 1.24f, 0.34f); draw_box_outline(0.30f, 1.24f, 0.34f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.72f, 0.84f, 0.0f); draw_box(0.30f, 1.24f, 0.34f); draw_box_outline(0.30f, 1.24f, 0.34f); glPopMatrix();
    glColor3f(0.16f, 0.28f, 0.14f);
    glPushMatrix(); glTranslatef(-0.32f, -0.12f, 0.0f); draw_box(0.42f, 1.32f, 0.42f); draw_box_outline(0.42f, 1.32f, 0.42f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.32f, -0.12f, 0.0f); draw_box(0.42f, 1.32f, 0.42f); draw_box_outline(0.42f, 1.32f, 0.42f); glPopMatrix();
    glColor3f(0.36f, 0.22f, 0.12f);
    glPushMatrix(); glTranslatef(-0.32f, -0.82f, 0.0f); draw_box(0.46f, 0.28f, 0.46f); draw_box_outline(0.46f, 0.28f, 0.46f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.32f, -0.82f, 0.0f); draw_box(0.46f, 0.28f, 0.46f); draw_box_outline(0.46f, 0.28f, 0.46f); glPopMatrix();

    glPushMatrix();
    glTranslatef(0.0f, 1.94f, 0.0f);
    glRotatef(draw_pitch, 1, 0, 0);
    glColor3f(0.62f, 0.46f, 0.34f); draw_box(0.68f, 0.82f, 0.68f); draw_box_outline(0.68f, 0.82f, 0.68f);
    glColor3f(0.24f, 0.12f, 0.06f);
    glPushMatrix(); glTranslatef(0.0f, 0.08f, 0.40f); draw_box(0.56f, 0.20f, 0.06f); draw_box_outline(0.56f, 0.20f, 0.06f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.0f, -0.02f, 0.42f); draw_box(0.42f, 0.12f, 0.04f); draw_box_outline(0.42f, 0.12f, 0.04f); glPopMatrix();
    glColor3f(0.08f, 0.26f, 0.08f);
    glPushMatrix(); glTranslatef(0.0f, 0.58f, 0.0f); draw_box(1.04f, 0.16f, 0.90f); draw_box_outline(1.04f, 0.16f, 0.90f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.0f, 0.78f, 0.0f); draw_box(0.74f, 0.28f, 0.64f); draw_box_outline(0.74f, 0.28f, 0.64f); glPopMatrix();
    glColor3f(0.72f, 0.14f, 0.10f);
    glPushMatrix(); glTranslatef(0.44f, 0.80f, 0.10f); draw_box(0.10f, 0.34f, 0.10f); draw_box_outline(0.10f, 0.34f, 0.10f); glPopMatrix();
    glPopMatrix();

    glPushMatrix(); glTranslatef(0.64f, 1.03f, 0.57f);
    glRotatef(draw_pitch, 1, 0, 0);
    glRotatef(-draw_recoil * 10.0f, 1, 0, 0);
    glTranslatef(0.0f, 0.0f, -draw_recoil * 0.08f);
    glScalef(0.8f, 0.8f, 0.8f); draw_gun_model(p->current_weapon); glPopMatrix();
}

void draw_weapon_p(PlayerState *p) {
    static int prev_weapon = -1;
    static int prev_shooting = 0;
    static int prev_flag_carrying = 0;
    static Uint32 knife_stab_start_ms = 0;
    static Uint32 flag_swing_start_ms = 0;
    if (p->in_vehicle) return; 
    glPushMatrix();
    glLoadIdentity();
    const float viewmodel_global_scale = 0.72f;
    Uint32 now_ms = SDL_GetTicks();
    int flag_carrying_vm = player_is_flag_carrying_viewmodel(p);
    ViewmodelTuning tune = flag_carrying_vm ? viewmodel_tuning_for_flag_carry(p->carried_flag_team_id) : viewmodel_tuning_for_weapon(p->current_weapon);

    if (flag_carrying_vm) {
        knife_stab_start_ms = 0;
    } else if (p->current_weapon == WPN_KNIFE) {
        if (prev_weapon != WPN_KNIFE) knife_stab_start_ms = 0;
        if (p->is_shooting > 0 && prev_shooting <= 0) knife_stab_start_ms = now_ms;
    } else if (prev_weapon == WPN_KNIFE) {
        knife_stab_start_ms = 0;
    }

    float knife_stab_t = 0.0f;
    if (p->current_weapon == WPN_KNIFE && knife_stab_start_ms > 0) {
        knife_stab_t = clamp01f((float)(now_ms - knife_stab_start_ms) / 210.0f);
        if (knife_stab_t >= 1.0f) knife_stab_start_ms = 0;
    }

    if (flag_carrying_vm) {
        if (!prev_flag_carrying) flag_swing_start_ms = 0;
        if (p->is_shooting > 0 && prev_shooting <= 0) flag_swing_start_ms = now_ms;
    } else {
        flag_swing_start_ms = 0;
    }

    float kick = (p->current_weapon == WPN_KNIFE || flag_carrying_vm) ? 0.0f : p->recoil_anim * tune.kick_scale;
    float reload_dip = (p->reload_timer > 0) ? sinf(p->reload_timer * 0.2f) * 0.5f - 0.5f : 0.0f;
    float slash_swing = (p->current_weapon == WPN_KATANA && p->katana_slash_timer > 0) ? ((float)p->katana_slash_timer / (float)KATANA_SLASH_ACTIVE_TICKS) : 0.0f;
    float dash_push = (p->current_weapon == WPN_KATANA && p->dash_timer > 0) ? 0.22f : 0.0f;
    float knife_drive = 0.0f, knife_recover = 0.0f;
    if (p->current_weapon == WPN_KNIFE && knife_stab_t > 0.0f) {
        knife_drive = clamp01f(knife_stab_t / 0.22f);
        knife_recover = clamp01f((knife_stab_t - 0.22f) / 0.78f);
        knife_drive = 1.0f - (1.0f - knife_drive) * (1.0f - knife_drive);
        knife_recover = knife_recover * knife_recover;
    }
    /* Positive knife_anim_forward means "stab into screen" (negative camera-z direction). */
    float knife_anim_forward = (knife_drive * 1.05f) - (knife_recover * 0.36f);
    float knife_anim_drop = (knife_drive * 0.13f) - (knife_recover * 0.05f);
    float knife_anim_pitch = (knife_drive * 30.0f) - (knife_recover * 10.0f);
    float knife_anim_roll = (knife_drive * 7.0f) - (knife_recover * 3.0f);

    float flag_swing_t = 0.0f;
    if (flag_carrying_vm && flag_swing_start_ms > 0) {
        flag_swing_t = clamp01f((float)(now_ms - flag_swing_start_ms) / 250.0f);
        if (flag_swing_t >= 1.0f) flag_swing_start_ms = 0;
    }
    float flag_wind = clamp01f(flag_swing_t / 0.20f);
    float flag_drive = clamp01f((flag_swing_t - 0.12f) / 0.36f);
    float flag_recover = clamp01f((flag_swing_t - 0.48f) / 0.52f);
    flag_wind = flag_wind * flag_wind;
    flag_drive = 1.0f - (1.0f - flag_drive) * (1.0f - flag_drive);
    flag_recover = flag_recover * flag_recover;
    float flag_anim_x = (-0.06f * flag_wind) + (0.20f * flag_drive) - (0.13f * flag_recover);
    float flag_anim_y = (0.04f * flag_wind) - (0.11f * flag_drive) + (0.05f * flag_recover);
    float flag_anim_z = (0.09f * flag_wind) - (0.56f * flag_drive) + (0.20f * flag_recover);
    float flag_anim_pitch = (5.0f * flag_wind) + (26.0f * flag_drive) - (10.0f * flag_recover);
    float flag_anim_yaw = (-8.0f * flag_wind) + (-34.0f * flag_drive) + (14.0f * flag_recover);
    float flag_anim_roll = (4.0f * flag_wind) + (18.0f * flag_drive) - (8.0f * flag_recover);

    float speed = sqrtf(p->vx*p->vx + p->vz*p->vz);
    float bob = sinf(SDL_GetTicks() * 0.015f) * speed * tune.idle_scale;
    float ads_blend = (current_fov < 50.0f) ? 0.22f : 0.0f;
    if (flag_carrying_vm) ads_blend = 0.0f;
    glTranslatef(
        tune.base_x - ads_blend + slash_swing * 0.18f + flag_anim_x,
        tune.base_y + kick + reload_dip * 0.7f + (bob * (flag_carrying_vm ? 0.55f : 0.4f)) - knife_anim_drop + flag_anim_y,
        tune.base_z + (kick * 0.45f) + (bob * (flag_carrying_vm ? 1.15f : 1.0f)) - dash_push - p->recoil_anim * tune.recoil_back - knife_anim_forward + flag_anim_z
    );
    glRotatef(-p->recoil_anim * tune.recoil_pitch - slash_swing * 65.0f - knife_anim_pitch + flag_anim_pitch, 1, 0, 0);
    glRotatef(flag_anim_yaw, 0, 1, 0);
    glRotatef(-p->recoil_anim * tune.recoil_roll - knife_anim_roll + flag_anim_roll, 0, 0, 1);
    glRotatef(-slash_swing * 40.0f, 0, 0, 1);
    if (flag_carrying_vm) glRotatef(-14.0f, 0, 1, 0);
    else if (p->current_weapon == WPN_KNIFE) glRotatef(-12.0f, 0, 1, 0);
    glScalef(viewmodel_global_scale, viewmodel_global_scale, viewmodel_global_scale);
    if (flag_carrying_vm) draw_viewmodel_flag_carry(p->carried_flag_team_id);
    else draw_viewmodel_weapon(p->current_weapon);
    if (!flag_carrying_vm && p->is_shooting > 0) {
        float flash = 1.0f;
        if (p->current_weapon == WPN_AR) flash = 0.85f;
        else if (p->current_weapon == WPN_SHOTGUN) flash = 1.25f;
        else if (p->current_weapon == WPN_SNIPER) flash = 0.95f;
        draw_weapon_muzzle_flash(p->current_weapon, flash);
    } else if (!flag_carrying_vm && p->recoil_anim > 0.0f) {
        draw_weapon_muzzle_flash(p->current_weapon, p->recoil_anim * 0.55f);
    }
    prev_weapon = p->current_weapon;
    prev_shooting = p->is_shooting;
    prev_flag_carrying = flag_carrying_vm;
    glPopMatrix();
}

void draw_head(int weapon_id) {
    switch(weapon_id) {
        case WPN_KNIFE:   glColor3f(0.8f, 0.8f, 0.9f); break;
        case WPN_MAGNUM:  glColor3f(0.4f, 0.4f, 0.4f); break;
        case WPN_AR:      glColor3f(0.2f, 0.3f, 0.2f); break;
        case WPN_SHOTGUN: glColor3f(0.5f, 0.3f, 0.2f); break;
        case WPN_SNIPER:  glColor3f(0.1f, 0.1f, 0.15f); break;
        case WPN_KATANA:  glColor3f(0.0f, 0.85f, 1.0f); break;
    }
    glBegin(GL_QUADS);
    glVertex3f(-0.4, 0.8, 0.4); glVertex3f(0.4, 0.8, 0.4); glVertex3f(0.4, 0, 0.4); glVertex3f(-0.4, 0, 0.4);
    glVertex3f(-0.4, 0.8, -0.4); glVertex3f(0.4, 0.8, -0.4);
    glVertex3f(0.4, 0, -0.4); glVertex3f(-0.4, 0, -0.4);
    glVertex3f(-0.4, 0.8, 0.4); glVertex3f(0.4, 0.8, 0.4); glVertex3f(0.4, 0.8, -0.4); glVertex3f(-0.4, 0.8, -0.4);
    glVertex3f(-0.4, 0, 0.4); glVertex3f(0.4, 0, 0.4); glVertex3f(0.4, 0, -0.4); glVertex3f(-0.4, 0, -0.4);
    glVertex3f(-0.4, 0.8, 0.4); glVertex3f(-0.4, 0, 0.4);
    glVertex3f(-0.4, 0, -0.4); glVertex3f(-0.4, 0.8, -0.4);
    glVertex3f(0.4, 0.8, 0.4); glVertex3f(0.4, 0, 0.4); glVertex3f(0.4, 0, -0.4); glVertex3f(0.4, 0.8, -0.4);
    glEnd();
}

void draw_player_3rd(PlayerState *p) {
    float draw_yaw = norm_yaw_deg(p->yaw);
    float draw_pitch = clamp_pitch_deg(p->pitch);
    float draw_recoil = (p->is_shooting > 0) ? 1.0f : p->recoil_anim;

    glPushMatrix();
    glTranslatef(p->x, p->y + 0.2f, p->z);
    // Simulation yaw assumes forward is -Z, but this model is authored facing +Z.
    glRotatef(180.0f - draw_yaw, 0, 1, 0);
    if (p->state == STATE_DEAD) {
        float nx = p->death_dir_x;
        float nz = p->death_dir_z;
        float nlen = sqrtf(nx * nx + nz * nz);
        if (nlen < 0.0001f) {
            nx = 0.0f;
            nz = -1.0f;
            nlen = 1.0f;
        }
        nx /= nlen;
        nz /= nlen;
        float yaw_rad = -draw_yaw * 0.0174533f;
        float fwd_x = sinf(yaw_rad), fwd_z = -cosf(yaw_rad);
        float right_x = cosf(yaw_rad), right_z = sinf(yaw_rad);
        float local_fwd = nx * fwd_x + nz * fwd_z;
        float local_right = nx * right_x + nz * right_z;
        float axis_x = -local_fwd;
        float axis_z = local_right;
        float axis_len = sqrtf(axis_x * axis_x + axis_z * axis_z);
        if (axis_len < 0.0001f) {
            axis_x = 1.0f;
            axis_z = 0.0f;
        } else {
            axis_x /= axis_len;
            axis_z /= axis_len;
        }
        unsigned int now_ms = SDL_GetTicks();
        float fall = death_pose_progress(p, now_ms);
        float twist = death_pose_twist_degrees(p, now_ms);
        float twist_sign = (local_right >= 0.0f) ? 1.0f : -1.0f;
        glRotatef(twist * twist_sign, 0.0f, 1.0f, 0.0f);
        glTranslatef(0.0f, 1.1f, 0.0f);
        glRotatef(88.0f * fall, axis_x, 0.0f, axis_z);
        glRotatef(8.0f * fall * local_right, 0.0f, 0.0f, 1.0f);
        glTranslatef(0.0f, -1.1f, 0.0f);
    }
    if (p->in_vehicle && p->vehicle_type == VEH_BUGGY) {
        /* Buggy is rendered from buggy world entities, not player pose. */
    } else {
        int forced_skin = -1;
        if (local_state.game_mode == MODE_TDMB || local_state.game_mode == MODE_TDMO || local_state.game_mode == MODE_CTFB) {
            forced_skin = (p->team_id == 1) ? SKIN_NINJA : SKIN_PIRATE;
        } else if (local_state.game_mode == MODE_STORY && p->is_bot) {
            forced_skin = SKIN_CYBORG;
        }
        int draw_skin = (forced_skin >= 0) ? forced_skin : clamp_skin_id(g_selected_skin);
        switch (draw_skin) {
            case SKIN_WANDERER:
                draw_player_skin_wanderer(p, draw_pitch, draw_recoil);
                break;
            case SKIN_GENIE:
                draw_player_skin_genie(p, draw_pitch, draw_recoil);
                break;
            case SKIN_BILL:
                draw_player_skin_bill(p, draw_pitch, draw_recoil);
                break;
            case SKIN_VIKING:
                draw_player_skin_viking(p, draw_pitch, draw_recoil);
                break;
            case SKIN_PIMP:
                draw_player_skin_pimp(p, draw_pitch, draw_recoil);
                break;
            case SKIN_NINJA:
                draw_player_skin_ninja(p, draw_pitch, draw_recoil);
                break;
            case SKIN_PIRATE:
                draw_player_skin_pirate(p, draw_pitch, draw_recoil);
                break;
            case SKIN_CYBORG:
                draw_player_skin_cyborg(p, draw_pitch, draw_recoil);
                break;
            case SKIN_MAYRICE:
                draw_player_skin_mayrice(p, draw_pitch, draw_recoil);
                break;
            case SKIN_PINK:
                draw_player_skin_pink(p, draw_pitch, draw_recoil);
                break;
            case SKIN_GEISHA:
                draw_player_skin_geisha(p, draw_pitch, draw_recoil);
                break;
            case SKIN_ALPINE:
                draw_player_skin_alpine(p, draw_pitch, draw_recoil);
                break;
            case SKIN_BAT:
            default:
                draw_player_skin_bat(p, draw_pitch, draw_recoil);
                break;
        }
    }
    glPopMatrix();
}

static void draw_helicopter_model(const HelicopterState *h) {
    if (!h || !h->active) return;
    glPushMatrix();
    glTranslatef(h->x, h->y, h->z);
    glRotatef(180.0f - norm_yaw_deg(h->yaw), 0, 1, 0);
    glRotatef(h->roll_visual, 0, 0, 1);
    glRotatef(h->pitch_visual, 1, 0, 0);

    glColor3f(0.25f, 0.7f, 0.25f); draw_box(3.8f, 1.2f, 6.0f);
    glPushMatrix(); glTranslatef(0.0f, 0.4f, -3.5f); glColor3f(0.2f, 0.6f, 0.2f); draw_box(0.5f, 0.4f, 6.0f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.0f, 0.7f, 1.4f); glColor3f(0.35f, 0.85f, 0.35f); draw_box(2.2f, 0.5f, 1.6f); glPopMatrix();
    glPushMatrix(); glTranslatef(-1.4f, -0.8f, 0.9f); glColor3f(0.4f, 0.4f, 0.4f); draw_box(0.2f, 0.2f, 4.8f); glPopMatrix();
    glPushMatrix(); glTranslatef(1.4f, -0.8f, 0.9f); glColor3f(0.4f, 0.4f, 0.4f); draw_box(0.2f, 0.2f, 4.8f); glPopMatrix();

    glPushMatrix();
    glTranslatef(0.0f, 1.1f, 0.0f);
    glRotatef(h->rotor_angle, 0, 1, 0);
    glColor3f(0.05f, 0.05f, 0.05f);
    draw_box(8.0f, 0.08f, 0.18f);
    draw_box(0.18f, 0.08f, 8.0f);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(0.0f, 0.5f, -6.2f);
    glRotatef(h->rotor_angle * 2.5f, 0, 0, 1);
    glColor3f(0.1f, 0.1f, 0.1f);
    draw_box(1.4f, 0.05f, 0.16f);
    glPopMatrix();
    glPopMatrix();
}

// --- NEW HELPER: Wireframe Circle ---
void draw_circle(float x, float y, float r, int segments) {
    glBegin(GL_LINE_LOOP);
    for(int i=0; i<segments; i++) {
        float theta = 2.0f * 3.1415926f * (float)i / (float)segments;
        float cx = r * cosf(theta);
        float cy = r * sinf(theta);
        glVertex2f(x + cx, y + cy);
    }
    glEnd();
}

static void draw_ammo_bars(const PlayerState *p) {
    if (!p) return;
    if (p->current_weapon == WPN_KNIFE || p->current_weapon == WPN_KATANA) return;
    if (p->current_weapon < 0 || p->current_weapon >= MAX_WEAPONS) return;

    int ammo = p->ammo[p->current_weapon];
    if (ammo < 0) ammo = 0;
    if (ammo == 0) return;

    const float bar_w = 12.0f;
    const float bar_h = 20.0f;
    const float gap = 4.0f;
    const int max_cols = 10;
    const float anchor_right = 1230.0f;
    const float anchor_bottom = 36.0f;

    int rows = (ammo + max_cols - 1) / max_cols;
    float bg_left = anchor_right - ((bar_w + gap) * max_cols) - 6.0f;
    float bg_right = anchor_right + 6.0f;
    float bg_bottom = anchor_bottom - 6.0f;
    float bg_top = anchor_bottom + rows * (bar_h + gap) - gap + 6.0f;

    glColor4f(0.10f, 0.08f, 0.04f, 0.45f);
    glRectf(bg_left, bg_bottom, bg_right, bg_top);

    float shade = (p->reload_timer > 0) ? 0.72f : 1.0f;
    glColor3f(1.0f * shade, 0.9f * shade, 0.15f * shade);
    glBegin(GL_QUADS);
    for (int i = 0; i < ammo; i++) {
        int col = i % max_cols;
        int row = i / max_cols;
        float x1 = anchor_right - col * (bar_w + gap);
        float x0 = x1 - bar_w;
        float y0 = anchor_bottom + row * (bar_h + gap);
        float y1 = y0 + bar_h;
        glVertex2f(x0, y0);
        glVertex2f(x1, y0);
        glVertex2f(x1, y1);
        glVertex2f(x0, y1);
    }
    glEnd();
}

void draw_hud(PlayerState *p) {
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); gluOrtho2D(0, 1280, 0, 720);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    if (vs0_art_direction_enabled) glColor3f(0.75f, 0.84f, 0.78f);
    else glColor3f(0, 1, 0);
    if (current_fov < 50.0f) { glBegin(GL_LINES); glVertex2f(0, 360); glVertex2f(1280, 360); glVertex2f(640, 0); glVertex2f(640, 720); glEnd(); } 
    else { glLineWidth(vs0_art_direction_enabled ? 1.4f : 2.0f); glBegin(GL_LINES); glVertex2f(632, 360); glVertex2f(648, 360); glVertex2f(640, 352); glVertex2f(640, 368); glEnd(); }
    
    // --- HIT INDICATORS ---
    if (p->hit_feedback > 0) {
        if (p->hit_feedback >= 25) glColor3f(1.0f, 0.0f, 0.0f); // RED (Kill/High Dmg)
        else glColor3f(0.0f, 1.0f, 0.0f); // GREEN (Normal)
        
        glLineWidth(2.0f);
        draw_circle(640, 360, 20.0f, 16); // Hit Ring
        
        // DOUBLE RING FOR KILL
        if (p->hit_feedback >= 25) {
            draw_circle(640, 360, 28.0f, 16); // Outer Kill Ring
        }
    }


    if (local_state.game_mode == MODE_TDMB || local_state.game_mode == MODE_TDMO) {
        char score_buf[96];
        snprintf(score_buf, sizeof(score_buf), "BLUE %d  -  %d RED", local_state.team_scores[1], local_state.team_scores[0]);
        glColor3f(0.40f, 0.75f, 1.0f);
        draw_string(score_buf, 470, 682, 7);
        glColor3f(0.8f, 0.85f, 0.9f);
        draw_string("TDMB · SCORE LIMIT 25", 520, 658, 4);
        draw_string(p->team_id == 1 ? "YOU: BLUE TEAM" : "YOU: RED TEAM", 548, 638, 4);
    } else if (local_state.game_mode == MODE_CTFB && local_state.ctf.active) {
        const char *my_flag = "HOME";
        const char *enemy_flag = "HOME";
        CtfFlagState *mine = &local_state.ctf.flags[p->team_id];
        CtfFlagState *enemy = &local_state.ctf.flags[p->team_id == 0 ? 1 : 0];
        if (mine->state == FLAG_DROPPED) my_flag = "DROPPED";
        else if (mine->state == FLAG_CARRIED) my_flag = "TAKEN";
        if (enemy->state == FLAG_DROPPED) enemy_flag = "DROPPED";
        else if (enemy->state == FLAG_CARRIED) enemy_flag = "CARRIED";
        char score_buf[128];
        snprintf(score_buf, sizeof(score_buf), "CTFB BLUE %d  -  %d RED", local_state.team_scores[1], local_state.team_scores[0]);
        glColor3f(0.40f, 0.75f, 1.0f);
        draw_string(score_buf, 450, 682, 6);
        glColor3f(0.82f, 0.86f, 0.94f);
        draw_string("CTFB · FIRST TO 3", 520, 658, 4);
        char status_buf[160];
        snprintf(status_buf, sizeof(status_buf), "YOUR FLAG: %s   ENEMY FLAG: %s", my_flag, enemy_flag);
        draw_string(status_buf, 438, 638, 4);
        if (p->carried_flag_team_id >= 0) {
            glColor3f(1.0f, 0.85f, 0.2f);
            draw_string("YOU HAVE THE FLAG · FIRE = 80 DMG MELEE", 430, 618, 4);
        }
    }
    float x0 = 50.0f, x1 = vs0_art_direction_enabled ? 220.0f : 250.0f;
    float y_health0 = 50.0f, y_health1 = vs0_art_direction_enabled ? 66.0f : 70.0f;
    float y_shield0 = vs0_art_direction_enabled ? 72.0f : 80.0f, y_shield1 = vs0_art_direction_enabled ? 88.0f : 100.0f;
    glColor3f(0.18f, 0.10f, 0.10f); glRectf(x0, y_health0, x1, y_health1); glColor3f(0.82f, 0.24f, 0.24f);
    glRectf(x0, y_health0, x0 + (p->health * (x1 - x0) / 100.0f), y_health1);
    glColor3f(0.10f, 0.12f, 0.20f); glRectf(x0, y_shield0, x1, y_shield1); glColor3f(0.34f, 0.52f, 0.86f);
    glRectf(x0, y_shield0, x0 + (p->shield * (x1 - x0) / 100.0f), y_shield1);
    
    if (p->in_vehicle) {
        glColor3f(0.0f, 1.0f, 0.0f);
        draw_string(p->vehicle_type == VEH_HELICOPTER ? "HELI ONLINE" : "BUGGY ONLINE", 50, 112, vs0_art_direction_enabled ? 8 : 12);
        char style_buf[96];
        if (p->vehicle_type == VEH_HELICOPTER) {
            snprintf(style_buf, sizeof(style_buf), "HP:%d ALT:%.1f", p->health, p->y);
        } else {
            snprintf(style_buf, sizeof(style_buf), "F6 STYLE:%s F7 GLOW:%s F8 LIGHT:%s",
                     vehicle_style_enabled ? "ON" : "OFF",
                     vehicle_underglow_enabled ? "ON" : "OFF",
                     vehicle_worklights_enabled ? "ON" : "OFF");
        }
        glColor3f(0.95f, 0.55f, 0.1f);
        draw_string(style_buf, 50, 98, vs0_art_direction_enabled ? 4 : 6);
    }
    glColor3f(0.58f, 0.75f, 0.76f);
    draw_string(terrain_wireframe_debug ? "F6 TERRAIN WIRE:ON" : "F6 TERRAIN WIRE:OFF", 50, 24, vs0_art_direction_enabled ? 3 : 5);
    glColor3f(0.68f, 0.56f, 0.76f);
    draw_string(terrain_normals_debug ? "F7 TERRAIN NORMALS:ON" : "F7 TERRAIN NORMALS:OFF", 220, 24, vs0_art_direction_enabled ? 3 : 5);
    glColor3f(0.78f, 0.69f, 0.48f);
    draw_string(voxworld_points_debug ? "F11 SCENE DEBUG:ON" : "F11 SCENE DEBUG:OFF", 430, 24, vs0_art_direction_enabled ? 3 : 5);
    glColor3f(0.72f, 0.74f, 0.80f);
    draw_string(vs0_art_direction_enabled ? "F12 VS0 ART:ON" : "F12 VS0 ART:OFF", 650, 24, 3);

    if (p->current_weapon == WPN_KATANA) {
        char katana_buf[64];
        if (p->dash_timer > 0) {
            snprintf(katana_buf, sizeof(katana_buf), "KATANA DASHING");
        } else if (p->ability_cooldown == 0) {
            snprintf(katana_buf, sizeof(katana_buf), "E: BLADE DASH READY");
        } else {
            snprintf(katana_buf, sizeof(katana_buf), "BLADE DASH CD: %.1f", p->ability_cooldown / 60.0f);
        }
        glColor3f(0.0f, 0.85f, 1.0f);
        draw_string(katana_buf, 50, 132, vs0_art_direction_enabled ? 6 : 8);
    } else if (p->storm_charges > 0) {
        char storm_buf[32];
        sprintf(storm_buf, "STORM ARROWS: %d", p->storm_charges);
        glColor3f(1.0f, 0.2f, 0.2f);
        draw_string(storm_buf, 50, 132, vs0_art_direction_enabled ? 6 : 8);
    } else if (p->ability_cooldown == 0) {
        glColor3f(0.0f, 0.8f, 1.0f);
        draw_string("E: STORM ARROWS READY", 50, 132, vs0_art_direction_enabled ? 3 : 4);
    }
    
    float raw_speed = sqrtf(p->vx*p->vx + p->vz*p->vz);
    char vel_buf[32]; sprintf(vel_buf, "VEL: %.2f", raw_speed);
    glColor3f(vs0_art_direction_enabled ? 0.80f : 1.0f, vs0_art_direction_enabled ? 0.78f : 1.0f, vs0_art_direction_enabled ? 0.44f : 0.0f);
    draw_string(vel_buf, 1120, 50, vs0_art_direction_enabled ? 5 : 8); 
    draw_ammo_bars(p);

    glEnable(GL_DEPTH_TEST); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW); glPopMatrix();
}

static const char *scene_name_ui(int scene_id) {
    switch (scene_id) {
        case SCENE_GARAGE_OSAKA: return "GARAGE_OSAKA";
        case SCENE_STADIUM: return "STADIUM";
        case SCENE_VOXWORLD: return "VOXWORLD";
        case SCENE_DUST_COMPOUND: return "DUST_COMPOUND";
        case SCENE_OIL_TANKER: return "OIL_TANKER";
        case SCENE_POO_POO_ISLAND: return "POO_POO_ISLAND";
        case SCENE_STORY_CAVE: return "STORY_CAVE";
        default: return "UNKNOWN";
    }
}

typedef struct {
    int id;
    int kills;
    int deaths;
} ScoreRow;

static int score_row_cmp_desc(const void *a, const void *b) {
    const ScoreRow *ra = (const ScoreRow*)a;
    const ScoreRow *rb = (const ScoreRow*)b;
    if (ra->kills != rb->kills) return rb->kills - ra->kills;
    if (ra->deaths != rb->deaths) return ra->deaths - rb->deaths;
    return ra->id - rb->id;
}

static int mode_uses_team_scoreboard(int mode) {
    return mode == MODE_TDM || mode == MODE_TDMB || mode == MODE_TDMO || mode == MODE_CTF || mode == MODE_CTFB;
}

static int is_valid_scoreboard_team_id(int team_id) {
    return team_id == TDMB_BLUE_TEAM || team_id == TDMB_RED_TEAM;
}

static void scoreboard_team_totals(int mode, int *out_blue, int *out_red) {
    int blue = 0;
    int red = 0;
    if (mode == MODE_TDMB || mode == MODE_TDMO || mode == MODE_CTFB) {
        blue = local_state.team_scores[TDMB_BLUE_TEAM];
        red = local_state.team_scores[TDMB_RED_TEAM];
    } else {
        for (int i = 0; i < MAX_CLIENTS; i++) {
            PlayerState *p = &local_state.players[i];
            if (!p->active || !is_valid_scoreboard_team_id(p->team_id)) continue;
            if (p->team_id == TDMB_BLUE_TEAM) blue += p->kills;
            else if (p->team_id == TDMB_RED_TEAM) red += p->kills;
        }
    }
    if (out_blue) *out_blue = blue;
    if (out_red) *out_red = red;
}

static void draw_team_section_rows(const ScoreRow *rows, int row_count, PlayerState *self,
                                   float section_l, float section_r, float player_x,
                                   float kills_x, float deaths_x, float *row_y) {
    for (int i = 0; i < row_count; i++) {
        PlayerState *row_p = &local_state.players[rows[i].id];
        int is_self = (row_p == self);
        float y = *row_y;
        float row_top = y + 11.0f;
        float row_bottom = y - 15.0f;
        float stripe = (i % 2 == 0) ? 0.15f : 0.10f;
        if (is_self) glColor4f(0.78f, 0.63f, 0.16f, 0.48f);
        else glColor4f(0.12f, 0.16f, 0.22f, stripe);
        glBegin(GL_QUADS);
        glVertex2f(section_l, row_bottom); glVertex2f(section_r, row_bottom);
        glVertex2f(section_r, row_top); glVertex2f(section_l, row_top);
        glEnd();

        glColor3f(is_self ? 1.0f : 0.82f, is_self ? 0.95f : 0.84f, is_self ? 0.35f : 0.92f);
        char name_buf[64];
        snprintf(name_buf, sizeof(name_buf), "%s%02d%s",
                 row_p->is_bot ? "BOT-" : "P",
                 rows[i].id,
                 is_self ? " (YOU)" : "");
        draw_string(name_buf, player_x, y, 6);
        char score_buf[32];
        snprintf(score_buf, sizeof(score_buf), "%d", rows[i].kills);
        draw_string(score_buf, kills_x, y, 6);
        snprintf(score_buf, sizeof(score_buf), "%d", rows[i].deaths);
        draw_string(score_buf, deaths_x, y, 6);
        *row_y -= 34.0f;
    }
}

static void draw_tab_scoreboard(PlayerState *self) {
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    if (!keys[SDL_SCANCODE_TAB]) return;

    const int team_mode = mode_uses_team_scoreboard(local_state.game_mode);
    ScoreRow rows[MAX_CLIENTS];
    ScoreRow blue_rows[MAX_CLIENTS];
    ScoreRow red_rows[MAX_CLIENTS];
    int row_count = 0;
    int blue_count = 0;
    int red_count = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        PlayerState *p = &local_state.players[i];
        if (!p->active) continue;
        ScoreRow row = { i, p->kills, p->deaths };
        if (team_mode && is_valid_scoreboard_team_id(p->team_id)) {
            if (p->team_id == TDMB_BLUE_TEAM) blue_rows[blue_count++] = row;
            else red_rows[red_count++] = row;
        } else if (!team_mode) {
            rows[row_count++] = row;
        }
    }
    if (team_mode) {
        qsort(blue_rows, (size_t)blue_count, sizeof(blue_rows[0]), score_row_cmp_desc);
        qsort(red_rows, (size_t)red_count, sizeof(red_rows[0]), score_row_cmp_desc);
    } else {
        qsort(rows, (size_t)row_count, sizeof(rows[0]), score_row_cmp_desc);
    }

    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); gluOrtho2D(0, 1280, 0, 720);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();

    const float panel_l = 220.0f;
    const float panel_r = 1060.0f;
    const float panel_b = 110.0f;
    const float panel_t = 630.0f;
    const float player_x = 300.0f;
    const float kills_x = 860.0f;
    const float deaths_x = 960.0f;
    const float row_step = 34.0f;

    glColor4f(0.05f, 0.07f, 0.10f, 0.84f);
    glBegin(GL_QUADS);
    glVertex2f(panel_l, panel_b); glVertex2f(panel_r, panel_b); glVertex2f(panel_r, panel_t); glVertex2f(panel_l, panel_t);
    glEnd();

    glColor4f(0.24f, 0.33f, 0.42f, 0.85f);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(panel_l + 1.0f, panel_b + 1.0f); glVertex2f(panel_r - 1.0f, panel_b + 1.0f);
    glVertex2f(panel_r - 1.0f, panel_t - 1.0f); glVertex2f(panel_l + 1.0f, panel_t - 1.0f);
    glEnd();
    glColor4f(0.02f, 0.04f, 0.07f, 0.85f);
    glLineWidth(1.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(panel_l + 5.0f, panel_b + 5.0f); glVertex2f(panel_r - 5.0f, panel_b + 5.0f);
    glVertex2f(panel_r - 5.0f, panel_t - 5.0f); glVertex2f(panel_l + 5.0f, panel_t - 5.0f);
    glEnd();

    if (team_mode) {
        int blue_total = 0, red_total = 0;
        scoreboard_team_totals(local_state.game_mode, &blue_total, &red_total);
        glColor3f(0.95f, 0.95f, 0.2f);
        draw_string("TEAM DEATHMATCH", 440, 590, 9);
        char totals[96];
        snprintf(totals, sizeof(totals), "BLUE %d  |  RED %d", blue_total, red_total);
        glColor3f(0.78f, 0.90f, 1.0f);
        draw_string(totals, 468, 560, 7);

        const float section_l = panel_l + 24.0f;
        const float section_r = panel_r - 24.0f;
        const float blue_header_y = 520.0f;
        const float red_header_y = 320.0f;
        const float section_player_x = 340.0f;
        const float section_kills_x = 860.0f;
        const float section_deaths_x = 960.0f;

        glColor3f(0.40f, 0.78f, 1.0f);
        draw_string("BLUE TEAM - NINJAS", 300, blue_header_y, 6);
        char blue_score[32];
        snprintf(blue_score, sizeof(blue_score), "%d", blue_total);
        draw_string(blue_score, 1000, blue_header_y, 6);
        glColor3f(0.72f, 0.90f, 1.0f);
        draw_string("PLAYER", section_player_x, blue_header_y - 30.0f, 5);
        draw_string("K", section_kills_x, blue_header_y - 30.0f, 5);
        draw_string("D", section_deaths_x, blue_header_y - 30.0f, 5);

        glColor4f(0.35f, 0.64f, 0.92f, 0.42f);
        glBegin(GL_LINES);
        glVertex2f(section_l, blue_header_y - 38.0f); glVertex2f(section_r, blue_header_y - 38.0f);
        glEnd();
        float blue_row_y = blue_header_y - 62.0f;
        int blue_visible = (int)((blue_row_y - (red_header_y + 34.0f)) / row_step) + 1;
        if (blue_visible < 0) blue_visible = 0;
        if (blue_visible > blue_count) blue_visible = blue_count;
        draw_team_section_rows(blue_rows, blue_visible, self, section_l, section_r,
                               section_player_x, section_kills_x, section_deaths_x, &blue_row_y);

        glColor3f(1.0f, 0.45f, 0.32f);
        draw_string("RED TEAM - PIRATES", 300, red_header_y, 6);
        char red_score[32];
        snprintf(red_score, sizeof(red_score), "%d", red_total);
        draw_string(red_score, 1000, red_header_y, 6);
        glColor3f(1.0f, 0.80f, 0.72f);
        draw_string("PLAYER", section_player_x, red_header_y - 30.0f, 5);
        draw_string("K", section_kills_x, red_header_y - 30.0f, 5);
        draw_string("D", section_deaths_x, red_header_y - 30.0f, 5);

        glColor4f(0.92f, 0.50f, 0.40f, 0.42f);
        glBegin(GL_LINES);
        glVertex2f(section_l, red_header_y - 38.0f); glVertex2f(section_r, red_header_y - 38.0f);
        glEnd();
        float red_row_y = red_header_y - 62.0f;
        int red_visible = (int)((red_row_y - (panel_b + 18.0f)) / row_step) + 1;
        if (red_visible < 0) red_visible = 0;
        if (red_visible > red_count) red_visible = red_count;
        draw_team_section_rows(red_rows, red_visible, self, section_l, section_r,
                               section_player_x, section_kills_x, section_deaths_x, &red_row_y);
    } else {
        char header[128];
        snprintf(header, sizeof(header), "SCOREBOARD - %s", scene_name_ui(local_state.scene_id));
        glColor3f(0.95f, 0.95f, 0.2f);
        draw_string(header, 290, 590, 9);
        glColor3f(0.72f, 0.90f, 1.0f);
        draw_string("PLAYER", player_x, 552, 6);
        draw_string("K", kills_x, 552, 6);
        draw_string("D", deaths_x, 552, 6);

        glColor4f(0.48f, 0.62f, 0.78f, 0.50f);
        glBegin(GL_LINES);
        glVertex2f(panel_l + 34.0f, 544.0f); glVertex2f(panel_r - 34.0f, 544.0f);
        glEnd();

        float row_start_y = 508.0f;
        int visible_rows = (int)((row_start_y - (panel_b + 18.0f)) / row_step) + 1;
        if (visible_rows < 0) visible_rows = 0;
        if (visible_rows > row_count) visible_rows = row_count;
        for (int i = 0; i < visible_rows; i++) {
            PlayerState *row_p = &local_state.players[rows[i].id];
            int is_self = (row_p == self);
            float y = row_start_y - (float)i * row_step;
            float row_top = y + 11.0f;
            float row_bottom = y - 15.0f;
            float stripe = (i % 2 == 0) ? 0.15f : 0.10f;
            if (is_self) glColor4f(0.78f, 0.63f, 0.16f, 0.48f);
            else glColor4f(0.12f, 0.16f, 0.22f, stripe);
            glBegin(GL_QUADS);
            glVertex2f(panel_l + 24.0f, row_bottom); glVertex2f(panel_r - 24.0f, row_bottom);
            glVertex2f(panel_r - 24.0f, row_top); glVertex2f(panel_l + 24.0f, row_top);
            glEnd();

            glColor3f(is_self ? 1.0f : 0.82f, is_self ? 0.95f : 0.84f, is_self ? 0.35f : 0.92f);
            char name_buf[64];
            snprintf(name_buf, sizeof(name_buf), "%s%02d", is_self ? "YOU-" : "P", rows[i].id);
            draw_string(name_buf, player_x, y, 6);
            char score_buf[64];
            snprintf(score_buf, sizeof(score_buf), "%d", rows[i].kills);
            draw_string(score_buf, kills_x, y, 6);
            snprintf(score_buf, sizeof(score_buf), "%d", rows[i].deaths);
            draw_string(score_buf, deaths_x, y, 6);
        }
    }

    glEnable(GL_DEPTH_TEST); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW); glPopMatrix();
}

static int target_in_view(PlayerState *p, float tx, float ty, float tz, float max_dist, float min_dot) {
    float rad_yaw = cam_yaw * 0.0174533f;
    float rad_pitch = cam_pitch * 0.0174533f;
    float fx = sinf(rad_yaw) * cosf(rad_pitch);
    float fy = sinf(rad_pitch);
    float fz = -cosf(rad_yaw) * cosf(rad_pitch);

    float ox = p->x;
    float oy = p->y + EYE_HEIGHT;
    float oz = p->z;

    float dx = tx - ox;
    float dy = ty - oy;
    float dz = tz - oz;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    if (dist > max_dist || dist <= 0.001f) return 0;
    dx /= dist; dy /= dist; dz /= dist;
    float dot = fx * dx + fy * dy + fz * dz;
    return dot >= min_dot;
}

static void draw_garage_portal_frame() {
    if (!scene_portal_active(local_state.scene_id)) return;
    float px = 0.0f, py = 0.0f, pz = 0.0f, pr = 0.0f;
    scene_portal_info(local_state.scene_id, &px, &py, &pz, &pr);

    glPushMatrix();
    glTranslatef(px, py, pz);
    glColor3f(0.2f, 0.9f, 1.0f);
    glLineWidth(3.0f);
    glBegin(GL_LINE_LOOP);
    glVertex3f(-pr, -2.0f, 0.0f);
    glVertex3f(pr, -2.0f, 0.0f);
    glVertex3f(pr, 6.0f, 0.0f);
    glVertex3f(-pr, 6.0f, 0.0f);
    glEnd();
    glPopMatrix();

    if (local_state.scene_id == SCENE_GARAGE_OSAKA) {
        glPushMatrix();
        glTranslatef(GARAGE_VOX_PORTAL_X, GARAGE_VOX_PORTAL_Y, GARAGE_VOX_PORTAL_Z);
        glColor3f(0.95f, 0.2f, 0.95f);
        glLineWidth(3.0f);
        glBegin(GL_LINE_LOOP);
        glVertex3f(-GARAGE_VOX_PORTAL_RADIUS, -2.0f, 0.0f);
        glVertex3f(GARAGE_VOX_PORTAL_RADIUS, -2.0f, 0.0f);
        glVertex3f(GARAGE_VOX_PORTAL_RADIUS, 6.0f, 0.0f);
        glVertex3f(-GARAGE_VOX_PORTAL_RADIUS, 6.0f, 0.0f);
        glEnd();
        glPopMatrix();

        glPushMatrix();
        glTranslatef(GARAGE_TANKER_PORTAL_X, GARAGE_TANKER_PORTAL_Y, GARAGE_TANKER_PORTAL_Z);
        glColor3f(1.0f, 0.55f, 0.15f);
        glLineWidth(3.0f);
        glBegin(GL_LINE_LOOP);
        glVertex3f(-GARAGE_TANKER_PORTAL_RADIUS, -2.0f, 0.0f);
        glVertex3f(GARAGE_TANKER_PORTAL_RADIUS, -2.0f, 0.0f);
        glVertex3f(GARAGE_TANKER_PORTAL_RADIUS, 6.0f, 0.0f);
        glVertex3f(-GARAGE_TANKER_PORTAL_RADIUS, 6.0f, 0.0f);
        glEnd();
        glPopMatrix();

        glPushMatrix();
        glTranslatef(GARAGE_DUST_PORTAL_X, GARAGE_DUST_PORTAL_Y, GARAGE_DUST_PORTAL_Z);
        glColor3f(1.0f, 0.82f, 0.35f);
        glLineWidth(3.0f);
        glBegin(GL_LINE_LOOP);
        glVertex3f(-GARAGE_DUST_PORTAL_RADIUS, -2.0f, 0.0f);
        glVertex3f(GARAGE_DUST_PORTAL_RADIUS, -2.0f, 0.0f);
        glVertex3f(GARAGE_DUST_PORTAL_RADIUS, 6.0f, 0.0f);
        glVertex3f(-GARAGE_DUST_PORTAL_RADIUS, 6.0f, 0.0f);
        glEnd();
        glPopMatrix();

        glPushMatrix();
        glTranslatef(GARAGE_POO_POO_PORTAL_X, GARAGE_POO_POO_PORTAL_Y, GARAGE_POO_POO_PORTAL_Z);
        glColor3f(0.30f, 1.0f, 0.72f);
        glLineWidth(3.0f);
        glBegin(GL_LINE_LOOP);
        glVertex3f(-GARAGE_POO_POO_PORTAL_RADIUS, -2.0f, 0.0f);
        glVertex3f(GARAGE_POO_POO_PORTAL_RADIUS, -2.0f, 0.0f);
        glVertex3f(GARAGE_POO_POO_PORTAL_RADIUS, 6.0f, 0.0f);
        glVertex3f(-GARAGE_POO_POO_PORTAL_RADIUS, 6.0f, 0.0f);
        glEnd();
        glPopMatrix();
    }
}

static void draw_garage_vehicle_pads() {
    int pad_count = 0;
    const VehiclePad *pads = scene_vehicle_pads(local_state.scene_id, &pad_count);
    if (!pads || pad_count == 0) return;
    for (int i = 0; i < pad_count; i++) {
        glPushMatrix();
        glTranslatef(pads[i].x, pads[i].y + 0.1f, pads[i].z);
        glColor3f(0.4f, 0.8f, 0.4f);
        glBegin(GL_LINE_LOOP);
        glVertex3f(-3.0f, 0.0f, -3.0f);
        glVertex3f(3.0f, 0.0f, -3.0f);
        glVertex3f(3.0f, 0.0f, 3.0f);
        glVertex3f(-3.0f, 0.0f, 3.0f);
        glEnd();
        glPopMatrix();
    }
}

static void draw_garage_overlay(PlayerState *p) {
    if (local_state.scene_id != SCENE_GARAGE_OSAKA) return;

    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glOrtho(0, 1280, 0, 720, -1, 1);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();

    glColor3f(0.2f, 1.0f, 1.0f);
    draw_string("OSAKA GARAGE", 40, 670, 10);
    glColor3f(0.9f, 0.9f, 0.9f);
    draw_string("PORTAL -> STADIUM", 40, 640, 6);
    draw_string("PORTAL -> VOXWORLD TERRAIN", 40, 620, 6);
    draw_string("PORTAL -> OIL TANKER", 40, 600, 6);
    draw_string("PORTAL -> DUST COMPOUND", 40, 580, 6);
    draw_string("PORTAL -> POO POO ISLAND", 40, 560, 6);

    int pad_count = 0;
    const VehiclePad *pads = scene_vehicle_pads(local_state.scene_id, &pad_count);
    float list_y = 600.0f;
    for (int i = 0; i < pad_count; i++) {
        int occupied = 0;
        for (int bi = 0; bi < MAX_BUGGIES; bi++) {
            BuggyState *b = &local_state.buggies[bi];
            if (!b->active || b->scene_id != local_state.scene_id) continue;
            float dx = b->x - pads[i].x;
            float dz = b->z - pads[i].z;
            if ((dx * dx + dz * dz) < 16.0f) {
                occupied = 1;
                break;
            }
        }
        char line[128];
        snprintf(line, sizeof(line), "%s [%s]", pads[i].label, occupied ? "OCCUPIED" : "IDLE");
        glColor3f(occupied ? 1.0f : 0.5f, occupied ? 0.6f : 0.9f, 0.6f);
        draw_string(line, 40, list_y, 6);
        list_y -= 20.0f;
    }

    float portal_x = 0.0f, portal_y = 0.0f, portal_z = 0.0f, portal_r = 0.0f;
    scene_portal_info(local_state.scene_id, &portal_x, &portal_y, &portal_z, &portal_r);
    int portal_target = target_in_view(p, portal_x, portal_y, portal_z, 30.0f, 0.75f);
    int vox_portal_target = target_in_view(p, GARAGE_VOX_PORTAL_X, GARAGE_VOX_PORTAL_Y, GARAGE_VOX_PORTAL_Z, 30.0f, 0.75f);
    int tanker_portal_target = target_in_view(p, GARAGE_TANKER_PORTAL_X, GARAGE_TANKER_PORTAL_Y, GARAGE_TANKER_PORTAL_Z, 30.0f, 0.75f);
    int dust_portal_target = target_in_view(p, GARAGE_DUST_PORTAL_X, GARAGE_DUST_PORTAL_Y, GARAGE_DUST_PORTAL_Z, 30.0f, 0.75f);
    int poo_poo_portal_target = target_in_view(p, GARAGE_POO_POO_PORTAL_X, GARAGE_POO_POO_PORTAL_Y, GARAGE_POO_POO_PORTAL_Z, 30.0f, 0.75f);
    int pad_target = 0;
    int heli_target = 0;
    if (scene_near_vehicle_pad(local_state.scene_id, p->x, p->z, 12.0f, NULL)) {
        int pad_idx = -1;
        if (scene_near_vehicle_pad(local_state.scene_id, p->x, p->z, 12.0f, &pad_idx)) {
            pad_target = target_in_view(p, pads[pad_idx].x, pads[pad_idx].y + 1.0f, pads[pad_idx].z, 20.0f, 0.7f);
        }
    }

    for (int hi = 0; hi < MAX_HELICOPTERS; hi++) {
        HelicopterState *h = &local_state.helicopters[hi];
        if (!h->active || h->scene_id != p->scene_id || h->occupant_player_id >= 0) continue;
        float dx = h->x - p->x, dy = h->y - p->y, dz = h->z - p->z;
        if ((dx * dx + dy * dy + dz * dz) <= (g_heli_tuning.enter_radius * g_heli_tuning.enter_radius)) {
            if (target_in_view(p, h->x, h->y + 1.0f, h->z, 25.0f, 0.65f)) {
                heli_target = 1;
            }
        }
    }

    glColor3f(1.0f, 1.0f, 0.0f);
    if (portal_target || vox_portal_target || tanker_portal_target || dust_portal_target || poo_poo_portal_target) {
        draw_string("TRAVEL", 600, 350, 8);
    } else if (heli_target || (p->in_vehicle && p->vehicle_type == VEH_HELICOPTER)) {
        draw_string(p->in_vehicle ? "PRESS F TO EXIT HELICOPTER" : "PRESS F TO ENTER HELICOPTER", 460, 350, 8);
    } else if (pad_target) {
        draw_string(p->in_vehicle ? "EXIT VEHICLE" : "ENTER VEHICLE", 560, 350, 8);
    }

    glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
}

void draw_projectiles() {
    glDisable(GL_TEXTURE_2D);
    glPointSize(6.0f);
    glBegin(GL_POINTS);
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        Projectile *p = &local_state.projectiles[i];
        if (!p->active) continue;
        if (p->scene_id != local_state.scene_id) continue;
        if (p->bounces_left > 0) glColor3f(1.0f, 0.4f, 0.1f);
        else glColor3f(1.0f, 0.0f, 0.0f);
        glVertex3f(p->x, p->y, p->z);
    }
    glEnd();
}

static void client_apply_scene_id(int scene_id, unsigned int now_ms) {
    if (scene_id < 0) return;
    if (local_state.scene_id != scene_id) {
        local_state.scene_id = scene_id;
        phys_set_scene(scene_id);
        travel_overlay_until_ms = now_ms + 500;
        for (int i = 0; i < MAX_PROJECTILES; i++) {
            local_state.projectiles[i].active = 0;
        }
        for (int i = 0; i < MAX_HELICOPTERS; i++) {
            local_state.helicopters[i].active = 0;
            local_state.helicopters[i].occupant_player_id = -1;
        }
    }
}

static void draw_travel_overlay() {
    unsigned int now_ms = SDL_GetTicks();
    if (travel_overlay_until_ms <= now_ms) return;
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glOrtho(0, 1280, 0, 720, -1, 1);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();

    glColor3f(0.9f, 0.9f, 0.2f);
    draw_string("TRAVELING...", 520, 360, 8);

    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
    glEnable(GL_DEPTH_TEST);
}


static void draw_tdmb_match_over_overlay(void) {
    if ((local_state.game_mode != MODE_TDMB && local_state.game_mode != MODE_TDMO && local_state.game_mode != MODE_CTFB) || !local_state.match_over) return;
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); gluOrtho2D(0, 1280, 0, 720);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glColor4f(0.0f, 0.0f, 0.0f, 0.62f);
    glBegin(GL_QUADS);
    glVertex2f(220, 210); glVertex2f(1060, 210); glVertex2f(1060, 510); glVertex2f(220, 510);
    glEnd();
    const int blue_won = (local_state.winning_team == 1);
    glColor3f(blue_won ? 0.35f : 1.0f, blue_won ? 0.75f : 0.32f, blue_won ? 1.0f : 0.28f);
    draw_string(blue_won ? "BLUE TEAM WINS" : "RED TEAM WINS", 460, 430, 10);
    glColor3f(0.95f, 0.95f, 0.95f);
    draw_string(local_state.game_mode == MODE_CTFB ? "R: RESTART CTFB" : "R: RESTART TDMB", 500, 340, 6);
    draw_string("ESC: BACK TO MENU", 486, 300, 6);
    glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
}

static void draw_story_overlay(void) {
    if (local_state.game_mode != MODE_STORY) return;
    if (local_state.story_phase != STORY_PHASE_COMPLETE) return;
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); gluOrtho2D(0, 1280, 0, 720);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glColor4f(0.0f, 0.0f, 0.0f, 0.50f);
    glBegin(GL_QUADS);
    glVertex2f(280, 250); glVertex2f(1000, 250); glVertex2f(1000, 470); glVertex2f(280, 470);
    glEnd();
    glColor3f(0.92f, 1.0f, 0.45f);
    draw_string("CAVE CLEARED", 485, 390, 10);
    glColor3f(0.95f, 0.95f, 0.95f);
    draw_string("STORY COMPLETE", 478, 340, 7);
    draw_string("ESC: BACK TO MENU", 474, 300, 6);
    glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
}

void draw_scene(PlayerState *render_p) {
    if (vs0_art_direction_enabled) glClearColor(0.18f, 0.25f, 0.36f, 1.0f);
    else glClearColor(0.02f, 0.02f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); glLoadIdentity();
    local_state.scene_id = render_p->scene_id;
    phys_set_scene(render_p->scene_id);
    unsigned int now_ms = SDL_GetTicks();
    int local_dead = (render_p->state == STATE_DEAD);
    float death_target = local_dead ? 1.0f : 0.0f;
    death_cam_blend += (death_target - death_cam_blend) * 0.18f;
    if (death_cam_blend < 0.001f) death_cam_blend = 0.0f;
    if (death_cam_blend > 0.999f) death_cam_blend = 1.0f;
    float base_cam_y = (render_p->crouching ? 2.5f : EYE_HEIGHT);
    float cam_y = lerpf(base_cam_y, 4.5f, death_cam_blend);
    float cx = 0.0f, cz = 0.0f;
    BuggyState *cam_buggy = NULL;
    if (render_p->in_vehicle && render_p->vehicle_type == VEH_BUGGY) {
        for (int bi = 0; bi < MAX_BUGGIES; bi++) {
            if (local_state.buggies[bi].active && local_state.buggies[bi].occupant_player_id == render_p->id) {
                cam_buggy = &local_state.buggies[bi];
                break;
            }
        }
    }
    static float heli_cam_x = 0.0f, heli_cam_y = 0.0f, heli_cam_z = 0.0f;
    int story_cutscene = (local_state.game_mode == MODE_STORY && local_state.story_phase == STORY_PHASE_CUTSCENE);
    if (render_p->in_vehicle && render_p->vehicle_type == VEH_HELICOPTER) {
        HelicopterState *hh = NULL;
        for (int i = 0; i < MAX_HELICOPTERS; i++) {
            if (local_state.helicopters[i].active && local_state.helicopters[i].occupant_player_id == render_p->id) {
                hh = &local_state.helicopters[i]; break;
            }
        }
        if (hh) {
            cam_yaw = hh->yaw;
            float rr = -cam_yaw * 0.01745f;
            float tx = render_p->x - sinf(rr) * 18.0f;
            float ty = render_p->y + 7.0f;
            float tz = render_p->z - cosf(rr) * 18.0f;
            if (heli_cam_x == 0.0f && heli_cam_y == 0.0f && heli_cam_z == 0.0f) {
                heli_cam_x = tx; heli_cam_y = ty; heli_cam_z = tz;
            }
            heli_cam_x += (tx - heli_cam_x) * 0.14f;
            heli_cam_y += (ty - heli_cam_y) * 0.14f;
            heli_cam_z += (tz - heli_cam_z) * 0.14f;
            glRotatef(-18.0f, 1, 0, 0);
            glRotatef(-cam_yaw, 0, 1, 0);
            glTranslatef(-heli_cam_x, -heli_cam_y, -heli_cam_z);
        }
    } else {
        float follow_yaw = cam_buggy ? cam_buggy->yaw : cam_yaw;
        float cam_z_off = cam_buggy ? 16.5f : (render_p->in_vehicle ? 10.0f : lerpf(0.0f, 8.5f, death_cam_blend));
        float rad = -follow_yaw * 0.01745f;
        cx = sinf(rad) * cam_z_off;
        cz = cosf(rad) * cam_z_off;
        if (cam_buggy) {
            cam_y = 5.2f;
            cam_yaw = follow_yaw;
        }
    }
    
    float reconcile_x = 0.0f;
    float reconcile_y = 0.0f;
    float reconcile_z = 0.0f;
    if (app_state == STATE_GAME_NET && my_client_id > 0 && my_client_id < MAX_CLIENTS && render_p == &local_state.players[my_client_id]) {
        reconcile_x = reconcile_corr_x;
        reconcile_y = reconcile_corr_y;
        reconcile_z = reconcile_corr_z;
    }

    if (story_cutscene) {
        float rr = -render_p->yaw * 0.01745f;
        float fx = sinf(rr);
        float fz = -cosf(rr);
        float cam_x = render_p->x + fx * 18.0f;
        float cam_y_cut = render_p->y + 5.0f;
        float cam_z = render_p->z + fz * 18.0f;
        float look_dx = render_p->x - cam_x;
        float look_dy = (render_p->y + 3.0f) - cam_y_cut;
        float look_dz = render_p->z - cam_z;
        float look_yaw = atan2f(look_dx, -look_dz) * 57.29578f;
        float look_pitch = atan2f(look_dy, sqrtf(look_dx * look_dx + look_dz * look_dz)) * 57.29578f;
        glRotatef(-look_pitch, 1, 0, 0);
        glRotatef(-look_yaw, 0, 1, 0);
        glTranslatef(-cam_x, -cam_y_cut, -cam_z);
    } else if (!(render_p->in_vehicle && render_p->vehicle_type == VEH_HELICOPTER)) {
        float draw_cam_pitch = lerpf(cam_pitch, -14.0f, death_cam_blend);
        glRotatef(-draw_cam_pitch, 1, 0, 0); glRotatef(-cam_yaw, 0, 1, 0);
        glTranslatef(-((render_p->x + reconcile_x) - cx), -((render_p->y + reconcile_y) + cam_y), -((render_p->z + reconcile_z) - cz));
    }

    {
        float sky_cam_x = (render_p->x + reconcile_x) - cx;
        float sky_cam_y = (render_p->y + reconcile_y) + cam_y;
        float sky_cam_z = (render_p->z + reconcile_z) - cz;
        if (story_cutscene) {
            float rr = -render_p->yaw * 0.01745f;
            float fx = sinf(rr);
            float fz = -cosf(rr);
            sky_cam_x = render_p->x + fx * 18.0f;
            sky_cam_y = render_p->y + 5.0f;
            sky_cam_z = render_p->z + fz * 18.0f;
        }
        if (render_p->in_vehicle && render_p->vehicle_type == VEH_HELICOPTER) {
            sky_cam_x = heli_cam_x;
            sky_cam_y = heli_cam_y;
            sky_cam_z = heli_cam_z;
        }
        /* draw_sky call here keeps sky rotation-locked to the camera, but camera-centered
           in world space so the background feels infinitely distant (no translation parallax). */
        retro_sky_draw(&g_retro_sky, sky_cam_x, sky_cam_y, sky_cam_z, now_ms * 0.001f);
    }

    RetroLightingState world_lighting;
    retro_lighting_eval(now_ms * 0.001f, g_world_lighting_preset, &world_lighting);
    retro_tune_world_fog(&world_lighting, local_state.scene_id);
    
    draw_grid(); 
    update_and_draw_trails();
    draw_terrain(&world_lighting);
    draw_voxworld_grass_overlay(&world_lighting, render_p);
    draw_voxworld_bushes();
    draw_map(&world_lighting);
    draw_team_map_markers(local_state.scene_id, local_state.game_mode);
    draw_garage_vehicle_pads();
    draw_garage_portal_frame();
    for (int bi = 0; bi < MAX_BUGGIES; bi++) {
        BuggyState *b = &local_state.buggies[bi];
        if (!b->active || b->scene_id != render_p->scene_id) continue;
        draw_buggy_entity(b);
    }
    for (int hi = 0; hi < MAX_HELICOPTERS; hi++) {
        HelicopterState *h = &local_state.helicopters[hi];
        if (!h->active || h->scene_id != render_p->scene_id) continue;
        draw_helicopter_model(h);
    }
    draw_projectiles();
    if (story_cutscene || (render_p->in_vehicle && render_p->vehicle_type != VEH_BUGGY)) draw_player_3rd(render_p);
    for(int i=0; i<MAX_CLIENTS; i++) {
        PlayerState *p = &local_state.players[i];
        if (!p->active || p->scene_id != render_p->scene_id) continue;
        if (p == render_p) continue;
        draw_player_3rd(p);
    }
    if (!story_cutscene) {
        draw_weapon_p(render_p);
        draw_hud(render_p);
    }
    draw_garage_overlay(render_p); draw_tab_scoreboard(render_p);
    draw_travel_overlay();
    draw_tdmb_match_over_overlay();
    draw_story_overlay();
}

typedef struct {
    float title_x;
    float title_y;
    float menu_x;
    float menu_y;
    float column_w;
    float row_gap;
    float icon_size;
    float text_x;
    float text_y;
    float footer_x;
    float footer_y;
} LobbyLayout;

static const LobbyLayout LOBBY_LAYOUT = {
    360.0f, 640.0f, 170.0f, 450.0f, 280.0f, 105.0f, 70.0f, 88.0f, 38.0f, 300.0f, 36.0f
};

static int lobby_grid_columns(int menu_count) {
    (void)menu_count;
    return 2;
}

static void lobby_button_pos(int index, int menu_count, const LobbyLayout *layout, float *x, float *y) {
    int cols = lobby_grid_columns(menu_count);
    int col = index % cols;
    int row = index / cols;
    *x = layout->menu_x + layout->column_w * (float)col;
    *y = layout->menu_y - layout->row_gap * (float)row;
}

/* Keep grid navigation intuitive for uneven row counts by snapping to nearest valid cell. */
static int lobby_nav_move(int selection, int menu_count, int dx, int dy) {
    if (menu_count <= 0) return 0;
    int cols = lobby_grid_columns(menu_count);
    int rows = (menu_count + cols - 1) / cols;
    int col = selection % cols;
    int row = selection / cols;

    if (dx != 0) {
        int next_col = (col + dx + cols) % cols;
        int idx = row * cols + next_col;
        if (idx >= menu_count) idx = row * cols;
        if (idx >= menu_count) idx = menu_count - 1;
        return idx;
    }
    if (dy != 0) {
        int next_row = (row + dy + rows) % rows;
        int idx = next_row * cols + col;
        if (idx >= menu_count) idx = menu_count - 1;
        return idx;
    }
    return selection;
}

static void draw_lobby_buttons(int menu_count, const LobbyLayout *layout) {
    const float colors[][3] = {
        {0.1f, 0.75f, 0.2f},  // green (join)
        {0.1f, 0.45f, 0.95f}, // blue
        {0.75f, 0.2f, 0.75f}, // magenta
        {0.0f, 0.75f, 0.75f}, // teal
        {0.9f, 0.75f, 0.1f},  // yellow
        {0.25f, 0.7f, 0.3f},  // green 2
        {0.85f, 0.2f, 0.2f},  // red
        {0.2f, 0.6f, 0.6f}    // light teal
    };
    int color_count = (int)(sizeof(colors) / sizeof(colors[0]));

    for (int i = 0; i < menu_count; i++) {
        float x = 0.0f, y = 0.0f;
        lobby_button_pos(i, menu_count, layout, &x, &y);
        const float *c = colors[i % color_count];
        glColor3f(c[0], c[1], c[2]);
        glRectf(x, y, x + layout->icon_size, y + layout->icon_size);

        if (i == lobby_selection) {
            glColor3f(1.0f, 1.0f, 1.0f);
            glLineWidth(3.0f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(x, y);
            glVertex2f(x + layout->icon_size, y);
            glVertex2f(x + layout->icon_size, y + layout->icon_size);
            glVertex2f(x, y + layout->icon_size);
            glEnd();
        }

        glColor3f(0.0f, 0.0f, 0.0f);
        draw_string(lobby_menu_label(i), x + layout->text_x + 2.0f, y + layout->text_y - 2.0f, 5);
        glColor3f(0.98f, 0.98f, 1.0f);
        draw_string(lobby_menu_label(i), x + layout->text_x, y + layout->text_y, 5);
        if (ui_edit_index == i) {
            glColor3f(0.95f, 0.9f, 0.2f);
            draw_string(ui_edit_buffer, x + layout->text_x, y + layout->icon_size * 0.2f, 5);
        }
    }
}

static int lobby_hit_test(float mx, float my, int menu_count, const LobbyLayout *layout) {
    for (int i = 0; i < menu_count; i++) {
        float x = 0.0f, y = 0.0f;
        lobby_button_pos(i, menu_count, layout, &x, &y);
        if (mx >= x && mx <= x + layout->icon_size && my >= y && my <= y + layout->icon_size) {
            return i;
        }
    }
    return -1;
}

static int skin_menu_visible_count(void) {
    return 4;
}

static int skin_menu_row_count(void) {
    return SKIN_COUNT;
}

static int skin_menu_visible_skin_count(void) {
    int visible_skin_rows = skin_menu_visible_count() - 1;
    return (visible_skin_rows > 0) ? visible_skin_rows : 1;
}

static int skin_menu_scroll_max(void) {
    int max_scroll = skin_menu_row_count() - skin_menu_visible_skin_count();
    return (max_scroll > 0) ? max_scroll : 0;
}

static void ensure_skin_selection_visible(void) {
    int max_scroll = skin_menu_scroll_max();
    if (skin_menu_selection >= 0 && skin_menu_selection < skin_menu_row_count()) {
        int visible_skin_count = skin_menu_visible_skin_count();
        if (skin_menu_selection < skin_menu_scroll) {
            skin_menu_scroll = skin_menu_selection;
        } else if (skin_menu_selection >= skin_menu_scroll + visible_skin_count) {
            skin_menu_scroll = skin_menu_selection - visible_skin_count + 1;
        }
    }
    if (skin_menu_scroll < 0) skin_menu_scroll = 0;
    if (skin_menu_scroll > max_scroll) skin_menu_scroll = max_scroll;
}

static int skin_hit_test_scroll_rows(float mx, float my, float base_x, float base_y, float w, float h, float gap) {
    int visible_skin_count = skin_menu_visible_skin_count();
    for (int row = 0; row < visible_skin_count; row++) {
        int i = skin_menu_scroll + row;
        if (i >= skin_menu_row_count()) break;
        float y = base_y - gap * row;
        if (mx >= base_x && mx <= base_x + w && my >= y && my <= y + h) return i;
    }
    return -1;
}

static int skin_hit_test_back_row(float mx, float my, float base_x, float base_y, float w, float h, float gap) {
    float y = base_y - gap * skin_menu_visible_skin_count();
    if (mx >= base_x && mx <= base_x + w && my >= y && my <= y + h) {
        return SKIN_MENU_BACK;
    }
    return -2;
}

static void draw_skin_chooser_overlay() {
    float panel_x = 770.0f;
    float panel_y = 545.0f;
    float panel_w = 330.0f;
    float panel_h = 275.0f;
    float item_h = 50.0f;
    float item_gap = 60.0f;
    float item_x = panel_x + 20.0f;
    float item_w = panel_w - 40.0f;
    float item_top = panel_y - 90.0f;

    glColor4f(0.05f, 0.08f, 0.12f, 0.92f);
    glBegin(GL_QUADS);
    glVertex2f(panel_x, panel_y);
    glVertex2f(panel_x + panel_w, panel_y);
    glVertex2f(panel_x + panel_w, panel_y - panel_h);
    glVertex2f(panel_x, panel_y - panel_h);
    glEnd();

    glColor3f(0.42f, 0.78f, 0.92f);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(panel_x, panel_y);
    glVertex2f(panel_x + panel_w, panel_y);
    glVertex2f(panel_x + panel_w, panel_y - panel_h);
    glVertex2f(panel_x, panel_y - panel_h);
    glEnd();

    glColor3f(0.85f, 0.95f, 1.0f);
    draw_string("CHOOSE SKIN", panel_x + 30.0f, panel_y - 30.0f, 5);

    int visible_skin_count = skin_menu_visible_skin_count();
    int entry_count = skin_menu_row_count();
    int max_scroll = skin_menu_scroll_max();
    if (skin_menu_scroll > max_scroll) skin_menu_scroll = max_scroll;
    if (skin_menu_scroll < 0) skin_menu_scroll = 0;
    ensure_skin_selection_visible();

    for (int row = 0; row < visible_skin_count; row++) {
        int i = skin_menu_scroll + row;
        if (i >= entry_count) break;
        float y = item_top - item_gap * row;
        int is_active = (clamp_skin_id(g_selected_skin) == i);
        int is_cursor = (skin_menu_selection == i);
        glColor3f(is_active ? 0.35f : 0.16f, is_active ? 0.65f : 0.22f, is_active ? 0.88f : 0.28f);
        glRectf(item_x, y, item_x + item_w, y + item_h);
        if (is_cursor) {
            glColor3f(0.95f, 0.95f, 0.95f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(item_x, y);
            glVertex2f(item_x + item_w, y);
            glVertex2f(item_x + item_w, y + item_h);
            glVertex2f(item_x, y + item_h);
            glEnd();
        }
        glColor3f(0.05f, 0.05f, 0.06f);
        draw_string(SKIN_LABELS[i], item_x + 12.0f, y + 29.0f, 5);
        if (is_active) {
            draw_string("< ACTIVE >", item_x + item_w - 120.0f, y + 29.0f, 4);
        }
    }

    float back_y = item_top - item_gap * visible_skin_count;
    int back_selected = (skin_menu_selection == SKIN_MENU_BACK);
    glColor3f(0.34f, 0.30f, 0.34f);
    glRectf(item_x, back_y, item_x + item_w, back_y + item_h);
    if (back_selected) {
        glColor3f(0.95f, 0.95f, 0.95f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(item_x, back_y);
        glVertex2f(item_x + item_w, back_y);
        glVertex2f(item_x + item_w, back_y + item_h);
        glVertex2f(item_x, back_y + item_h);
        glEnd();
    }
    glColor3f(0.05f, 0.05f, 0.06f);
    draw_string("BACK", item_x + 12.0f, back_y + 29.0f, 5);

    if (entry_count > visible_skin_count) {
        float track_x = panel_x + panel_w - 12.0f;
        float track_y0 = item_top;
        float track_y1 = item_top - item_gap * (visible_skin_count - 1) + item_h;
        float track_h = track_y0 - track_y1;
        float knob_h = track_h * ((float)visible_skin_count / (float)entry_count);
        if (knob_h < 18.0f) knob_h = 18.0f;
        float t = (max_scroll > 0) ? ((float)skin_menu_scroll / (float)max_scroll) : 0.0f;
        float knob_y = track_y0 - t * (track_h - knob_h);

        glColor3f(0.10f, 0.16f, 0.22f);
        glRectf(track_x, track_y0, track_x + 6.0f, track_y1);
        glColor3f(0.42f, 0.78f, 0.92f);
        glRectf(track_x, knob_y, track_x + 6.0f, knob_y - knob_h);
    }

    glColor3f(0.62f, 0.86f, 0.97f);
    draw_string("MOUSEWHEEL / UP-DOWN TO SCROLL", panel_x + 22.0f, panel_y - panel_h + 16.0f, 3);
}

void net_init() {
    #ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
    #endif
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        NET_ERR_LOG("SOCKET_CREATE_FAILED host=%s port=%d", SERVER_HOST, SERVER_PORT);
        return;
    }
    NET_CLIENT_LOG("SOCKET_CREATE_OK fd=%d", sock);
    #ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
    #else
    int flags = fcntl(sock, F_GETFL, 0); fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    #endif
}

void net_shutdown() {
    if (sock >= 0) {
        char buffer[128];
        NetHeader *h = (NetHeader*)buffer;
        memset(h, 0, sizeof(NetHeader));
        h->type = PACKET_DISCONNECT;
        sendto(sock, buffer, sizeof(NetHeader), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));

        #ifdef _WIN32
        closesocket(sock);
        #else
        close(sock);
        #endif
        sock = -1;
    }
    reset_client_render_state_for_net();
}

void net_connect() {
    if (sock < 0) net_init();
    if (sock < 0) return;
    local_state.game_mode = net_requested_mode;
    NET_CLIENT_LOG("CONNECT_BEGIN host=%s port=%d mode=%d", SERVER_HOST, SERVER_PORT, net_requested_mode);
    struct hostent *he = gethostbyname(SERVER_HOST);
    if (he) {
        server_addr.sin_family = AF_INET; 
        server_addr.sin_port = htons(SERVER_PORT); 
        memcpy(&server_addr.sin_addr, he->h_addr_list[0], he->h_length);
        char ip_buf[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &server_addr.sin_addr, ip_buf, sizeof(ip_buf));
        NET_CLIENT_LOG("DNS_OK host=%s ip=%s", SERVER_HOST, ip_buf);
        char buffer[128];
        NetHeader *h = (NetHeader*)buffer;
        memset(buffer, 0, sizeof(buffer));
        h->type = PACKET_CONNECT;
        h->scene_id = 0;
        buffer[sizeof(NetHeader)] = (unsigned char)(net_requested_mode & 0xFF);
        int packet_len = (int)sizeof(NetHeader) + 1;
        int sent = sendto(sock, buffer, packet_len, 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
        if (sent < 0) {
            NET_ERR_LOG("CONNECT_SEND_FAILED host=%s ip=%s port=%d mode=%d", SERVER_HOST, ip_buf, SERVER_PORT, net_requested_mode);
            return;
        }
        NET_CLIENT_LOG("CONNECT_BYTES b0=%u b1=%u b2=%u b3=%u b4=%u b5=%u b6=%u b7=%u len=%d",
                       (unsigned char)buffer[0], (unsigned char)buffer[1], (unsigned char)buffer[2], (unsigned char)buffer[3],
                       (unsigned char)buffer[4], (unsigned char)buffer[5], (unsigned char)buffer[6], (unsigned char)buffer[7], sent);
        net_diag.connect_started = 1;
        net_diag.connect_start_ms = SDL_GetTicks();
        net_diag.got_welcome = 0;
        net_diag.got_any_snapshot = 0;
        net_diag.got_local_snapshot = 0;
        net_diag.control_unlocked_logged = 0;
        net_diag.first_cmd_sent_logged = 0;
        net_diag.tx_cmds = 0;
        net_diag.snapshots_rx = 0;
        NET_CLIENT_LOG("CONNECT_SENT host=%s ip=%s port=%d mode=%d", SERVER_HOST, ip_buf, SERVER_PORT, net_requested_mode);
    } else {
        NET_ERR_LOG("DNS_FAIL host=%s port=%d", SERVER_HOST, SERVER_PORT);
    }
}

UserCmd client_create_cmd(float fwd, float str, float yaw, float pitch, int shoot, int jump, int crouch, int reload, int use, int ability, int wpn_idx) {
    UserCmd cmd;
    memset(&cmd, 0, sizeof(UserCmd));
    cmd.sequence = ++net_cmd_seq; cmd.timestamp = SDL_GetTicks();
    cmd.yaw = yaw; cmd.pitch = pitch;
    cmd.fwd = fwd; cmd.str = str;
    if(shoot) cmd.buttons |= BTN_ATTACK; if(jump) cmd.buttons |= BTN_JUMP;
    if(crouch) cmd.buttons |= BTN_CROUCH;
    if(reload) cmd.buttons |= BTN_RELOAD;
    if(use) cmd.buttons |= BTN_USE;
    if(ability) cmd.buttons |= BTN_ABILITY_1;
    client_cmd_hist[cmd.sequence % CLIENT_RECON_HISTORY] = cmd;
    net_latest_seq_sent = cmd.sequence;
    cmd.weapon_idx = wpn_idx; return cmd;
}

static void client_apply_cmd_movement(PlayerState *p, const UserCmd *cmd, unsigned int now_ms) {
    if (!p || !cmd) return;
    shankpit_apply_usercmd_inputs(p, cmd);
    shankpit_simulate_movement_tick(p, now_ms);
}

static float shortest_angle_delta_deg(float from, float to) {
    float d = to - from;
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

static float angle_lerp_deg(float a, float b, float t) {
    float d = shortest_angle_delta_deg(a, b);
    return norm_yaw_deg(a + d * t);
}

static void client_decay_pending_correction(unsigned int now_ms) {
    if (net_last_corr_decay_ms == 0) {
        net_last_corr_decay_ms = now_ms;
        return;
    }
    float dt = (float)(now_ms - net_last_corr_decay_ms) / 1000.0f;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.25f) dt = 0.25f;
    net_last_corr_decay_ms = now_ms;

    float keep = expf(-RECONCILE_DECAY_LAMBDA * dt);
    reconcile_corr_x *= keep;
    reconcile_corr_y *= keep;
    reconcile_corr_z *= keep;
    reconcile_corr_yaw *= keep;
    reconcile_corr_pitch *= keep;

    if (fabsf(reconcile_corr_x) < 0.001f) reconcile_corr_x = 0.0f;
    if (fabsf(reconcile_corr_y) < 0.001f) reconcile_corr_y = 0.0f;
    if (fabsf(reconcile_corr_z) < 0.001f) reconcile_corr_z = 0.0f;
    if (fabsf(reconcile_corr_yaw) < 0.01f) reconcile_corr_yaw = 0.0f;
    if (fabsf(reconcile_corr_pitch) < 0.01f) reconcile_corr_pitch = 0.0f;
}

static void client_reconcile_local_player(unsigned int ack_seq, float auth_x, float auth_y, float auth_z, float auth_yaw, float auth_pitch) {
    if (my_client_id <= 0 || my_client_id >= MAX_CLIENTS) return;
    if (ack_seq <= net_last_reconciled_ack) return;

    PlayerState *p = &local_state.players[my_client_id];
    if (!p->active) return;

    float prev_x = p->x;
    float prev_y = p->y;
    float prev_z = p->z;
    float prev_yaw = p->yaw;
    float prev_pitch = p->pitch;
    float prev_vx = p->vx;
    float prev_vy = p->vy;
    float prev_vz = p->vz;
    int prev_ground = p->on_ground;
    int prev_jump = p->in_jump;

    p->x = auth_x; p->y = auth_y; p->z = auth_z;
    p->yaw = norm_yaw_deg(auth_yaw);
    p->pitch = clamp_pitch_deg(auth_pitch);

    int replayed = 0;
    for (unsigned int seq = ack_seq + 1; seq <= net_latest_seq_sent; seq++) {
        UserCmd cmd = client_cmd_hist[seq % CLIENT_RECON_HISTORY];
        if (cmd.sequence != seq) continue;
        client_apply_cmd_movement(p, &cmd, cmd.timestamp);
        replayed++;
    }

    float ex = prev_x - p->x;
    float ey = prev_y - p->y;
    float ez = prev_z - p->z;
    float pos_err = sqrtf(ex * ex + ey * ey + ez * ez);
    float yaw_err = shortest_angle_delta_deg(p->yaw, prev_yaw);
    float pitch_err = prev_pitch - p->pitch;

    if (pos_err > RECONCILE_HARD_SNAP_DIST || fabsf(yaw_err) > RECONCILE_HARD_SNAP_YAW) {
        reconcile_corr_x = 0.0f;
        reconcile_corr_y = 0.0f;
        reconcile_corr_z = 0.0f;
        reconcile_corr_yaw = 0.0f;
        reconcile_corr_pitch = 0.0f;
    } else {
        if (p->on_ground && fabsf(ey) < 0.15f) {
            ey = 0.0f;
        }
        reconcile_corr_x += ex;
        reconcile_corr_y += ey;
        reconcile_corr_z += ez;
        reconcile_corr_yaw += yaw_err;
        reconcile_corr_pitch += pitch_err;
    }
    net_last_reconciled_ack = ack_seq;

    static unsigned int last_recon_dbg = 0;
    unsigned int now = SDL_GetTicks();
    if (now - last_recon_dbg >= 1000) {
        last_recon_dbg = now;
        printf("[NET] reconcile err=%.3f replayed=%d ack=%u latest=%u\n", pos_err, replayed, ack_seq, net_latest_seq_sent);
    }

#if NET_JITTER_DIAG
    if (net_reconcile_diag.window_start_ms == 0) {
        net_reconcile_diag.window_start_ms = now;
    }
    net_reconcile_diag.sample_count++;
    net_reconcile_diag.corr_sum += pos_err;
    if (pos_err > net_reconcile_diag.corr_max) net_reconcile_diag.corr_max = pos_err;
    float abs_yaw_err = fabsf(yaw_err);
    net_reconcile_diag.yaw_corr_sum += abs_yaw_err;
    if (abs_yaw_err > net_reconcile_diag.yaw_corr_max) net_reconcile_diag.yaw_corr_max = abs_yaw_err;
    net_reconcile_diag.replay_sum += (unsigned int)replayed;
    if ((unsigned int)replayed > net_reconcile_diag.replay_max) net_reconcile_diag.replay_max = (unsigned int)replayed;
    net_reconcile_diag.last_ack = ack_seq;
    net_reconcile_diag.last_latest = net_latest_seq_sent;
    net_reconcile_diag_emit(now);
#endif

#if NET_PARITY_DEBUG
    float pos_err_2d = sqrtf(ex * ex + ez * ez);
    float dvx = p->vx - prev_vx;
    float dvy = p->vy - prev_vy;
    float dvz = p->vz - prev_vz;
    net_parity_stats.last_ack_seq = ack_seq;
    net_parity_stats.last_local_seq = net_latest_seq_sent;
    net_parity_stats.last_replay_count = replayed;
    net_parity_stats.last_pos_corr = pos_err;
    net_parity_stats.last_pos_corr_2d = pos_err_2d;
    net_parity_stats.last_yaw_corr = fabsf(yaw_err);
    net_parity_stats.last_vel_delta = sqrtf(dvx * dvx + dvy * dvy + dvz * dvz);
    net_parity_stats.last_ground_mismatch = (p->on_ground != prev_ground) ? 1 : 0;
    net_parity_stats.last_jump_mismatch = (p->in_jump != prev_jump) ? 1 : 0;
    net_parity_stats.correction_count++;
    net_parity_stats.correction_sum += pos_err;
    if (pos_err > net_parity_stats.correction_max) net_parity_stats.correction_max = pos_err;
    net_parity_stats.replay_sum += (unsigned int)replayed;
    if (net_parity_stats.last_ground_mismatch) net_parity_stats.grounded_mismatch_count++;
    if (net_parity_stats.last_jump_mismatch) net_parity_stats.jump_mismatch_count++;
    net_parity_debug_sample(now);
#endif
}

static void net_apply_remote_interpolation(unsigned int now_ms) {
    int32_t synced_now = (int32_t)now_ms + net_server_time_offset_ms;
    uint32_t render_ts = (synced_now > (int32_t)INTERP_DELAY_MS) ? (uint32_t)(synced_now - (int32_t)INTERP_DELAY_MS) : 0;

#if NET_JITTER_DIAG
    if (net_interp_diag.window_start_ms == 0) {
        net_interp_diag.window_start_ms = now_ms;
        net_interp_diag.offset_last_ms = net_server_time_offset_ms;
    }
    int32_t offset_delta = net_server_time_offset_ms - net_interp_diag.offset_last_ms;
    int32_t offset_delta_abs = offset_delta < 0 ? -offset_delta : offset_delta;
    if (offset_delta_abs > net_interp_diag.offset_delta_abs_max_ms) {
        net_interp_diag.offset_delta_abs_max_ms = offset_delta_abs;
    }
    net_interp_diag.offset_last_ms = net_server_time_offset_ms;
    net_interp_diag.render_to_latest_snapshot_delta_ms = (int32_t)render_ts - (int32_t)net_last_snapshot_server_ts;
#endif

    for (int i = 1; i < MAX_CLIENTS; i++) {
        if (i == my_client_id) continue;
        RemoteInterp *ri = &rinterp[i];
        if (!ri->has_a) continue;

        PlayerState *p = &local_state.players[i];
        if (!p->active) continue;

        if (!ri->has_b || ri->b_time_ms <= ri->a_time_ms) {
#if NET_JITTER_DIAG
            net_interp_diag.interp_missing_pair++;
#endif
            p->x = ri->a.x; p->y = ri->a.y; p->z = ri->a.z;
            p->yaw = norm_yaw_deg(ri->a.yaw); p->pitch = clamp_pitch_deg(ri->a.pitch);
            continue;
        }

        float num = (float)((int)render_ts - (int)ri->a_time_ms);
        float den = (float)((int)ri->b_time_ms - (int)ri->a_time_ms);
        if (den <= 0.0f) {
#if NET_JITTER_DIAG
            net_interp_diag.interp_bad_timestamp++;
#endif
            p->x = ri->b.x; p->y = ri->b.y; p->z = ri->b.z;
            p->yaw = norm_yaw_deg(ri->b.yaw); p->pitch = clamp_pitch_deg(ri->b.pitch);
            continue;
        }
        float t = num / den;
        if (t < 0.0f) {
#if NET_JITTER_DIAG
            net_interp_diag.interp_t_clamp_zero++;
#endif
            t = 0.0f;
        }
        if (t > 1.0f) {
#if NET_JITTER_DIAG
            net_interp_diag.interp_t_clamp_one++;
#endif
            t = 1.0f;
        }

        p->x = ri->a.x + (ri->b.x - ri->a.x) * t;
        p->y = ri->a.y + (ri->b.y - ri->a.y) * t;
        p->z = ri->a.z + (ri->b.z - ri->a.z) * t;
        p->yaw = angle_lerp_deg(ri->a.yaw, ri->b.yaw, t);
        p->pitch = clamp_pitch_deg(ri->a.pitch + (ri->b.pitch - ri->a.pitch) * t);
    }

#if NET_JITTER_DIAG
    net_interp_diag_emit(now_ms);
#endif
}

void net_send_cmd(UserCmd cmd) {
    char packet_data[256];
    int cursor = 0;

    for (int i = NET_CMD_HISTORY - 1; i > 0; i--) {
        net_cmd_history[i] = net_cmd_history[i - 1];
    }
    net_cmd_history[0] = cmd;
    if (net_cmd_history_count < NET_CMD_HISTORY) net_cmd_history_count++;

    NetHeader head; head.type = PACKET_USERCMD;
    head.client_id = 0;
    head.scene_id = 0;
    memcpy(packet_data + cursor, &head, sizeof(NetHeader)); cursor += sizeof(NetHeader);

    unsigned char count = (unsigned char)net_cmd_history_count;
    memcpy(packet_data + cursor, &count, 1); cursor += 1;

    for (int i = 0; i < net_cmd_history_count; i++) {
        memcpy(packet_data + cursor, &net_cmd_history[i], sizeof(UserCmd));
        cursor += sizeof(UserCmd);
    }

    sendto(sock, packet_data, cursor, 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
    net_diag.tx_cmds++;
    if (!net_diag.first_cmd_sent_logged) {
        net_diag.first_cmd_sent_logged = 1;
        NET_CLIENT_LOG("USERCMD_TX_FIRST seq=%u payload_bytes=%d history_count=%d", cmd.sequence, cursor, net_cmd_history_count);
    }
}

void net_process_snapshot(char *buffer, int len) {
    if (my_client_id <= 0 || my_client_id >= MAX_CLIENTS) {
        return;
    }
    if (len < (int)sizeof(NetHeader)) return;
    if (len < (int)sizeof(NetHeader) + 1) return;
    NetHeader *head = (NetHeader*)buffer;
    if (net_last_snapshot_server_ts != 0 && head->timestamp <= net_last_snapshot_server_ts) {
#if NET_JITTER_DIAG
        net_interp_diag.interp_bad_timestamp++;
#endif
        return;
    }
    net_last_snapshot_server_ts = head->timestamp;

    int32_t target_offset = (int32_t)head->timestamp - (int32_t)SDL_GetTicks();
    if (!net_server_time_sync) {
        net_server_time_offset_ms = target_offset;
        net_server_time_sync = 1;
    } else {
        net_server_time_offset_ms = (net_server_time_offset_ms * 7 + target_offset) / 8;
    }

    int cursor = (int)sizeof(NetHeader);
    unsigned char count = *(unsigned char*)(buffer + cursor); cursor++;

    int needed = (int)sizeof(NetHeader) + 1 + (int)count * (int)sizeof(NetPlayer);
    if (len < needed) return;

    int local_seen = 0;
    unsigned char seen[MAX_CLIENTS];
    memset(seen, 0, sizeof(seen));
    unsigned int local_ack_seq = 0;
    float local_auth_x = 0.0f, local_auth_y = 0.0f, local_auth_z = 0.0f;
    float local_auth_yaw = 0.0f, local_auth_pitch = 0.0f;

    for(int i=0; i<count; i++) {
        if (cursor + (int)sizeof(NetPlayer) > len) break;

        NetPlayer *np = (NetPlayer*)(buffer + cursor);
        cursor += (int)sizeof(NetPlayer);

        int id = np->id;
        if (id <= 0 || id >= MAX_CLIENTS) continue;
        seen[id] = 1;

        PlayerState *p = &local_state.players[id];
        p->active = 1;
        p->scene_id = np->scene_id;

        if (id != my_client_id) {
            p->x = np->x; p->y = np->y; p->z = np->z;
            p->yaw   = norm_yaw_deg(np->yaw);
            p->pitch = clamp_pitch_deg(np->pitch);
        }

        p->state = np->state;
        p->is_bot = np->is_bot ? 1 : 0;
        p->team_id = np->team_id;
        p->health = np->health;
        p->shield = np->shield;
        p->crouching = np->crouching;

        int safe_weapon = np->current_weapon;
        if (safe_weapon < 0 || safe_weapon >= MAX_WEAPONS) {
            safe_weapon = WPN_MAGNUM;
        }
        p->current_weapon = safe_weapon;

        unsigned char was_shooting = net_prev_is_shooting[id];
        unsigned char now_shooting = np->is_shooting;
        net_prev_is_shooting[id] = now_shooting;

        p->is_shooting = now_shooting;
        p->in_vehicle = np->in_vehicle;
        p->carried_flag_team_id = np->carried_flag_team_id;
        p->storm_charges = np->storm_charges;
        p->hit_feedback = np->hit_feedback;
        p->kills = (int)np->kills;
        p->deaths = (int)np->deaths;
        p->death_duration_ms = np->death_duration_ms;
        p->death_dir_x = np->death_dir_x;
        p->death_dir_z = np->death_dir_z;
        if (np->state == STATE_DEAD && np->death_elapsed_ms > 0) {
            unsigned int now_local_ms = SDL_GetTicks();
            p->death_time_ms = (now_local_ms >= np->death_elapsed_ms) ? (now_local_ms - np->death_elapsed_ms) : 0;
        } else if (np->state != STATE_DEAD) {
            p->death_time_ms = 0;
        }
        p->ammo[p->current_weapon] = np->ammo;

        if (now_shooting && !was_shooting) p->recoil_anim = 1.0f;

        if (id == my_client_id) {
            local_seen = 1;
            local_ack_seq = np->last_seq;
            local_auth_x = np->x;
            local_auth_y = np->y;
            local_auth_z = np->z;
            local_auth_yaw = np->yaw;
            local_auth_pitch = np->pitch;

            int first_local_snapshot_sync = !net_have_initial_local_snapshot_sync;

            int spawn_transition = (!net_have_spawn_state)
                || (net_last_life_state != STATE_ALIVE && np->state == STATE_ALIVE)
                || (net_last_scene_id != np->scene_id && np->state == STATE_ALIVE);
            net_have_spawn_state = 1;
            net_last_life_state = np->state;
            net_last_scene_id = np->scene_id;

            if (first_local_snapshot_sync) {
                spawn_transition = 1;
            }

            if (spawn_transition) {
                const char *reason = first_local_snapshot_sync
                    ? "initial_local_snapshot"
                    : "spawn_transition";
                client_apply_spawn_transition_sync(p, np, reason);
            }
            if (first_local_snapshot_sync) {
                net_diag.got_local_snapshot = 1;
                unsigned int now_ms = SDL_GetTicks();
                unsigned int dt_ms = net_diag.connect_started ? (now_ms - net_diag.connect_start_ms) : 0;
                NET_CLIENT_LOG("LOCAL_SNAPSHOT_SYNC client_id=%d scene_id=%d ack=%u last_seq=%u dt_ms=%u",
                               my_client_id, (int)np->scene_id, local_ack_seq, net_latest_seq_sent, dt_ms);
            }
        } else {
            RemoteInterp *ri = &rinterp[id];
            if (!ri->has_a) {
                ri->a = *np;
                ri->a_time_ms = head->timestamp;
                ri->has_a = 1;
            } else if (!ri->has_b && head->timestamp > ri->a_time_ms) {
                ri->b = *np;
                ri->b_time_ms = head->timestamp;
                ri->has_b = 1;
            } else if (ri->has_b && head->timestamp > ri->b_time_ms) {
                ri->a = ri->b;
                ri->a_time_ms = ri->b_time_ms;
                ri->b = *np;
                ri->b_time_ms = head->timestamp;
            }
        }
    }

    if (local_seen) {
        client_reconcile_local_player(local_ack_seq, local_auth_x, local_auth_y, local_auth_z, local_auth_yaw, local_auth_pitch);
    }

    for (int id = 1; id < MAX_CLIENTS; id++) {
        if (id == my_client_id) continue;
        if (!seen[id]) {
            local_state.players[id].active = 0;
            local_state.players[id].is_shooting = 0;
            net_prev_is_shooting[id] = 0;
            rinterp[id].has_a = 0;
            rinterp[id].has_b = 0;
        }
    }

    for (int hi = 0; hi < MAX_HELICOPTERS; hi++) {
        local_state.helicopters[hi].active = 0;
        local_state.helicopters[hi].occupant_player_id = -1;
    }
    for (int bi = 0; bi < MAX_BUGGIES; bi++) {
        local_state.buggies[bi].active = 0;
        local_state.buggies[bi].occupant_player_id = -1;
    }
    unsigned char heli_count = 0;
    if (cursor < len) {
        heli_count = *(unsigned char *)(buffer + cursor);
        cursor++;
        for (int hi = 0; hi < heli_count; hi++) {
            if (cursor + (int)sizeof(NetHelicopter) > len) break;
            NetHelicopter *nh = (NetHelicopter *)(buffer + cursor);
            cursor += (int)sizeof(NetHelicopter);
            if (nh->id >= MAX_HELICOPTERS) continue;
            HelicopterState *h = &local_state.helicopters[nh->id];
            h->active = nh->active;
            h->id = nh->id;
            h->scene_id = nh->scene_id;
            h->grounded = nh->grounded;
            h->x = nh->x; h->y = nh->y; h->z = nh->z;
            h->vx = nh->vx; h->vy = nh->vy; h->vz = nh->vz;
            h->yaw = nh->yaw;
            h->pitch_visual = nh->pitch_visual;
            h->roll_visual = nh->roll_visual;
            h->rotor_angle = nh->rotor_angle;
            h->rotor_speed = nh->rotor_speed;
            h->health = nh->health;
            h->occupant_player_id = nh->occupant_player_id;
            if (h->occupant_player_id > 0 && h->occupant_player_id < MAX_CLIENTS) {
                PlayerState *occ = &local_state.players[h->occupant_player_id];
                occ->in_vehicle = 1;
                occ->vehicle_type = VEH_HELICOPTER;
            }
        }
    }
    if (cursor < len) {
        unsigned char buggy_count = *(unsigned char *)(buffer + cursor);
        cursor++;
        for (int bi = 0; bi < buggy_count; bi++) {
            if (cursor + (int)sizeof(NetBuggy) > len) break;
            NetBuggy *nb = (NetBuggy *)(buffer + cursor);
            cursor += (int)sizeof(NetBuggy);
            if (nb->id >= MAX_BUGGIES) continue;
            BuggyState *b = &local_state.buggies[nb->id];
            b->active = nb->active;
            b->id = nb->id;
            b->scene_id = nb->scene_id;
            b->grounded = nb->grounded;
            b->x = nb->x; b->y = nb->y; b->z = nb->z;
            b->vx = nb->vx; b->vy = nb->vy; b->vz = nb->vz;
            b->yaw = nb->yaw;
            b->pitch = nb->pitch;
            b->roll = nb->roll;
            b->steer = nb->steer;
            b->occupant_player_id = nb->occupant_player_id;
            if (b->occupant_player_id > 0 && b->occupant_player_id < MAX_CLIENTS) {
                PlayerState *occ = &local_state.players[b->occupant_player_id];
                occ->in_vehicle = 1;
                occ->vehicle_type = VEH_BUGGY;
                occ->x = b->x; occ->y = b->y; occ->z = b->z;
                occ->yaw = b->yaw;
            }
        }
    }
#if HELI_NET_DEBUG
    printf("[HELI SNAPSHOT][RX] scene=%d heli_count=%u cursor=%d len=%d\n",
           (int)head->scene_id, heli_count, cursor, len);
#endif
    for (int i = 1; i < MAX_CLIENTS; i++) {
        PlayerState *p = &local_state.players[i];
        if (!p->active) continue;
        if (!p->in_vehicle) p->vehicle_type = VEH_NONE;
        else if (p->vehicle_type != VEH_HELICOPTER) p->vehicle_type = VEH_BUGGY;
    }

    int render_id = (my_client_id > 0 && my_client_id < MAX_CLIENTS && local_state.players[my_client_id].active)
        ? my_client_id
        : 0;
    int scene_id = local_state.players[render_id].scene_id;
    if (scene_id < 0) {
        scene_id = local_state.scene_id;
    }
    if (scene_id != last_applied_scene_id && scene_id >= 0) {
        last_applied_scene_id = scene_id;
        client_apply_scene_id(scene_id, SDL_GetTicks());
    }
}


void net_tick() {
    char buffer[4096];
    while (1) {
        struct sockaddr_in sender;
        socklen_t slen = sizeof(sender);
        int len = recvfrom(sock, buffer, 4096, 0, (struct sockaddr*)&sender, &slen);
        if (len <= 0) {
            #ifdef _WIN32
            int e = WSAGetLastError();
            if (e == WSAEWOULDBLOCK) break;
            #else
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            #endif
            break;
        }

        if (len < (int)sizeof(NetHeader)) {
            unsigned int now_ms = SDL_GetTicks();
            if (net_should_log_every(&net_diag.last_invalid_packet_log_ms, 1000, now_ms)) {
                NET_WARN_LOG("SHORT_PACKET len=%d min=%zu", len, sizeof(NetHeader));
            }
            continue;
        }

        NetHeader *head = (NetHeader*)buffer;
        if (head->type == PACKET_SNAPSHOT) {
            unsigned int now_ms = SDL_GetTicks();
            if (!net_diag.got_any_snapshot) {
                net_diag.got_any_snapshot = 1;
                unsigned int dt_ms = net_diag.connect_started ? (now_ms - net_diag.connect_start_ms) : 0;
                NET_CLIENT_LOG("SNAPSHOT_RX_FIRST len=%d dt_ms=%u", len, dt_ms);
            }
            net_diag.snapshots_rx++;
            net_diag.latest_snapshot_len = len;
            net_diag.latest_entity_count = (int)head->entity_count;
            net_process_snapshot(buffer, len);
        } else if (head->type == PACKET_WELCOME) {
            my_client_id = head->client_id;
            unsigned int now_ms = SDL_GetTicks();
            char sender_buf[64];
            net_format_addr(&sender, sender_buf, sizeof(sender_buf));
            if (!net_diag.got_welcome) {
                net_diag.got_welcome = 1;
                net_diag.welcome_ms = now_ms;
                unsigned int dt_ms = net_diag.connect_started ? (now_ms - net_diag.connect_start_ms) : 0;
                NET_CLIENT_LOG("WELCOME_RX_FIRST client_id=%d scene_id=%d from=%s dt_ms=%u",
                               my_client_id, head->scene_id, sender_buf, dt_ms);
            } else if (net_should_log_every(&net_diag.last_dup_welcome_log_ms, 1000, now_ms)) {
                NET_WARN_LOG("WELCOME_DUPLICATE client_id=%d scene_id=%d from=%s", my_client_id, head->scene_id, sender_buf);
            }

            // Server-assigned identity becomes our local simulation/render identity
            net_local_pid = my_client_id;
            net_spawn_protect_cmds = 0;
            net_have_spawn_state = 0;
            net_have_initial_local_snapshot_sync = 0;
            net_last_life_state = STATE_DEAD;
            net_last_scene_id = 255;
            last_applied_scene_id = -999;

            if (my_client_id > 0 && my_client_id < MAX_CLIENTS) {
                PlayerState *lp = &local_state.players[my_client_id];

                // Pre-warm local slot data, but keep it inactive until first authoritative local snapshot sync.
                lp->scene_id = head->scene_id;
                lp->active = 0;

        
                if (head->scene_id >= 0 && head->scene_id != last_applied_scene_id) {
                    last_applied_scene_id = head->scene_id;
                    client_apply_scene_id(head->scene_id, SDL_GetTicks());
                }
            } else {
                 // invalid id => disable local sim/render identity
                net_local_pid = -1;
            }

            NET_CLIENT_LOG("WELCOME_APPLIED my_client_id=%d net_local_pid=%d", my_client_id, net_local_pid);
            if (len >= (int)sizeof(NetHeader) + 1) {
                local_state.game_mode = (unsigned char)buffer[sizeof(NetHeader)];
            }
            printf("✅ JOINED SERVER AS CLIENT ID: %d (welcome received)\n", my_client_id);
        } else {
            unsigned int now_ms = SDL_GetTicks();
            if (net_should_log_every(&net_diag.last_invalid_packet_log_ms, 1000, now_ms)) {
                NET_WARN_LOG("UNKNOWN_PACKET type=%d len=%d", head->type, len);
            }
        }
    }
}


int main(int argc, char* argv[]) {
    for(int i=1; i<argc; i++) {
        if(strcmp(argv[i], "--host") == 0 && i+1<argc) {
            strncpy(SERVER_HOST, argv[++i], 63);
        }
    }

    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window *win = SDL_CreateWindow("SHANKPIT [BUILD 181 - CTF RELOADED]", 100, 100, 1280, 720, SDL_WINDOW_OPENGL);
    SDL_GL_CreateContext(win);
    proctex_init();
    proc_tex_create(&g_vehicle_noise_tex, 64, 64);
    proctex_make_noise_rgba(&g_vehicle_noise_tex, 64, 64, g_vehicle_style.seed);
    proctex_upload_to_gl(&g_vehicle_noise_tex);
    proc_tex_create(&g_vehicle_glitch_tex, 64, 64);
    proctex_make_glitch_marks_rgba(&g_vehicle_glitch_tex, 64, 64, g_vehicle_style.seed ^ 0xA53u);
    proctex_upload_to_gl(&g_vehicle_glitch_tex);
    retro_sky_init(&g_retro_sky);
    net_init();
    
    local_init_match(1, 0);
    load_skin_selection();
    lobby_init_labels();
    ui_bridge_init("127.0.0.1", 17777);
    if (ui_bridge_fetch_state(&ui_state)) {
        ui_use_server = 1;
        ui_last_poll = SDL_GetTicks();
    }
    
    int running = 1;
    while(running) {
        SDL_Event e;
        while(SDL_PollEvent(&e)) {
            if(e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED && app_state != STATE_LOBBY) SDL_SetRelativeMouseMode(SDL_TRUE);
            if (e.type == SDL_MOUSEBUTTONDOWN && app_state != STATE_LOBBY) SDL_SetRelativeMouseMode(SDL_TRUE);
            
            if (app_state == STATE_LOBBY) {
                if (e.type == SDL_TEXTINPUT && ui_edit_index >= 0) {
                    size_t len = strlen(e.text.text);
                    if (len > 0 && ui_edit_len < (int)sizeof(ui_edit_buffer) - 1) {
                        int copy = (int)len;
                        if (ui_edit_len + copy >= (int)sizeof(ui_edit_buffer) - 1) {
                            copy = (int)sizeof(ui_edit_buffer) - 1 - ui_edit_len;
                        }
                        memcpy(ui_edit_buffer + ui_edit_len, e.text.text, (size_t)copy);
                        ui_edit_len += copy;
                        ui_edit_buffer[ui_edit_len] = '\0';
                    }
                }
                if (e.type == SDL_KEYDOWN) {
                    if (ui_edit_index >= 0) {
                        if (e.key.keysym.sym == SDLK_BACKSPACE && ui_edit_len > 0) {
                            ui_edit_len--;
                            ui_edit_buffer[ui_edit_len] = '\0';
                        }
                        if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                            lobby_end_edit(1);
                        }
                        if (e.key.keysym.sym == SDLK_ESCAPE) {
                            lobby_end_edit(0);
                        }
                        continue;
                    }
                    if (skin_menu_open) {
                        if (e.key.keysym.sym == SDLK_UP) {
                            if (skin_menu_selection == SKIN_MENU_BACK) {
                                skin_menu_selection = SKIN_COUNT - 1;
                                ensure_skin_selection_visible();
                            } else if (skin_menu_selection == 0) {
                                skin_menu_selection = SKIN_MENU_BACK;
                            } else {
                                skin_menu_selection--;
                                ensure_skin_selection_visible();
                            }
                        } else if (e.key.keysym.sym == SDLK_DOWN) {
                            if (skin_menu_selection == SKIN_MENU_BACK) {
                                skin_menu_selection = 0;
                                ensure_skin_selection_visible();
                            } else if (skin_menu_selection >= SKIN_COUNT - 1) {
                                skin_menu_selection = SKIN_MENU_BACK;
                            } else {
                                skin_menu_selection++;
                                ensure_skin_selection_visible();
                            }
                        } else if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_BACKSPACE) {
                            skin_menu_open = 0;
                        } else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                            if (skin_menu_selection >= 0 && skin_menu_selection < SKIN_COUNT) {
                                g_selected_skin = clamp_skin_id(skin_menu_selection);
                                save_skin_selection();
                            } else if (skin_menu_selection == SKIN_MENU_BACK) {
                                skin_menu_open = 0;
                            }
                        }
                    } else {
                        if (e.key.keysym.sym == SDLK_UP) {
                            int count = lobby_menu_count();
                            lobby_selection = lobby_nav_move(lobby_selection, count, 0, -1);
                        }
                        if (e.key.keysym.sym == SDLK_DOWN) {
                            int count = lobby_menu_count();
                            lobby_selection = lobby_nav_move(lobby_selection, count, 0, 1);
                        }
                        if (e.key.keysym.sym == SDLK_LEFT) {
                            int count = lobby_menu_count();
                            lobby_selection = lobby_nav_move(lobby_selection, count, -1, 0);
                        }
                        if (e.key.keysym.sym == SDLK_RIGHT) {
                            int count = lobby_menu_count();
                            lobby_selection = lobby_nav_move(lobby_selection, count, 1, 0);
                        }
                        if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                            lobby_start_action(lobby_selection);
                        }
                    }
                }
                if (e.type == SDL_MOUSEWHEEL && skin_menu_open) {
                    skin_menu_scroll -= e.wheel.y;
                    int max_scroll = skin_menu_scroll_max();
                    if (skin_menu_scroll < 0) skin_menu_scroll = 0;
                    if (skin_menu_scroll > max_scroll) skin_menu_scroll = max_scroll;
                }
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                    float mx = (float)e.button.x;
                    float my = 720.0f - (float)e.button.y;
                    if (skin_menu_open) {
                        int back_hit = skin_hit_test_back_row(mx, my, 790.0f, 455.0f, 290.0f, 50.0f, 60.0f);
                        if (back_hit == SKIN_MENU_BACK) {
                            skin_menu_selection = SKIN_MENU_BACK;
                            skin_menu_open = 0;
                            continue;
                        }
                        int hit = skin_hit_test_scroll_rows(mx, my, 790.0f, 455.0f, 290.0f, 50.0f, 60.0f);
                        if (hit >= 0 && hit < SKIN_COUNT) {
                            skin_menu_selection = hit;
                            ensure_skin_selection_visible();
                            g_selected_skin = clamp_skin_id(hit);
                            save_skin_selection();
                        }
                        continue;
                    }

                    int menu_count = lobby_menu_count();
                    int hit = lobby_hit_test(mx, my, menu_count, &LOBBY_LAYOUT);
                    if (hit >= 0) {
                        unsigned int now = SDL_GetTicks();
                        if (ui_last_click_index == hit && ui_last_click_ms > 0) {
                            unsigned int delta = now - ui_last_click_ms;
                            if (delta <= 250) {
                                lobby_selection = hit;
                                lobby_start_action(hit);
                                ui_last_click_ms = 0;
                                ui_last_click_index = -1;
                            } else if (delta <= 700) {
                                lobby_selection = hit;
                                if (hit != lobby_menu_count() - 1) {
                                    lobby_start_edit(hit);
                                } else {
                                    lobby_start_action(hit);
                                }
                                ui_last_click_ms = 0;
                                ui_last_click_index = -1;
                            } else {
                                ui_last_click_ms = now;
                                ui_last_click_index = hit;
                            }
                        } else {
                            ui_last_click_ms = now;
                            ui_last_click_index = hit;
                        }
                    }
                }
            } else {
                if (e.type == SDL_KEYDOWN) {
                    if ((local_state.game_mode == MODE_TDMB || local_state.game_mode == MODE_TDMO || local_state.game_mode == MODE_CTFB) && local_state.match_over && e.key.keysym.sym == SDLK_r) {
                        local_init_match(12, local_state.game_mode);
                    } else if (e.key.keysym.sym == SDLK_ESCAPE) {
                        if (app_state == STATE_GAME_NET) net_shutdown();
                        app_state = STATE_LOBBY;
                        SDL_SetRelativeMouseMode(SDL_FALSE);
                        setup_lobby_2d();
                    } else if (e.key.keysym.sym == SDLK_F6) {
                        terrain_wireframe_debug = !terrain_wireframe_debug;
                        printf("[TERRAIN] wireframe=%s\n", terrain_wireframe_debug ? "on" : "off");
                    } else if (e.key.keysym.sym == SDLK_F7) {
                        terrain_normals_debug = !terrain_normals_debug;
                        printf("[TERRAIN] normals=%s\n", terrain_normals_debug ? "on" : "off");
                    } else if (e.key.keysym.sym == SDLK_F8) {
                        vehicle_style_enabled = !vehicle_style_enabled;
                    } else if (e.key.keysym.sym == SDLK_F9) {
                        vehicle_underglow_enabled = !vehicle_underglow_enabled;
                    } else if (e.key.keysym.sym == SDLK_F10) {
                        vehicle_worklights_enabled = !vehicle_worklights_enabled;
                    } else if (e.key.keysym.sym == SDLK_F11) {
                        voxworld_points_debug = !voxworld_points_debug;
                        printf("[SCENE DEBUG] points_debug=%s\n", voxworld_points_debug ? "on" : "off");
                    } else if (e.key.keysym.sym == SDLK_F12) {
                        vs0_art_direction_enabled = !vs0_art_direction_enabled;
                        printf("[VS0 ART] enabled=%s\n", vs0_art_direction_enabled ? "on" : "off");
                    }
                }
                if(e.type == SDL_MOUSEMOTION) {
                    if (local_state.game_mode == MODE_STORY && local_state.story_phase == STORY_PHASE_CUTSCENE) continue;
                    if (app_state == STATE_GAME_NET && net_spawn_protect_cmds > 0) continue;
                    float sens = (current_fov < 50.0f) ? 0.05f : 0.15f; 
                    cam_yaw -= e.motion.xrel * sens;
                    if(cam_yaw > 360) cam_yaw -= 360; if(cam_yaw < 0) cam_yaw += 360;
                    cam_pitch -= e.motion.yrel * sens;
                    if(cam_pitch > 89) cam_pitch = 89; if(cam_pitch < -89) cam_pitch = -89;
                }
            }
        }
        if (app_state != STATE_LOBBY) SDL_SetRelativeMouseMode(SDL_TRUE);
        if (app_state == STATE_LOBBY) {
             unsigned int now = SDL_GetTicks();
             if (ui_use_server && (now - ui_last_poll) > 1000) {
                 if (ui_bridge_fetch_state(&ui_state)) {
                     ui_last_poll = now;
                     int count = lobby_menu_count();
                     if (lobby_selection >= count) lobby_selection = 0;
                 }
             }
             glClearColor(0.02f, 0.02f, 0.05f, 1.0f); // Dark Lobby
             glClear(GL_COLOR_BUFFER_BIT);
             setup_lobby_2d();
             glColor3f(0, 1, 1); // CYAN TEXT
             draw_string("SHANKPIT", LOBBY_LAYOUT.title_x, LOBBY_LAYOUT.title_y, 12);
             int menu_count = lobby_menu_count();
             draw_lobby_buttons(menu_count, &LOBBY_LAYOUT);
             if (skin_menu_open) {
                 draw_skin_chooser_overlay();
             }

             glColor3f(0.4f, 0.6f, 0.7f);
             draw_string("DOUBLE CLICK TO SELECT MODE", LOBBY_LAYOUT.footer_x, LOBBY_LAYOUT.footer_y, 5);
             SDL_GL_SwapWindow(win);
        } 
        else {
            const Uint8 *k = SDL_GetKeyboardState(NULL);
            int control_pid = (app_state == STATE_GAME_NET && my_client_id > 0 && my_client_id < MAX_CLIENTS) ? my_client_id : 0;
            int in_heli = local_state.players[control_pid].in_vehicle && local_state.players[control_pid].vehicle_type == VEH_HELICOPTER;
            float fwd=0, str=0;
            if(k[SDL_SCANCODE_W]) fwd+=1; if(k[SDL_SCANCODE_S]) fwd-=1;
            if (in_heli) {
                if(k[SDL_SCANCODE_A]) str-=1;
                if(k[SDL_SCANCODE_D]) str+=1;
            } else {
                if(k[SDL_SCANCODE_D]) str+=1; if(k[SDL_SCANCODE_A]) str-=1;
            }
            float move_len = sqrtf(fwd * fwd + str * str);
            if (move_len > 1.0f) {
                fwd /= move_len;
                str /= move_len;
            }
            int jump = k[SDL_SCANCODE_SPACE]; int crouch = k[SDL_SCANCODE_LCTRL];
            int shoot = (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT));
            int reload = in_heli ? k[SDL_SCANCODE_Q] : k[SDL_SCANCODE_R];
            int use = k[SDL_SCANCODE_F];
            int ability = in_heli ? k[SDL_SCANCODE_E] : k[SDL_SCANCODE_E];
            if(k[SDL_SCANCODE_1]) wpn_req=0; if(k[SDL_SCANCODE_2]) wpn_req=1;
            if(k[SDL_SCANCODE_3]) wpn_req=2; if(k[SDL_SCANCODE_4]) wpn_req=3; if(k[SDL_SCANCODE_5]) wpn_req=4; if(k[SDL_SCANCODE_6]) wpn_req=5;

            int fov_pid = (app_state == STATE_GAME_NET && net_local_pid > 0 && local_state.players[net_local_pid].active)
                ? net_local_pid
                : 0;
            float target_fov = (local_state.players[fov_pid].current_weapon == WPN_SNIPER && (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_RIGHT))) ? 20.0f : 75.0f;
            current_fov += (target_fov - current_fov) * 0.2f;
            glMatrixMode(GL_PROJECTION); glLoadIdentity(); gluPerspective(current_fov, 1280.0/720.0, 0.1, Z_FAR); 
            glMatrixMode(GL_MODELVIEW);
            if (app_state == STATE_GAME_NET) {
                net_local_pid = (my_client_id > 0 && my_client_id < MAX_CLIENTS) ? my_client_id : -1;
                net_tick();
                unsigned int now_ms = SDL_GetTicks();
                net_check_diag_timeouts(now_ms);
                if (net_local_pid > 0 && net_have_initial_local_snapshot_sync) {
                    if (now_ms - net_last_cmd_send_ms >= CLIENT_USERCMD_INTERVAL_MS) {
                        if (!net_diag.control_unlocked_logged) {
                            net_diag.control_unlocked_logged = 1;
                            NET_CLIENT_LOG("CONTROL_UNLOCKED client_id=%d local_pid=%d dt_ms=%u",
                                           my_client_id, net_local_pid,
                                           net_diag.connect_started ? (now_ms - net_diag.connect_start_ms) : 0);
                        }
                        UserCmd cmd = client_create_cmd(fwd, str, cam_yaw, cam_pitch, shoot, jump, crouch, reload, use, ability, wpn_req);
                        client_apply_cmd_movement(&local_state.players[net_local_pid], &cmd, now_ms);
                        net_send_cmd(cmd);
                        net_last_cmd_send_ms = now_ms;
                        if (net_spawn_protect_cmds > 0) net_spawn_protect_cmds--;
                    }
                } else if (net_should_log_every(&net_diag.last_blocked_input_log_ms, 1000, now_ms)) {
                    if (!(net_local_pid > 0)) {
                        NET_WARN_LOG("INPUT_BLOCKED reason=invalid_client_id my_client_id=%d local_pid=%d", my_client_id, net_local_pid);
                    } else if (!net_have_initial_local_snapshot_sync) {
                        NET_WARN_LOG("INPUT_BLOCKED reason=awaiting_local_snapshot_sync client_id=%d", my_client_id);
                    }
                }
                client_decay_pending_correction(now_ms);
                net_apply_remote_interpolation(now_ms);
                net_emit_client_summaries(now_ms);
            } else {
                local_state.players[0].in_use = use;
                if (use && local_state.players[0].vehicle_cooldown == 0 && local_state.transition_timer == 0) {
                    PlayerState *p0 = &local_state.players[0];
                    HelicopterState *near_h = NULL;
                    for (int hi = 0; hi < MAX_HELICOPTERS; hi++) {
                        HelicopterState *h = &local_state.helicopters[hi];
                        if (!h->active || h->scene_id != p0->scene_id) continue;
                        float dx = h->x - p0->x, dy = h->y - p0->y, dz = h->z - p0->z;
                        if ((dx*dx + dy*dy + dz*dz) <= (g_heli_tuning.enter_radius * g_heli_tuning.enter_radius)) {
                            near_h = h; break;
                        }
                    }
                    int in_garage = local_state.scene_id == SCENE_GARAGE_OSAKA;
                    int portal_id = -1;
                    if (scene_portal_triggered(p0, &portal_id)) {
                        int dest_scene = -1;
                        float sx = 0.0f, sy = 0.0f, sz = 0.0f;
                        if (portal_resolve_destination(p0->scene_id, portal_id, p0->id, &dest_scene, &sx, &sy, &sz)) {
                            p0->scene_id = dest_scene;
                            p0->x = sx; p0->y = sy; p0->z = sz;
                            p0->vx = 0.0f; p0->vy = 0.0f; p0->vz = 0.0f;
                            scene_request_transition(dest_scene);
                        }
                    } else if (p0->in_vehicle && p0->vehicle_type == VEH_HELICOPTER) {
                        for (int hi = 0; hi < MAX_HELICOPTERS; hi++) {
                            HelicopterState *h = &local_state.helicopters[hi];
                            if (!h->active || h->occupant_player_id != 0) continue;
                            if (heli_try_place_player(p0, h, g_heli_tuning.exit_offset, 0.0f) ||
                                heli_try_place_player(p0, h, -g_heli_tuning.exit_offset, 0.0f) ||
                                heli_try_place_player(p0, h, 0.0f, -g_heli_tuning.exit_offset)) {
                                p0->in_vehicle = 0;
                                p0->vehicle_type = VEH_NONE;
                                h->occupant_player_id = -1;
                                p0->vehicle_cooldown = 30;
                            }
                            break;
                        }
                    } else if (near_h && near_h->occupant_player_id < 0) {
                        near_h->occupant_player_id = 0;
                        p0->in_vehicle = 1;
                        p0->vehicle_type = VEH_HELICOPTER;
                        p0->x = near_h->x; p0->y = near_h->y; p0->z = near_h->z;
                        p0->vehicle_cooldown = 30;
                    } else if (p0->in_vehicle && p0->vehicle_type == VEH_BUGGY) {
                        for (int bi = 0; bi < MAX_BUGGIES; bi++) {
                            BuggyState *b = &local_state.buggies[bi];
                            if (!b->active || b->occupant_player_id != 0) continue;
                            buggy_try_exit(p0, b);
                            break;
                        }
                        p0->vehicle_cooldown = 30;
                    } else {
                        BuggyState *near_b = buggy_find_nearby(p0->scene_id, p0->x, p0->y, p0->z, in_garage ? 8.0f : 14.0f);
                        if (near_b && near_b->occupant_player_id < 0) {
                            buggy_try_enter(p0, near_b);
                            p0->vehicle_cooldown = 30;
                        }
                    }
                }
                if(local_state.players[0].vehicle_cooldown > 0) local_state.players[0].vehicle_cooldown--;
                unsigned int now_ms = SDL_GetTicks();
                local_update(fwd, str, cam_yaw, cam_pitch, shoot, wpn_req, jump, crouch, reload, ability, NULL, now_ms);
            }
            int render_pid = 0;
            if (app_state == STATE_GAME_NET &&
                my_client_id > 0 && my_client_id < MAX_CLIENTS &&
                local_state.players[my_client_id].active) {
                render_pid = my_client_id;
            }
            int sim_pid = (app_state == STATE_GAME_NET) ? net_local_pid : 0;
            if (app_state == STATE_GAME_NET &&
                my_client_id > 0 && my_client_id < MAX_CLIENTS &&
                local_state.players[my_client_id].active) {
                static unsigned int last_pid_mismatch_log = 0;
                unsigned int now_ms = SDL_GetTicks();
                if (sim_pid != render_pid && now_ms - last_pid_mismatch_log >= 250) {
                    last_pid_mismatch_log = now_ms;
                    printf("PID mismatch (active): my_client_id=%d sim_pid=%d render_pid=%d\n",
                           my_client_id, sim_pid, render_pid);
                }
                if (sim_pid != render_pid) {
                    // fail open: trust render_pid
                    sim_pid = render_pid;
                }
            }
            PlayerState *render_p = &local_state.players[render_pid];
            if (app_state == STATE_GAME_NET && !net_have_initial_local_snapshot_sync) {
                render_pid = 0;
                render_p = &local_state.players[render_pid];
            }
            phys_set_scene(render_p->scene_id);
            unsigned int terrain_now_ms = SDL_GetTicks();
            if (terrain_now_ms - terrain_debug_last_log_ms >= 1000) {
                terrain_debug_last_log_ms = terrain_now_ms;
                TerrainHeightfield *terrain = scene_active_terrain();
                printf("[TERRAIN] active=%s scene=%d\n", (terrain && terrain->active) ? "yes" : "no", render_p->scene_id);
                if (terrain && terrain->active) {
                    float th = terrain_sample_height(terrain, render_p->x, render_p->z);
                    int ground_source_terrain = 0;
                    float gh = phys_sample_ground_height(render_p->x, render_p->z, &ground_source_terrain);
                    printf("[TERRAIN] sample x=%.2f z=%.2f h=%.2f ground=%.2f source=%s last=%s\n",
                           render_p->x, render_p->z, th, gh,
                           ground_source_terrain ? "terrain" : "box",
                           phys_last_grounded_on_terrain() ? "terrain" : "box");
                }
            }
            draw_scene(render_p);
            SDL_GL_SwapWindow(win);
        }
        SDL_Delay(16);
    }
    proc_tex_destroy(&g_vehicle_noise_tex);
    proc_tex_destroy(&g_vehicle_glitch_tex);
    retro_sky_shutdown(&g_retro_sky);
    SDL_Quit();
    return 0;
}
