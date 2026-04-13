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
#include "server_mode.h"
#include "server_state.h"

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

#define SERVER_SNAPSHOT_INTERVAL_TICKS 3
#define SERVER_DM_FRAG_LIMIT 25
#define SERVER_DM_ROUND_MS (6 * 60 * 1000)

static const int g_dm_rotation[] = { SCENE_STADIUM, SCENE_VOXWORLD, SCENE_OIL_TANKER };
static int g_dm_rotation_idx = 0;
static int g_server_match_scene = SCENE_GARAGE_OSAKA;
static unsigned int g_round_start_ms = 0;

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

static int server_scene_is_dm_map(int scene_id) {
    return scene_id == SCENE_STADIUM || scene_id == SCENE_VOXWORLD || scene_id == SCENE_OIL_TANKER;
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
        p->state = STATE_ALIVE;
        p->health = 100;
        p->shield = 100;
    }
    if (g_server_match_scene == SCENE_VOXWORLD) {
        printf("[HELI] authoritative voxworld spawn count=%d\n", server_scene_heli_count(SCENE_VOXWORLD));
    }
    printf("[ROUND] next_map=%d rotation_idx=%d\n", g_server_match_scene, g_dm_rotation_idx);
}

static int addr_equal(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
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

            return i;
        }
    }
    return -1;
}

static void free_slot(int slot) {
    if (slot <= 0 || slot >= MAX_CLIENTS) return;
    for (int i = 0; i < MAX_HELICOPTERS; i++) {
        if (local_state.helicopters[i].active && local_state.helicopters[i].occupant_player_id == slot) {
            local_state.helicopters[i].occupant_player_id = -1;
        }
    }
    slots[slot].active = 0;
    slots[slot].welcomed = 0;
    slots[slot].cmd_seen = 0;
    memset(&slots[slot].addr, 0, sizeof(struct sockaddr_in));
    slots[slot].last_heard = 0.0;
    slots[slot].player_id = -1;
    server_disconnect(slot, client_last_seq);
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
    NetHeader h;
    h.type = PACKET_WELCOME;
    h.client_id = (unsigned char)client_id;
    h.sequence = 0;
    h.timestamp = now;
    h.entity_count = 0;
    h.scene_id = (unsigned char)local_state.players[client_id].scene_id;
    sendto(sock, (char*)&h, sizeof(NetHeader), 0,
           (const struct sockaddr*)addr, sizeof(struct sockaddr_in));
    if (client_id > 0 && client_id < MAX_CLIENTS) {
        slots[client_id].welcomed = 1;
    }
}

static int ensure_slot_for_sender(const struct sockaddr_in *sender) {
    int slot = find_slot_by_addr(sender);
    if (slot != -1) {
        slots[slot].last_heard = now_seconds();
        local_state.client_meta[slot].last_heard_ms = get_server_time();
        return slot;
    }

    int new_slot = alloc_slot(sender);
    if (new_slot != -1) {
        char ip_buf[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &sender->sin_addr, ip_buf, sizeof(ip_buf));
        printf("CLIENT %d CONNECTED (%s:%d)\n", new_slot, ip_buf, ntohs(sender->sin_port));
        send_welcome(sender, new_slot);
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
        } else if (strcmp(argv[i], "--deathmatch") == 0) {
            mode = MODE_DEATHMATCH;
        }
    }
    return mode;
}

void server_net_init() {
    setbuf(stdout, NULL);
    #ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
    #endif
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    #ifdef _WIN32
    u_long mode = 1; ioctlsocket(sock, FIONBIO, &mode);
    #else
    int flags = fcntl(sock, F_GETFL, 0); fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    #endif
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(6969);
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        printf("FAILED TO BIND PORT 6969\n");
        exit(1);
    } else {
        printf("SERVER LISTENING ON PORT 6969\nWaiting...\n");
    }
}

void process_user_cmd(int client_id, UserCmd *cmd) {
    if (cmd->sequence <= client_last_seq[client_id]) return;
    PlayerState *p = &local_state.players[client_id];
    shankpit_apply_usercmd_inputs(p, cmd);
    client_last_seq[client_id] = cmd->sequence;
}

void server_handle_packet(struct sockaddr_in *sender, char *buffer, int size) {
    if (size < (int)sizeof(NetHeader)) return;
    NetHeader *head = (NetHeader*)buffer;
    int client_id = -1;

    if (head->type == PACKET_CONNECT || head->type == PACKET_USERCMD || head->type == PACKET_DISCONNECT) {
        client_id = ensure_slot_for_sender(sender);
    }
    if (client_id == -1) return;

    if (head->type == PACKET_CONNECT) {
        PlayerState *p = &local_state.players[client_id];
        client_last_seq[client_id] = 0;
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
        send_welcome(sender, client_id);
    }

    if (client_id != -1 && head->type == PACKET_DISCONNECT) {
        free_slot(client_id);
        return;
    }

    // --- USER COMMANDS ---
    if (client_id != -1 && head->type == PACKET_USERCMD) {
        int cursor = (int)sizeof(NetHeader);
        if (size < cursor + 1) return;

        unsigned char count = *(unsigned char*)(buffer + cursor); cursor += 1;

        if (size >= cursor + (int)(count * sizeof(UserCmd))) {
            UserCmd *cmds = (UserCmd*)(buffer + cursor);

            // process oldest->newest to preserve chronological intent
            for (int i = (int)count - 1; i >= 0; i--) {
                process_user_cmd(client_id, &cmds[i]);
            }

            slots[client_id].last_heard = now_seconds();
            local_state.client_meta[client_id].last_heard_ms = get_server_time();
            slots[client_id].cmd_seen = 1;
            local_state.players[client_id].active = slots[client_id].welcomed && slots[client_id].cmd_seen;
            local_state.client_meta[client_id].active = local_state.players[client_id].active;
        }
    }
}

void server_broadcast() {
    char buffer[4096];
    for (int i = 1; i < MAX_CLIENTS; i++) {
        if (!slots[i].active || !slots[i].welcomed) continue;
        int recipient_scene = local_state.players[i].scene_id;
        int cursor = 0;
        unsigned char count = 0;

        for (int pi = 1; pi < MAX_CLIENTS; pi++) {
            if (!(slots[pi].active && slots[pi].welcomed && slots[pi].cmd_seen)) continue;
            if (!local_state.players[pi].active) continue;
            if (local_state.players[pi].scene_id != recipient_scene) continue;
            count++;
        }

        NetHeader head;
        head.type = PACKET_SNAPSHOT;
        head.client_id = 0;
        head.sequence = local_state.server_tick;
        head.timestamp = get_server_time();
        head.scene_id = (unsigned char)(recipient_scene < 0 ? 0 : recipient_scene);
        head.entity_count = count;

        memcpy(buffer + cursor, &head, sizeof(NetHeader)); cursor += (int)sizeof(NetHeader);
        memcpy(buffer + cursor, &count, 1); cursor += 1;

        for (int pi = 1; pi < MAX_CLIENTS; pi++) {
            PlayerState *p = &local_state.players[pi];
            if (!(slots[pi].active && slots[pi].welcomed && slots[pi].cmd_seen && p->active)) continue;
            if (p->scene_id != recipient_scene) continue;

            if (cursor + (int)sizeof(NetPlayer) + 1 > (int)sizeof(buffer)) break;
            NetPlayer np;
            np.id = (unsigned char)pi;
            np.scene_id = (unsigned char)p->scene_id;
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
            np.hit_feedback = (unsigned char)p->hit_feedback;
            np.storm_charges = (unsigned char)p->storm_charges;
            np.sticky_grenades = (unsigned char)(p->sticky_grenades < 0 ? 0 : p->sticky_grenades);
            np.kills = (unsigned short)(p->kills < 0 ? 0 : p->kills);
            np.deaths = (unsigned short)(p->deaths < 0 ? 0 : p->deaths);
            p->accumulated_reward = 0;
            memcpy(buffer + cursor, &np, sizeof(NetPlayer)); cursor += (int)sizeof(NetPlayer);
        }

        unsigned char heli_count = 0;
        for (int hi = 0; hi < MAX_HELICOPTERS; hi++) {
            HelicopterState *h = &local_state.helicopters[hi];
            if (!h->active || h->scene_id != recipient_scene) continue;
            heli_count++;
        }

        if (cursor + 1 > (int)sizeof(buffer)) continue;
        memcpy(buffer + cursor, &heli_count, 1); cursor += 1;
        for (int hi = 0; hi < MAX_HELICOPTERS; hi++) {
            HelicopterState *h = &local_state.helicopters[hi];
            if (!h->active || h->scene_id != recipient_scene) continue;
            if (cursor + (int)sizeof(NetHelicopter) > (int)sizeof(buffer)) break;
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
        }

        unsigned char sticky_count = 0;
        for (int si = 0; si < MAX_STICKY_GRENADES; si++) {
            StickyGrenadeState *g = &local_state.sticky_grenades[si];
            if (!g->active || g->scene_id != recipient_scene) continue;
            sticky_count++;
        }
        memcpy(buffer + cursor, &sticky_count, 1); cursor += 1;
        for (int si = 0; si < MAX_STICKY_GRENADES; si++) {
            StickyGrenadeState *g = &local_state.sticky_grenades[si];
            if (!g->active || g->scene_id != recipient_scene) continue;
            NetStickyGrenade ng;
            ng.id = (unsigned char)g->id;
            ng.scene_id = (unsigned char)g->scene_id;
            ng.active = (unsigned char)g->active;
            ng.attached = (unsigned char)g->attached;
            ng.attach_type = g->attach_type;
            ng.attach_target_id = (signed char)g->attach_target_id;
            ng.fuse_ticks = (unsigned short)(g->fuse_ticks < 0 ? 0 : g->fuse_ticks);
            ng.x = g->x; ng.y = g->y; ng.z = g->z;
            memcpy(buffer + cursor, &ng, sizeof(NetStickyGrenade)); cursor += (int)sizeof(NetStickyGrenade);
        }

        unsigned char pickup_count = 0;
        for (int wi = 0; wi < MAX_WORLD_PICKUPS; wi++) {
            WorldPickup *wp = &local_state.world_pickups[wi];
            if (!wp->active || !wp->available || wp->scene_id != recipient_scene) continue;
            pickup_count++;
        }
        memcpy(buffer + cursor, &pickup_count, 1); cursor += 1;
        for (int wi = 0; wi < MAX_WORLD_PICKUPS; wi++) {
            WorldPickup *wp = &local_state.world_pickups[wi];
            if (!wp->active || !wp->available || wp->scene_id != recipient_scene) continue;
            NetWorldPickup nw;
            nw.id = (unsigned char)wp->id;
            nw.scene_id = (unsigned char)wp->scene_id;
            nw.active = (unsigned char)wp->active;
            nw.type = wp->type;
            nw.x = wp->x; nw.y = wp->y; nw.z = wp->z;
            memcpy(buffer + cursor, &nw, sizeof(NetWorldPickup)); cursor += (int)sizeof(NetWorldPickup);
        }
#if HELI_NET_DEBUG
        printf("[HELI SNAPSHOT][TX] client=%d scene=%d heli_count=%u players=%u\n", i, recipient_scene, heli_count, count);
#endif
        if (slots[i].active) {
            sendto(sock, buffer, cursor, 0,
                   (struct sockaddr*)&slots[i].addr,
                   sizeof(struct sockaddr_in));
        }
    }
}

int main(int argc, char *argv[]) {
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
        }
    }

    if (recorder.enabled && !recorder.file) {
        recorder_init_file("shankpit_recording.lispasm");
    }

    server_net_init();
    int mode = parse_server_mode(argc, argv);
    local_init_match(1, mode);
    if (mode == MODE_DEATHMATCH || mode == MODE_TDM) {
        g_server_match_scene = g_dm_rotation[g_dm_rotation_idx];
        scene_load(g_server_match_scene);
        g_round_start_ms = get_server_time();
        if (g_server_match_scene == SCENE_VOXWORLD) {
            printf("[HELI] authoritative voxworld spawn count=%d\n", server_scene_heli_count(SCENE_VOXWORLD));
        }
    } else {
        g_server_match_scene = SCENE_GARAGE_OSAKA;
    }
    local_state.players[0].active = 0;
    local_state.players[0].health = 0;
    local_state.players[0].state = STATE_DEAD;
    printf("SERVER MODE: %s\n", mode == MODE_TDM ? "TEAM DEATHMATCH" : "DEATHMATCH");

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
        // TIMEOUT_SWEEP
        for (int i = 1; i < MAX_CLIENTS; i++) {
            if (slots[i].active && now_seconds() - slots[i].last_heard > 5.0) {
                free_slot(i);
            }
        }

        int active_count = 0;

        for(int i=0; i<MAX_CLIENTS; i++) {
            PlayerState *p = &local_state.players[i];

            if (i > 0 && p->active && p->state == STATE_DEAD) {
                if (local_state.game_mode != MODE_SURVIVAL && now > p->respawn_time) {
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
                        }
                    }
                }
                p->use_was_down = p->in_use;
                if (p->vehicle_cooldown > 0) p->vehicle_cooldown--;

                if (!(p->in_vehicle && p->vehicle_type == VEH_HELICOPTER)) {
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

        if ((mode == MODE_DEATHMATCH || mode == MODE_TDM) && server_scene_is_dm_map(g_server_match_scene)) {
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

        update_projectiles(now);
        recorder_write_frame(tick, now);
        if ((tick % SERVER_SNAPSHOT_INTERVAL_TICKS) == 0) {
            server_broadcast();
        }

        int connected = 0;
        for (int i = 1; i < MAX_CLIENTS; i++) {
            if (slots[i].active && slots[i].welcomed && slots[i].cmd_seen && local_state.players[i].active) connected++;
        }
        active_count = connected;
        
        if (tick % 60 == 0) {
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

        #ifdef _WIN32
        Sleep(16);
        #else
        usleep(16000);
        #endif

        tick++;
    }

    return 0;
}
