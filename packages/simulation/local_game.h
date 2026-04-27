#ifndef LOCAL_GAME_H
#define LOCAL_GAME_H

#include "../common/protocol.h"
#include "../common/physics.h"
#include "../common/shared_movement.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

ServerState local_state;
int was_holding_jump = 0;
static int tdmb_last_kills[MAX_CLIENTS];
#define SHANKPIT_HELI_DEBUG 0
#define TDMB_BLUE_TEAM 1
#define TDMB_RED_TEAM 0
#define TDMB_BLUE_BOTS 5
#define TDMB_RED_BOTS 6
#define TDMB_SCORE_LIMIT 25
#define CTFB_SCORE_LIMIT 3
#define CTFB_DROPPED_RETURN_MS 12000
#define CTFB_RESPAWN_DELAY_MS 3000
#define CTFB_USE_RADIUS 28.0f
#define CTFB_CAPTURE_RADIUS 40.0f
#define CTFB_CARRY_MELEE_DAMAGE 80
#define CTFB_CARRY_MELEE_COOLDOWN_MS 450
#define CTFB_RESPAWN_DEBUG_LOG 0
#define STORY_INTRO_CUTSCENE_DURATION_MS 5000
#define STORY_BREACH_CUTSCENE_DURATION_MS 20000
#define STORY_BOSS_ATTACK_MIN_MS 2500U
#define STORY_BOSS_ATTACK_MAX_MS 4000U
#define STORY_BOSS_ATTACK_RADIUS 125.0f

static int mode_uses_team_scores(int mode) {
    return mode == MODE_TDM || mode == MODE_TDMB || mode == MODE_TDMO || mode == MODE_CTFB;
}

static unsigned int mode_respawn_delay_ms(int mode) {
    switch (mode) {
        case MODE_CTFB: return CTFB_RESPAWN_DELAY_MS;
        default: return 2000;
    }
}

static int scene_random_tdmb_map(void) {
    static int seeded = 0;
    static const int tdmb_maps[] = {
        SCENE_DUST_COMPOUND,
        SCENE_OIL_TANKER,
        SCENE_STADIUM,
        SCENE_VOXWORLD,
        SCENE_POO_POO_ISLAND
    };
    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = 1;
    }
    return tdmb_maps[rand() % (int)(sizeof(tdmb_maps) / sizeof(tdmb_maps[0]))];
}

static const char *scene_name_debug(int scene_id) {
    switch (scene_id) {
        case SCENE_DUST_COMPOUND: return "DUST_COMPOUND";
        case SCENE_OIL_TANKER: return "OIL_TANKER";
        case SCENE_STADIUM: return "STADIUM";
        case SCENE_VOXWORLD: return "VOXWORLD";
        case SCENE_POO_POO_ISLAND: return "POO_POO_ISLAND";
        case SCENE_STORY_CAVE: return "STORY_CAVE";
        default: return "UNKNOWN";
    }
}

static int team_id_is_valid(int team_id) {
    return team_id == TDMB_BLUE_TEAM || team_id == TDMB_RED_TEAM;
}

typedef enum {
    CTF_BOT_INTENT_ATTACK_FLAG = 0,
    CTF_BOT_INTENT_RETURN_HOME = 1,
    CTF_BOT_INTENT_DEFEND_HOME = 2,
    CTF_BOT_INTENT_RECOVER_FLAG = 3,
    CTF_BOT_INTENT_CHASE_CARRIER = 4,
    CTF_BOT_INTENT_ESCORT = 5
} CtfBotIntent;

typedef struct {
    int bot_id;
    int my_team_id;
    int enemy_team_id;
    int has_enemy_flag;
    int my_flag_state;
    int enemy_flag_state;
    int my_flag_carrier_id;
    int enemy_flag_carrier_id;
    float dist_enemy_flag;
    float dist_own_flag;
    float dist_own_capture_zone;
    float dist_enemy_base;
    float dist_own_base;
    float nearest_enemy_dist;
    float nearest_ally_dist;
    int health;
    int current_weapon;
    int attack_suppressed_for_flag;
    int intent;
    int scene_id;
    float x, y, z, yaw;
    int team_score;
    int enemy_score;
} CtfBotObservation;

typedef struct {
    float total;
    float pickup;
    float return_reward;
    float capture;
    float carrier_kill;
    float objective_progress;
    float death_penalty;
    float stuck_penalty;
} CtfBotRewardBreakdown;

static int ctf_enemy_team(int team_id) { return team_id == 0 ? 1 : 0; }

void local_update(float fwd, float str, float yaw, float pitch, int shoot, int weapon_req, int jump, int crouch, int reload, int ability, void *server_context, unsigned int cmd_time);
void update_entity(PlayerState *p, float dt, void *server_context, unsigned int cmd_time);
static inline void heli_spawn_defaults(HelicopterState *h, int id, int scene_id, float x, float y, float z);
static inline void buggy_spawn_defaults(BuggyState *b, int id, int scene_id, float x, float z, float yaw);
static inline BuggyState *buggy_find_nearby(int scene_id, float x, float y, float z, float radius);
static inline int buggy_try_enter(PlayerState *p, BuggyState *b);
static inline int buggy_try_exit(PlayerState *p, BuggyState *b);
static inline void buggy_tick_all(void);
static void ctf_init_match_state(int scene_id);
void local_init_match(int num_players, int mode);

float rand_weight() { return ((float)(rand()%2000)/1000.0f) - 1.0f; } 
float rand_pos() { return ((float)(rand()%1000)/1000.0f); } 

void init_genome(BotGenome *g) {
    g->version = 1;
    g->w_aggro = 0.5f + rand_weight() * 0.5f;
    g->w_strafe = rand_weight();
    g->w_jump = 0.05f + rand_pos() * 0.1f; 
    g->w_slide = 0.01f + rand_pos() * 0.05f;
    g->w_turret = 5.0f + rand_pos() * 10.0f;
    g->w_repel = 1.0f + rand_pos();
}

void evolve_bot(PlayerState *loser, PlayerState *winner) {
    loser->brain = winner->brain;
    loser->brain.w_aggro += rand_weight() * 0.1f;
    loser->brain.w_strafe += rand_weight() * 0.1f;
    loser->brain.w_jump += rand_weight() * 0.01f;
    loser->brain.w_slide += rand_weight() * 0.01f;
}

PlayerState* get_best_bot() {
    PlayerState *best = NULL;
    float max_score = -99999.0f;
    for(int i=1; i<MAX_CLIENTS; i++) {
        if (!local_state.players[i].active) continue;
        if (local_state.players[i].accumulated_reward > max_score) {
            max_score = local_state.players[i].accumulated_reward;
            best = &local_state.players[i];
        }
    }
    return best;
}

static inline void scene_load(int scene_id) {
    local_state.scene_id = scene_id;
    phys_set_scene(scene_id);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!local_state.players[i].active) continue;
        local_state.players[i].scene_id = scene_id;
        scene_force_spawn(&local_state.players[i]);
    }
    for (int hi = 0; hi < MAX_HELICOPTERS; hi++) {
        memset(&local_state.helicopters[hi], 0, sizeof(local_state.helicopters[hi]));
        local_state.helicopters[hi].id = hi;
        local_state.helicopters[hi].occupant_player_id = -1;
    }
    for (int bi = 0; bi < MAX_BUGGIES; bi++) {
        memset(&local_state.buggies[bi], 0, sizeof(local_state.buggies[bi]));
        local_state.buggies[bi].id = bi;
        local_state.buggies[bi].occupant_player_id = -1;
    }
    int pad_count = 0;
    const VehiclePad *pads = scene_vehicle_pads(scene_id, &pad_count);
    for (int i = 0; i < pad_count && i < MAX_BUGGIES; i++) {
        buggy_spawn_defaults(&local_state.buggies[i], i, scene_id, pads[i].x, pads[i].z, 180.0f);
        printf("[BUGGY] spawned id=%d scene=%d x=%.1f z=%.1f\n", i, scene_id, pads[i].x, pads[i].z);
    }

    if (scene_id == SCENE_VOXWORLD) {
        float red_y = voxworld_heli_spawn_y(VOXWORLD_BASE_RED_X);
        float blue_y = voxworld_heli_spawn_y(VOXWORLD_BASE_BLUE_X);
        heli_spawn_defaults(&local_state.helicopters[0], 0, SCENE_VOXWORLD, VOXWORLD_HELI_RED_X, red_y, VOXWORLD_HELI_RED_Z);
        heli_spawn_defaults(&local_state.helicopters[1], 1, SCENE_VOXWORLD, VOXWORLD_HELI_BLUE_X, blue_y, VOXWORLD_HELI_BLUE_Z);
        local_state.helicopters[0].yaw = 90.0f;
        local_state.helicopters[1].yaw = 270.0f;
        local_state.helicopters[0].grounded = 1;
        local_state.helicopters[1].grounded = 1;
        local_state.helicopters[0].rotor_speed = 14.0f;
        local_state.helicopters[1].rotor_speed = 14.0f;
#if SHANKPIT_HELI_DEBUG
        printf("[HELI] scene=%d spawned=2 red=(%.1f,%.1f,%.1f) blue=(%.1f,%.1f,%.1f)\n",
               scene_id,
               local_state.helicopters[0].x, local_state.helicopters[0].y, local_state.helicopters[0].z,
               local_state.helicopters[1].x, local_state.helicopters[1].y, local_state.helicopters[1].z);
#endif
    }
    if (local_state.game_mode == MODE_CTFB) {
        ctf_init_match_state(scene_id);
    }
}

static inline void heli_spawn_defaults(HelicopterState *h, int id, int scene_id, float x, float y, float z) {
    memset(h, 0, sizeof(*h));
    h->active = 1;
    h->id = id;
    h->scene_id = scene_id;
    h->x = x; h->y = y; h->z = z;
    h->health = 250;
    h->occupant_player_id = -1;
    h->rotor_speed = 8.0f;
    h->yaw = 180.0f;
}

static inline void buggy_spawn_defaults(BuggyState *b, int id, int scene_id, float x, float z, float yaw) {
    memset(b, 0, sizeof(*b));
    b->active = 1;
    b->id = id;
    b->scene_id = scene_id;
    b->x = x; b->z = z;
    b->yaw = yaw;
    b->occupant_player_id = -1;
    float gy = terrain_sample_height(&g_scene_terrain, x, z);
    if (gy < 0.0f) gy = 0.0f;
    b->y = gy + BUGGY_WHEEL_RADIUS + BUGGY_CHASSIS_CLEARANCE;
    b->grounded = 1;
    b->health = 300;
}

static inline BuggyState *buggy_find_nearby(int scene_id, float x, float y, float z, float radius) {
    float rr = radius * radius;
    for (int i = 0; i < MAX_BUGGIES; i++) {
        BuggyState *b = &local_state.buggies[i];
        if (!b->active || b->scene_id != scene_id) continue;
        float dx = b->x - x, dy = b->y - y, dz = b->z - z;
        if ((dx*dx + dy*dy + dz*dz) <= rr) return b;
    }
    return NULL;
}

static inline int buggy_try_enter(PlayerState *p, BuggyState *b) {
    if (!p || !b || !b->active || b->occupant_player_id >= 0) return 0;
    b->occupant_player_id = p->id;
    p->in_vehicle = 1;
    p->vehicle_type = VEH_BUGGY;
    p->x = b->x; p->y = b->y; p->z = b->z;
    p->vx = p->vy = p->vz = 0.0f;
    p->on_ground = b->grounded;
    printf("[BUGGY] enter player=%d buggy=%d\n", p->id, b->id);
    return 1;
}

static inline int buggy_try_exit(PlayerState *p, BuggyState *b) {
    if (!p || !b || b->occupant_player_id != p->id) return 0;
    float r = -b->yaw * 0.0174532925f;
    float right_x = cosf(r), right_z = sinf(r);
    float offsets[3][2] = { { right_x * 3.2f, right_z * 3.2f }, { -right_x * 3.2f, -right_z * 3.2f }, { 0.0f, -3.2f } };
    for (int i = 0; i < 3; i++) {
        float px = b->x + offsets[i][0];
        float pz = b->z + offsets[i][1];
        float py = terrain_sample_height(&g_scene_terrain, px, pz);
        if (py < 0.0f) py = 0.0f;
        if (!heli_point_collides(px, py + 1.0f, pz)) {
            p->x = px; p->z = pz; p->y = py;
            break;
        }
    }
    p->in_vehicle = 0;
    p->vehicle_type = VEH_NONE;
    p->on_ground = 1;
    b->occupant_player_id = -1;
    printf("[BUGGY] exit player=%d buggy=%d persisted=1 speed=%.2f\n", p->id, b->id, sqrtf(b->vx*b->vx + b->vz*b->vz));
    return 1;
}

static inline void buggy_tick_all(void) {
    for (int i = 0; i < MAX_BUGGIES; i++) {
        BuggyState *b = &local_state.buggies[i];
        if (!b->active) continue;
        float throttle = 0.0f;
        float steer_intent = 0.0f;
        if (b->occupant_player_id >= 0 && b->occupant_player_id < MAX_CLIENTS) {
            PlayerState *occ = &local_state.players[b->occupant_player_id];
            b->scene_id = occ->scene_id;
            /* While driving a buggy:
               - occ->yaw is the driver's desired look/steer yaw (camera yaw target)
               - b->yaw is the buggy body yaw */
            throttle = occ->in_fwd;
            float yaw_err = norm_yaw_deg(occ->yaw - b->yaw);
            if (yaw_err > 180.0f) yaw_err -= 360.0f;
            if (yaw_err < -180.0f) yaw_err += 360.0f;
            steer_intent = yaw_err / 50.0f;
            float throttle_abs = fabsf(throttle);
            if (throttle_abs < 0.05f) steer_intent = 0.0f;
            if (steer_intent > 1.0f) steer_intent = 1.0f;
            if (steer_intent < -1.0f) steer_intent = -1.0f;
        } else {
            b->occupant_player_id = -1;
        }
        phys_set_scene(b->scene_id);
        simulate_buggy_state(b, throttle, steer_intent, SHANKPIT_NET_FIXED_DT, b->occupant_player_id >= 0);
        if (b->occupant_player_id >= 0 && b->occupant_player_id < MAX_CLIENTS) {
            PlayerState *occ = &local_state.players[b->occupant_player_id];
            occ->x = b->x; occ->y = b->y; occ->z = b->z;
            occ->vx = occ->vy = occ->vz = 0.0f;
            occ->on_ground = b->grounded;
        }
    }
}

static inline HelicopterState *heli_find_nearby(int scene_id, float x, float y, float z, float radius) {
    float rr = radius * radius;
    for (int i = 0; i < MAX_HELICOPTERS; i++) {
        HelicopterState *h = &local_state.helicopters[i];
        if (!h->active || h->scene_id != scene_id) continue;
        float dx = h->x - x, dy = h->y - y, dz = h->z - z;
        if ((dx * dx + dy * dy + dz * dz) <= rr) return h;
    }
    return NULL;
}

static inline int heli_try_place_player(PlayerState *p, HelicopterState *h, float ox, float oz) {
    float px = h->x + ox;
    float pz = h->z + oz;
    if (!heli_point_collides(px, h->y + 1.0f, pz) && !heli_point_collides(px, h->y + 2.0f, pz)) {
        p->x = px; p->y = h->y; p->z = pz;
        return 1;
    }
    return 0;
}

static inline void scene_request_transition(int scene_id) {
    if (local_state.transition_timer > 0) return;
    local_state.pending_scene = scene_id;
    local_state.transition_timer = 12;
}

static inline void scene_tick_transition() {
    if (local_state.transition_timer <= 0) return;
    local_state.transition_timer--;
    if (local_state.transition_timer == 0 && local_state.pending_scene >= 0) {
        scene_load(local_state.pending_scene);
        local_state.pending_scene = -1;
    }
}

static void ctf_training_on_episode_begin(void) {}
static void ctf_training_on_step(unsigned int now_ms) { (void)now_ms; }
static void ctf_training_on_episode_end(int winning_team) { (void)winning_team; }

static void ctf_add_reward(int player_id, float amount, const char *reason, CtfBotRewardBreakdown *b) {
    if (player_id < 0 || player_id >= MAX_CLIENTS) return;
    PlayerState *p = &local_state.players[player_id];
    if (!p->active || !p->is_bot) return;
    p->ctf_cumulative_reward += amount;
    p->ctf_last_reward = amount;
    p->accumulated_reward += amount;
    if (b) b->total += amount;
    if (reason && (amount != 0.0f)) {
        printf("[CTFB_REWARD] bot=%d reason=%s amount=%.2f total=%.2f\n", player_id, reason, amount, p->ctf_cumulative_reward);
    }
}

static void ctf_reset_flag(int team_id) {
    CtfFlagState *f = &local_state.ctf.flags[team_id];
    f->state = FLAG_AT_HOME;
    f->carrier_id = -1;
    f->x = f->home_x; f->y = f->home_y; f->z = f->home_z;
    f->dropped_until_ms = 0;
}

static void ctf_init_match_state(int scene_id) {
    memset(&local_state.ctf, 0, sizeof(local_state.ctf));
    local_state.ctf.active = 1;
    local_state.ctf.scene_id = scene_id;
    local_state.ctf.score_limit = CTFB_SCORE_LIMIT;
    for (int team = 0; team < 2; team++) {
        CtfFlagState *f = &local_state.ctf.flags[team];
        f->owning_team_id = team;
        f->scene_id = scene_id;
        if (!get_ctf_flag_home(scene_id, team, &f->home_x, &f->home_y, &f->home_z)) {
            float sx, sy, sz, ex, ey, ez;
            if (scene_get_team_base_marker(scene_id, team, &sx, &sy, &sz, &ex, &ey, &ez)) {
                f->home_x = sx; f->home_y = sy; f->home_z = sz;
            }
        }
        ctf_reset_flag(team);
    }
}

static void ctf_drop_flag_from_carrier(int player_id, unsigned int now_ms) {
    if (player_id < 0 || player_id >= MAX_CLIENTS) return;
    PlayerState *p = &local_state.players[player_id];
    if (p->carried_flag_team_id < 0 || p->carried_flag_team_id > 1) return;
    CtfFlagState *flag = &local_state.ctf.flags[p->carried_flag_team_id];
    flag->state = FLAG_DROPPED;
    flag->carrier_id = -1;
    flag->x = p->x; flag->y = p->y + 2.0f; flag->z = p->z;
    flag->dropped_until_ms = now_ms + CTFB_DROPPED_RETURN_MS;
    flag->last_interaction_ms = now_ms;
    p->carried_flag_team_id = -1;
}

static void ctf_schedule_respawn(PlayerState *attacker, PlayerState *victim, unsigned int now_ms, float incoming_x, float incoming_z) {
    if (!victim) return;
#if CTFB_RESPAWN_DEBUG_LOG
    int dropped_team = victim->carried_flag_team_id;
#endif
    phys_enter_death_state(attacker, victim, now_ms, mode_respawn_delay_ms(MODE_CTFB), incoming_x, incoming_z);
    victim->carried_flag_team_id = -1;
#if CTFB_RESPAWN_DEBUG_LOG
    printf("[CTFB] carrier %d died, dropped flag team %d, respawn in %d ms\n",
           victim->id, dropped_team, CTFB_RESPAWN_DELAY_MS);
#endif
}

static void build_ctf_bot_observation(int bot_id, CtfBotObservation *out) {
    memset(out, 0, sizeof(*out));
    PlayerState *p = &local_state.players[bot_id];
    int my_team = p->team_id;
    int enemy = ctf_enemy_team(my_team);
    CtfFlagState *my_flag = &local_state.ctf.flags[my_team];
    CtfFlagState *enemy_flag = &local_state.ctf.flags[enemy];
    out->bot_id = bot_id; out->my_team_id = my_team; out->enemy_team_id = enemy;
    out->has_enemy_flag = (p->carried_flag_team_id == enemy);
    out->my_flag_state = my_flag->state; out->enemy_flag_state = enemy_flag->state;
    out->my_flag_carrier_id = my_flag->carrier_id; out->enemy_flag_carrier_id = enemy_flag->carrier_id;
    float dx = enemy_flag->x - p->x, dz = enemy_flag->z - p->z;
    out->dist_enemy_flag = sqrtf(dx*dx + dz*dz);
    dx = my_flag->x - p->x; dz = my_flag->z - p->z;
    out->dist_own_flag = sqrtf(dx*dx + dz*dz);
    float cx, cy, cz, cr = CTFB_CAPTURE_RADIUS;
    if (get_ctf_capture_zone(p->scene_id, my_team, &cx, &cy, &cz, &cr)) {
        dx = cx - p->x; dz = cz - p->z; out->dist_own_capture_zone = sqrtf(dx*dx + dz*dz);
    }
    out->health = p->health;
    out->current_weapon = p->current_weapon;
    out->attack_suppressed_for_flag = out->has_enemy_flag;
    out->team_score = local_state.team_scores[my_team];
    out->enemy_score = local_state.team_scores[enemy];
}

static int select_ctf_bot_intent(PlayerState *me) {
    int my_team = me->team_id;
    int enemy = ctf_enemy_team(my_team);
    CtfFlagState *my_flag = &local_state.ctf.flags[my_team];
    CtfFlagState *enemy_flag = &local_state.ctf.flags[enemy];
    if (me->carried_flag_team_id == enemy) return CTF_BOT_INTENT_RETURN_HOME;
    if (my_flag->state == FLAG_CARRIED) return CTF_BOT_INTENT_CHASE_CARRIER;
    if (my_flag->state == FLAG_DROPPED) return CTF_BOT_INTENT_RECOVER_FLAG;
    if (enemy_flag->state == FLAG_CARRIED && team_id_is_valid(enemy_flag->carrier_id)) return CTF_BOT_INTENT_ESCORT;
    return CTF_BOT_INTENT_ATTACK_FLAG;
}

static void ctf_handle_use_interactions(PlayerState *p, unsigned int now_ms) {
    if (!p->in_use || p->use_was_down) return;
    int my_team = p->team_id;
    int enemy = ctf_enemy_team(my_team);
    CtfFlagState *enemy_flag = &local_state.ctf.flags[enemy];
    CtfFlagState *my_flag = &local_state.ctf.flags[my_team];
    if (p->carried_flag_team_id < 0 && enemy_flag->state != FLAG_CARRIED) {
        float dx = enemy_flag->x - p->x, dz = enemy_flag->z - p->z;
        if ((dx*dx + dz*dz) <= (CTFB_USE_RADIUS * CTFB_USE_RADIUS)) {
            enemy_flag->state = FLAG_CARRIED;
            enemy_flag->carrier_id = p->id;
            enemy_flag->last_interaction_ms = now_ms;
            p->carried_flag_team_id = enemy;
            ctf_add_reward(p->id, 20.0f, "pickup_enemy_flag", NULL);
            return;
        }
    }
    if (my_flag->state == FLAG_DROPPED) {
        float dx = my_flag->x - p->x, dz = my_flag->z - p->z;
        if ((dx*dx + dz*dz) <= (CTFB_USE_RADIUS * CTFB_USE_RADIUS)) {
            ctf_reset_flag(my_team);
            my_flag->last_interaction_ms = now_ms;
            ctf_add_reward(p->id, 12.0f, "return_own_flag", NULL);
            return;
        }
    }
}

static void ctf_tick_flags(unsigned int now_ms) {
    if (!local_state.ctf.active) return;
    for (int t = 0; t < 2; t++) {
        CtfFlagState *f = &local_state.ctf.flags[t];
        if (f->state == FLAG_CARRIED) {
            if (f->carrier_id < 0 || f->carrier_id >= MAX_CLIENTS || !local_state.players[f->carrier_id].active || local_state.players[f->carrier_id].state == STATE_DEAD) {
                f->state = FLAG_DROPPED;
                f->carrier_id = -1;
                f->dropped_until_ms = now_ms + CTFB_DROPPED_RETURN_MS;
            } else {
                PlayerState *carrier = &local_state.players[f->carrier_id];
                f->x = carrier->x; f->y = carrier->y + 10.0f; f->z = carrier->z;
            }
        } else if (f->state == FLAG_DROPPED && now_ms >= f->dropped_until_ms) {
            ctf_reset_flag(t);
        }
    }
}

static void ctf_try_capture(PlayerState *p, unsigned int now_ms) {
    int my_team = p->team_id;
    int enemy = ctf_enemy_team(my_team);
    if (p->carried_flag_team_id != enemy) return;
    CtfFlagState *my_flag = &local_state.ctf.flags[my_team];
    if (my_flag->state != FLAG_AT_HOME) return;
    float cx, cy, cz, radius = CTFB_CAPTURE_RADIUS;
    if (!get_ctf_capture_zone(p->scene_id, my_team, &cx, &cy, &cz, &radius)) return;
    float dx = cx - p->x, dz = cz - p->z;
    if ((dx*dx + dz*dz) > (radius * radius)) return;
    local_state.team_scores[my_team]++;
    local_state.ctf.capture_scores[my_team] = local_state.team_scores[my_team];
    local_state.ctf.event_counter++;
    ctf_add_reward(p->id, 120.0f, "capture_flag", NULL);
    ctf_reset_flag(enemy);
    p->carried_flag_team_id = -1;
    if (local_state.team_scores[my_team] >= local_state.score_limit) {
        local_state.match_over = 1;
        local_state.winning_team = my_team;
        ctf_training_on_episode_end(my_team);
    }
    (void)now_ms;
}

static void ctf_try_carry_melee(PlayerState *attacker, unsigned int now_ms) {
    if (attacker->carried_flag_team_id < 0) return;
    if (attacker->ctf_melee_cooldown_ms > now_ms) return;
    /* Reuse knife melee envelope (trace + range gate); CTFB keeps its own damage/cooldown as source of truth. */
    attacker->ctf_melee_cooldown_ms = now_ms + CTFB_CARRY_MELEE_COOLDOWN_MS;
    attacker->is_shooting = 3;
    phys_try_melee_strike(attacker, local_state.players, CTFB_CARRY_MELEE_DAMAGE, 16, 0, now_ms, mode_respawn_delay_ms(local_state.game_mode));
}

static float story_cutscene_lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

static void story_cutscene_clear_runtime(StoryCutsceneState *cs) {
    memset(cs, 0, sizeof(*cs));
    cs->id = STORY_CUTSCENE_NONE;
}

static const char *story_cutscene_name(int id) {
    switch (id) {
        case STORY_CUTSCENE_INTRO: return "INTRO";
        case STORY_CUTSCENE_BOSS_DEFEAT_BREACH: return "BOSS_DEFEAT_BREACH";
        default: return "NONE";
    }
}

static const char *story_puppet_name(int type) {
    switch (type) {
        case STORY_PUPPET_RIFT_HOUND: return "RIFT_HOUND";
        case STORY_PUPPET_SHAMBLER_TROOPER: return "SHAMBLER_TROOPER";
        case STORY_PUPPET_SHRIEKER: return "SHRIEKER";
        case STORY_PUPPET_GORE_BRUTE: return "GORE_BRUTE";
        case STORY_PUPPET_PORTAL_SHEPHERD: return "PORTAL_SHEPHERD";
        default: return "UNKNOWN";
    }
}

static void story_cutscene_set_shot(StoryCutsceneShot *shot,
                                    unsigned int start_ms, unsigned int end_ms,
                                    float sx, float sy, float sz,
                                    float ex, float ey, float ez,
                                    float slx, float sly, float slz,
                                    float elx, float ely, float elz,
                                    int lerp_mode) {
    shot->start_ms = start_ms;
    shot->end_ms = end_ms;
    shot->start_pos[0] = sx; shot->start_pos[1] = sy; shot->start_pos[2] = sz;
    shot->end_pos[0] = ex; shot->end_pos[1] = ey; shot->end_pos[2] = ez;
    shot->start_look[0] = slx; shot->start_look[1] = sly; shot->start_look[2] = slz;
    shot->end_look[0] = elx; shot->end_look[1] = ely; shot->end_look[2] = elz;
    shot->lerp_mode = lerp_mode;
}

static StoryPuppetActor *story_cutscene_spawn_puppet(StoryCutsceneState *cs, int type, int move_mode,
                                                      float x, float y, float z, float yaw,
                                                      float scale_x, float scale_y, float scale_z,
                                                      unsigned int now_ms, unsigned int lifetime_ms,
                                                      float vel_x, float vel_y, float vel_z, float jitter_amp) {
    for (int i = 0; i < STORY_CUTSCENE_MAX_PUPPETS; i++) {
        StoryPuppetActor *p = &cs->puppets[i];
        if (p->active) continue;
        memset(p, 0, sizeof(*p));
        p->active = 1;
        p->type = type;
        p->move_mode = move_mode;
        p->x = x; p->y = y; p->z = z;
        p->base_y = y;
        p->yaw = yaw;
        p->scale_x = scale_x; p->scale_y = scale_y; p->scale_z = scale_z;
        p->spawn_time_ms = now_ms;
        p->lifetime_ms = lifetime_ms;
        p->vel_x = vel_x; p->vel_y = vel_y; p->vel_z = vel_z;
        p->jitter_amp = jitter_amp;
        cs->puppet_count++;
        printf("[CUTSCENE] spawn puppet type=%s slot=%d\n", story_puppet_name(type), i);
        return p;
    }
    return NULL;
}

static void story_cutscene_start_intro(unsigned int now_ms) {
    StoryCutsceneState *cs = &local_state.story_cutscene;
    story_cutscene_clear_runtime(cs);
    cs->active = 1;
    cs->id = STORY_CUTSCENE_INTRO;
    cs->start_ms = now_ms;
    cs->portal.active = 0;
    cs->shot_count = 2;
    StoryBossState *boss = &local_state.story_boss;
    story_cutscene_set_shot(&cs->shots[0], 0, 2500,
                            boss->x + 180.0f, boss->y + 88.0f, boss->z + 220.0f,
                            boss->x + 180.0f, boss->y + 88.0f, boss->z + 220.0f,
                            boss->x, boss->y + 24.0f, boss->z,
                            boss->x, boss->y + 24.0f, boss->z, 0);
    story_cutscene_set_shot(&cs->shots[1], 2500, STORY_INTRO_CUTSCENE_DURATION_MS,
                            boss->x + 220.0f, boss->y + 120.0f, boss->z + 260.0f,
                            boss->x + 150.0f, boss->y + 92.0f, boss->z + 180.0f,
                            boss->x, boss->y + 30.0f, boss->z,
                            boss->x, boss->y + 20.0f, boss->z, 1);
    cs->camera.shot_index = -1;
    printf("[CUTSCENE] start id=%s\n", story_cutscene_name(cs->id));
}

static void story_cutscene_start_breach(unsigned int now_ms) {
    StoryCutsceneState *cs = &local_state.story_cutscene;
    StoryBossState *boss = &local_state.story_boss;
    story_cutscene_clear_runtime(cs);
    cs->active = 1;
    cs->id = STORY_CUTSCENE_BOSS_DEFEAT_BREACH;
    cs->start_ms = now_ms;
    cs->portal.active = 1;
    cs->portal.x = boss->x;
    cs->portal.y = boss->y + 140.0f;
    cs->portal.z = boss->z - 40.0f;
    cs->portal.radius = 5.0f;
    cs->shot_count = 8;
    cs->camera.shot_index = -1;

    story_cutscene_set_shot(&cs->shots[0], 0, 1000,
                            boss->x + 120.0f, boss->y + 65.0f, boss->z + 170.0f,
                            boss->x + 120.0f, boss->y + 65.0f, boss->z + 170.0f,
                            boss->x, boss->y + 20.0f, boss->z,
                            boss->x, boss->y + 20.0f, boss->z, 0);
    story_cutscene_set_shot(&cs->shots[1], 1000, 3000,
                            boss->x + 170.0f, boss->y + 110.0f, boss->z + 260.0f,
                            boss->x + 210.0f, boss->y + 140.0f, boss->z + 300.0f,
                            cs->portal.x, cs->portal.y - 25.0f, cs->portal.z,
                            cs->portal.x, cs->portal.y, cs->portal.z, 1);
    story_cutscene_set_shot(&cs->shots[2], 3000, 6000,
                            boss->x + 190.0f, boss->y + 120.0f, boss->z + 280.0f,
                            boss->x + 110.0f, boss->y + 100.0f, boss->z + 215.0f,
                            cs->portal.x, cs->portal.y, cs->portal.z,
                            cs->portal.x, cs->portal.y, cs->portal.z, 1);
    story_cutscene_set_shot(&cs->shots[3], 6000, 9000,
                            boss->x + 70.0f, boss->y + 64.0f, boss->z + 145.0f,
                            boss->x + 10.0f, boss->y + 52.0f, boss->z + 120.0f,
                            boss->x + 20.0f, boss->y + 20.0f, boss->z + 20.0f,
                            boss->x + 8.0f, boss->y + 20.0f, boss->z + 10.0f, 1);
    story_cutscene_set_shot(&cs->shots[4], 9000, 12000,
                            boss->x + 130.0f, boss->y + 80.0f, boss->z + 230.0f,
                            boss->x + 75.0f, boss->y + 68.0f, boss->z + 182.0f,
                            cs->portal.x, cs->portal.y - 10.0f, cs->portal.z,
                            cs->portal.x, cs->portal.y - 8.0f, cs->portal.z, 1);
    story_cutscene_set_shot(&cs->shots[5], 12000, 15000,
                            boss->x + 90.0f, boss->y + 70.0f, boss->z + 165.0f,
                            boss->x + 60.0f, boss->y + 64.0f, boss->z + 146.0f,
                            boss->x + 16.0f, boss->y + 20.0f, boss->z + 20.0f,
                            boss->x + 10.0f, boss->y + 20.0f, boss->z + 20.0f, 1);
    story_cutscene_set_shot(&cs->shots[6], 15000, 18000,
                            boss->x + 165.0f, boss->y + 100.0f, boss->z + 220.0f,
                            boss->x + 122.0f, boss->y + 90.0f, boss->z + 190.0f,
                            cs->portal.x, cs->portal.y + 10.0f, cs->portal.z,
                            cs->portal.x, cs->portal.y + 8.0f, cs->portal.z, 1);
    story_cutscene_set_shot(&cs->shots[7], 18000, STORY_BREACH_CUTSCENE_DURATION_MS,
                            boss->x + 160.0f, boss->y + 92.0f, boss->z + 215.0f,
                            boss->x + 138.0f, boss->y + 88.0f, boss->z + 205.0f,
                            cs->portal.x, cs->portal.y + 6.0f, cs->portal.z,
                            cs->portal.x, cs->portal.y + 6.0f, cs->portal.z, 1);
    printf("[CUTSCENE] start id=%s\n", story_cutscene_name(cs->id));
}

static void story_cutscene_update(unsigned int now_ms) {
    StoryCutsceneState *cs = &local_state.story_cutscene;
    if (!cs->active) return;
    cs->elapsed_ms = now_ms - cs->start_ms;
    unsigned int t_ms = cs->elapsed_ms;

    int shot_index = cs->shot_count - 1;
    for (int i = 0; i < cs->shot_count; i++) {
        if (t_ms >= cs->shots[i].start_ms && t_ms < cs->shots[i].end_ms) {
            shot_index = i;
            break;
        }
    }
    if (shot_index != cs->camera.shot_index) {
        cs->camera.shot_index = shot_index;
        printf("[CUTSCENE] shot change id=%s shot=%d\n", story_cutscene_name(cs->id), shot_index);
    }

    StoryCutsceneShot *shot = &cs->shots[shot_index];
    float t = 0.0f;
    if (shot->end_ms > shot->start_ms) {
        t = (float)(t_ms - shot->start_ms) / (float)(shot->end_ms - shot->start_ms);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
    }
    cs->camera.active = 1;
    if (shot->lerp_mode) {
        for (int k = 0; k < 3; k++) {
            cs->camera.pos[k] = story_cutscene_lerp(shot->start_pos[k], shot->end_pos[k], t);
            cs->camera.look[k] = story_cutscene_lerp(shot->start_look[k], shot->end_look[k], t);
        }
    } else {
        for (int k = 0; k < 3; k++) {
            cs->camera.pos[k] = shot->start_pos[k];
            cs->camera.look[k] = shot->start_look[k];
        }
    }

    cs->shake_amp = 0.0f;
    cs->glow_pulse = 0.0f;
    if (cs->id == STORY_CUTSCENE_BOSS_DEFEAT_BREACH) {
        StoryPortalFx *portal = &cs->portal;
        if (t_ms >= 1000 && t_ms < 3000) {
            float stage = (float)(t_ms - 1000) / 2000.0f;
            portal->rupture_alpha = stage;
            portal->open_alpha = stage * 0.35f;
            portal->radius = 7.0f + stage * 20.0f;
            cs->shake_amp = 1.8f + stage * 2.4f;
            cs->glow_pulse = stage;
        } else if (t_ms >= 3000) {
            float stage = (float)(t_ms - 3000) / 3000.0f;
            if (stage > 1.0f) stage = 1.0f;
            portal->rupture_alpha = 1.0f;
            portal->open_alpha = 0.35f + stage * 0.65f;
            portal->radius = 27.0f + stage * 45.0f;
            cs->glow_pulse = 0.7f + 0.3f * sinf((float)t_ms * 0.01f);
        }
        if (t_ms >= 12000 && t_ms < 15000) cs->shake_amp = 2.8f;
        portal->pulse = 0.5f + 0.5f * sinf((float)t_ms * 0.0085f);
        portal->spin_deg += 0.28f + portal->open_alpha * 1.2f;

        StoryBossState *boss = &local_state.story_boss;
        if ((cs->event_flags & 1) == 0 && t_ms >= 6000) {
            cs->event_flags |= 1;
            for (int i = 0; i < 8; i++) {
                float lane = (float)i - 3.5f;
                story_cutscene_spawn_puppet(cs, STORY_PUPPET_RIFT_HOUND, STORY_PUPPET_MOVE_BURST,
                                            boss->x + lane * 5.6f, voxworld_height_at(boss->x + lane * 5.6f, boss->z + 26.0f) + 2.5f, boss->z + 26.0f,
                                            180.0f, 1.0f, 1.0f, 1.0f, now_ms, 11000,
                                            lane * 0.16f, 0.0f, 7.0f + ((i % 2) ? 0.9f : -0.5f), 0.7f);
            }
        }
        if ((cs->event_flags & 2) == 0 && t_ms >= 9000) {
            cs->event_flags |= 2;
            for (int i = 0; i < 5; i++) {
                float lane = (float)i - 2.0f;
                story_cutscene_spawn_puppet(cs, STORY_PUPPET_SHAMBLER_TROOPER, STORY_PUPPET_MOVE_HEAVY_WALK,
                                            boss->x + lane * 9.0f, voxworld_height_at(boss->x + lane * 9.0f, boss->z + 28.0f) + 3.5f, boss->z + 28.0f,
                                            180.0f, 1.25f, 1.25f, 1.25f, now_ms, 12000,
                                            lane * 0.08f, 0.0f, 3.3f, 0.0f);
            }
            for (int i = 0; i < 3; i++) {
                float side = (i == 0) ? -1.0f : (i == 1 ? 1.0f : 0.0f);
                story_cutscene_spawn_puppet(cs, STORY_PUPPET_SHRIEKER, STORY_PUPPET_MOVE_HOVER,
                                            boss->x + side * 34.0f, boss->y + 88.0f + (float)i * 7.0f, boss->z + 8.0f + (float)i * 8.0f,
                                            180.0f, 1.12f, 1.12f, 1.12f, now_ms, 12000,
                                            side * 0.45f, 0.0f, 0.3f + 0.2f * (float)i, 0.0f);
            }
        }
        if ((cs->event_flags & 4) == 0 && t_ms >= 12000) {
            cs->event_flags |= 4;
            story_cutscene_spawn_puppet(cs, STORY_PUPPET_GORE_BRUTE, STORY_PUPPET_MOVE_STOMP,
                                        boss->x + 8.0f, voxworld_height_at(boss->x + 8.0f, boss->z + 32.0f) + 4.8f, boss->z + 32.0f,
                                        180.0f, 1.9f, 1.9f, 1.9f, now_ms, 12000,
                                        0.0f, 0.0f, 2.2f, 0.0f);
        }
        if ((cs->event_flags & 8) == 0 && t_ms >= 15000) {
            cs->event_flags |= 8;
            story_cutscene_spawn_puppet(cs, STORY_PUPPET_PORTAL_SHEPHERD, STORY_PUPPET_MOVE_PRESENCE,
                                        boss->x, boss->y + 84.0f, boss->z + 2.0f,
                                        180.0f, 1.5f, 1.5f, 1.5f, now_ms, 12000,
                                        0.0f, 0.2f, 0.0f, 0.0f);
        }
    }

    for (int i = 0; i < STORY_CUTSCENE_MAX_PUPPETS; i++) {
        StoryPuppetActor *p = &cs->puppets[i];
        if (!p->active) continue;
        unsigned int alive_ms = now_ms - p->spawn_time_ms;
        if (p->lifetime_ms > 0 && alive_ms >= p->lifetime_ms) {
            p->active = 0;
            continue;
        }
        float dt = SHANKPIT_NET_FIXED_DT;
        p->x += p->vel_x * dt;
        p->z += p->vel_z * dt;
        if (p->move_mode == STORY_PUPPET_MOVE_BURST) {
            p->x += sinf((float)alive_ms * 0.03f + (float)i) * p->jitter_amp * 0.08f;
        } else if (p->move_mode == STORY_PUPPET_MOVE_HOVER) {
            p->y = p->base_y + sinf((float)alive_ms * 0.007f + (float)i) * 2.6f;
        } else if (p->move_mode == STORY_PUPPET_MOVE_STOMP) {
            p->y = p->base_y + fabsf(sinf((float)alive_ms * 0.0045f)) * 1.5f;
        } else if (p->move_mode == STORY_PUPPET_MOVE_PRESENCE) {
            p->y = p->base_y + sinf((float)alive_ms * 0.0035f) * 1.4f;
        }
    }

    if (!cs->finished) {
        unsigned int end_ms = (cs->id == STORY_CUTSCENE_INTRO) ? STORY_INTRO_CUTSCENE_DURATION_MS : STORY_BREACH_CUTSCENE_DURATION_MS;
        if (t_ms >= end_ms) {
            cs->finished = 1;
            cs->active = 0;
            printf("[CUTSCENE] finished id=%s\n", story_cutscene_name(cs->id));
        }
    }
}

static float story_boss_weapon_damage(int weapon) {
    if (weapon >= 0 && weapon < MAX_WEAPONS) return (float)WPN_STATS[weapon].dmg;
    return 8.0f;
}

static int story_boss_is_targeted(const PlayerState *hero, const StoryBossState *boss, float max_dist, float cone_dot) {
    float to_x = boss->x - hero->x;
    float to_y = (boss->y + 36.0f) - (hero->y + EYE_HEIGHT);
    float to_z = boss->z - hero->z;
    float dist = sqrtf(to_x * to_x + to_y * to_y + to_z * to_z);
    if (dist <= 0.001f || dist > max_dist) return 0;
    float r = -hero->yaw * 0.0174533f;
    float rp = hero->pitch * 0.0174533f;
    float fx = sinf(r) * cosf(rp);
    float fy = sinf(rp);
    float fz = -cosf(r) * cosf(rp);
    float inv = 1.0f / dist;
    float dot = fx * (to_x * inv) + fy * (to_y * inv) + fz * (to_z * inv);
    return dot >= cone_dot;
}

static void story_boss_apply_player_hit(PlayerState *hero, unsigned int now_ms) {
    StoryBossState *boss = &local_state.story_boss;
    if (local_state.game_mode != MODE_STORY || local_state.story_phase != STORY_PHASE_BOSS_PLAYING) return;
    if (!boss->active || boss->defeated || hero->state == STATE_DEAD) return;
    int weapon = hero->current_weapon;
    float max_dist = 560.0f;
    float cone_dot = 0.93f;
    if (weapon == WPN_SNIPER) cone_dot = 0.88f;
    else if (weapon == WPN_SHOTGUN) cone_dot = 0.95f;
    else if (weapon == WPN_KNIFE || weapon == WPN_KATANA) {
        max_dist = 24.0f;
        cone_dot = 0.72f;
    }
    if (!story_boss_is_targeted(hero, boss, max_dist, cone_dot)) return;
    float dmg = story_boss_weapon_damage(weapon);
    boss->health -= dmg;
    if (boss->health < 0.0f) boss->health = 0.0f;
    boss->hurt_flash_until_ms = now_ms + 130;
    static unsigned int next_damage_log_ms = 0;
    if (now_ms >= next_damage_log_ms) {
        printf("[STORY] boss damaged %.1f hp=%.1f/%.1f\n", dmg, boss->health, boss->max_health);
        next_damage_log_ms = now_ms + 350;
    }
    if (boss->health <= 0.0f) {
        boss->defeated = 1;
        boss->active = 0;
        local_state.story_phase = STORY_PHASE_POST_BOSS_BREACH_CUTSCENE;
        local_state.story_phase_start_ms = now_ms;
        story_cutscene_start_breach(now_ms);
        printf("[STORY] boss defeated -> breach cutscene\n");
    }
}

static void story_boss_tick(PlayerState *hero, unsigned int now_ms) {
    StoryBossState *boss = &local_state.story_boss;
    if (local_state.game_mode != MODE_STORY || local_state.story_phase != STORY_PHASE_BOSS_PLAYING) return;
    if (!boss->active || boss->defeated || hero->state == STATE_DEAD) return;
    unsigned int cooldown = STORY_BOSS_ATTACK_MIN_MS + (unsigned int)(((boss->x + boss->z) < 0.0f ? -boss->z : boss->z));
    cooldown = STORY_BOSS_ATTACK_MIN_MS + (cooldown % (STORY_BOSS_ATTACK_MAX_MS - STORY_BOSS_ATTACK_MIN_MS + 1U));
    if (now_ms - boss->last_attack_ms < cooldown) return;
    boss->last_attack_ms = now_ms;
    boss->hurt_flash_until_ms = now_ms + 180;
    float dx = hero->x - boss->x;
    float dz = hero->z - boss->z;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist <= STORY_BOSS_ATTACK_RADIUS) {
        int damage = 26;
        hero->shield_regen_timer = SHIELD_REGEN_DELAY;
        if (hero->shield > 0) {
            if (hero->shield >= damage) { hero->shield -= damage; damage = 0; }
            else { damage -= hero->shield; hero->shield = 0; }
        }
        hero->health -= damage;
        if (hero->health <= 0) {
            hero->health = 0;
            phys_enter_death_state(NULL, hero, now_ms, mode_respawn_delay_ms(local_state.game_mode), 0.0f, 0.0f);
            local_state.story_phase = STORY_PHASE_FAILED;
            local_state.story_phase_start_ms = now_ms;
            local_state.match_over = 1;
        }
    }
}

// --- BOT AI ---
void bot_think(int bot_idx, PlayerState *players, float *out_fwd, float *out_yaw, int *out_buttons) {
    PlayerState *me = &players[bot_idx];
    if (me->state == STATE_DEAD || local_state.match_over) { *out_buttons = 0; return; }
    if (local_state.game_mode == MODE_STORY) { *out_buttons = 0; *out_fwd = 0.0f; *out_yaw = me->yaw; return; }
    if (local_state.game_mode == MODE_CTFB && local_state.ctf.active && team_id_is_valid(me->team_id)) {
        CtfBotObservation obs;
        build_ctf_bot_observation(bot_idx, &obs);
        (void)obs;
        int intent = select_ctf_bot_intent(me);
        me->ctf_bot_intent = intent;
        int my_team = me->team_id;
        int enemy = ctf_enemy_team(my_team);
        float tx = 0.0f, tz = 0.0f;
        if (intent == CTF_BOT_INTENT_RETURN_HOME) {
            float cy, cr;
            get_ctf_capture_zone(me->scene_id, my_team, &tx, &cy, &tz, &cr);
        } else if (intent == CTF_BOT_INTENT_CHASE_CARRIER && local_state.ctf.flags[my_team].carrier_id >= 0) {
            PlayerState *carrier = &players[local_state.ctf.flags[my_team].carrier_id];
            tx = carrier->x; tz = carrier->z;
        } else if (intent == CTF_BOT_INTENT_RECOVER_FLAG) {
            tx = local_state.ctf.flags[my_team].x;
            tz = local_state.ctf.flags[my_team].z;
        } else if (intent == CTF_BOT_INTENT_ESCORT && local_state.ctf.flags[enemy].carrier_id >= 0) {
            PlayerState *carrier = &players[local_state.ctf.flags[enemy].carrier_id];
            tx = carrier->x; tz = carrier->z;
        } else {
            tx = local_state.ctf.flags[enemy].x;
            tz = local_state.ctf.flags[enemy].z;
        }
        float dx = tx - me->x;
        float dz = tz - me->z;
        float target_yaw = atan2f(dx, dz) * (180.0f / 3.14159f);
        float diff = angle_diff(target_yaw, *out_yaw);
        if (diff > 8.0f) diff = 8.0f;
        if (diff < -8.0f) diff = -8.0f;
        *out_yaw += diff;
        *out_fwd = 0.9f;
        float dist_sq = dx*dx + dz*dz;
        if (dist_sq < (CTFB_USE_RADIUS * CTFB_USE_RADIUS)) *out_buttons |= BTN_USE;
    }

    int team_mode = (local_state.game_mode == MODE_TDM || local_state.game_mode == MODE_CTF || local_state.game_mode == MODE_TDMB || local_state.game_mode == MODE_TDMO || local_state.game_mode == MODE_CTFB);
    int target_idx = -1;
    float min_dist = 9999.0f;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (i == bot_idx) continue;
        if (!players[i].active) continue;
        if (players[i].state == STATE_DEAD) continue;
        if (team_mode && players[i].team_id == me->team_id) continue;
        
        float dx = players[i].x - me->x;
        float dz = players[i].z - me->z;
        float dist = sqrtf(dx*dx + dz*dz);
        
        if (i == 0 || dist < min_dist) { 
            if (i == 0 && (!team_mode || players[0].team_id != me->team_id)) dist *= 0.5f;
            if (dist < min_dist) { min_dist = dist; target_idx = i; }
        }
    }

    if (target_idx != -1) {
        PlayerState *t = &players[target_idx];
        float dx = t->x - me->x;
        float dz = t->z - me->z;
        float target_yaw = atan2f(dx, dz) * (180.0f / 3.14159f);
        
        float turn_speed = (me->brain.w_turret > 1.0f) ? me->brain.w_turret : 10.0f;
        float diff = angle_diff(target_yaw, *out_yaw);
        if (diff > turn_speed) diff = turn_speed;
        if (diff < -turn_speed) diff = -turn_speed;
        *out_yaw += diff;
        
        *out_buttons |= BTN_ATTACK;
        
        if (min_dist > 15.0f) *out_fwd = me->brain.w_aggro;
        else if (min_dist < 5.0f) *out_fwd = -me->brain.w_aggro; 
        else *out_fwd = 0.2f; 
        
        *out_yaw += me->brain.w_strafe * 10.0f;
        if (me->on_ground && (rand()%1000 < (me->brain.w_jump * 1000.0f))) *out_buttons |= BTN_JUMP;
        if (me->on_ground && (rand()%1000 < (me->brain.w_slide * 1000.0f))) *out_buttons |= BTN_CROUCH;
        if (me->ammo[me->current_weapon] <= 0) *out_buttons |= BTN_RELOAD;
    } else {
        *out_yaw += 2.0f;
        *out_fwd = 0.5f;
    }
}

// --- UPDATE LOOP ---
void update_entity(PlayerState *p, float dt, void *server_context, unsigned int cmd_time) {
    if (!p->active) return;

    phys_set_scene(p->scene_id);

    if (p->state == STATE_DEAD) {
        p->in_shoot = 0;
        p->in_reload = 0;
        p->in_use = 0;
        p->in_jump = 0;
        p->in_ability = 0;
        apply_friction(p);
        p->vy -= GRAVITY_DROP;
        p->y += p->vy;
        resolve_collision(p);
        p->x += p->vx;
        p->z += p->vz;
        if (p->hit_feedback > 0) p->hit_feedback--;
        if (p->recoil_anim > 0.0f) p->recoil_anim -= 0.1f;
        if (p->recoil_anim < 0.0f) p->recoil_anim = 0.0f;
        return;
    }

    if (cmd_time < p->stunned_until_ms) {
        p->in_fwd = 0.0f;
        p->in_strafe = 0.0f;
        p->in_jump = 0;
        p->in_shoot = 0;
        p->in_reload = 0;
        p->in_use = 0;
        p->in_ability = 0;
        p->vx = 0.0f;
        p->vz = 0.0f;
    }

    apply_friction(p);
    float g = (p->in_jump) ? GRAVITY_FLOAT : GRAVITY_DROP;
    if (p->dash_timer <= 0) p->vy -= g; 
    p->y += p->vy;
    
    resolve_collision(p);
    if (p->dash_timer > 0) {
        float nx = 0.0f, ny = 0.0f, nz = 0.0f;
        float hit_x = 0.0f, hit_y = 0.0f, hit_z = 0.0f;
        float next_x = p->x + p->vx;
        float next_y = p->y;
        float next_z = p->z + p->vz;
        if (trace_map(p->x, p->y + 1.0f, p->z, next_x, next_y + 1.0f, next_z, &hit_x, &hit_y, &hit_z, &nx, &ny, &nz)) {
            p->x = hit_x;
            p->z = hit_z;
            p->dash_timer = 0;
            p->dash_vx = p->dash_vy = p->dash_vz = 0.0f;
            p->vx = 0.0f; p->vy = 0.0f; p->vz = 0.0f;
        } else {
            p->x = next_x;
            p->z = next_z;
        }
    } else {
        p->x += p->vx;
        p->z += p->vz;
    }

    if (p->recoil_anim > 0) p->recoil_anim -= 0.1f;
    if (p->recoil_anim < 0) p->recoil_anim = 0;
    if (p->hit_feedback > 0) p->hit_feedback--;

    update_weapons(p, local_state.players, local_state.projectiles, p->in_shoot > 0, p->in_reload > 0, p->in_ability > 0, cmd_time, mode_respawn_delay_ms(local_state.game_mode));
    scene_safety_check(p);
}

static void apply_projectile_damage(PlayerState *owner, PlayerState *target, int damage, unsigned int now_ms, float hit_vx, float hit_vz) {
    if (!target->active || target->state == STATE_DEAD) return;
    int team_mode = (local_state.game_mode == MODE_TDM || local_state.game_mode == MODE_CTF || local_state.game_mode == MODE_TDMB || local_state.game_mode == MODE_TDMO || local_state.game_mode == MODE_CTFB);
    if (owner && team_mode && owner->team_id == target->team_id) return;
    target->shield_regen_timer = SHIELD_REGEN_DELAY;
    if (target->shield > 0) {
        if (target->shield >= damage) { target->shield -= damage; damage = 0; }
        else { damage -= target->shield; target->shield = 0; }
    }
    target->health -= damage;
    if (target->health <= 0) {
        if (local_state.game_mode == MODE_CTFB) {
            int carried_flag_team_id = target->carried_flag_team_id;
            ctf_drop_flag_from_carrier(target->id, now_ms);
            if (owner && local_state.ctf.flags[target->team_id].carrier_id == target->id) ctf_add_reward(owner->id, 25.0f, "kill_enemy_carrier", NULL);
            if (carried_flag_team_id >= 0) ctf_add_reward(target->id, -20.0f, "died_with_flag", NULL);
        }
        if (owner) {
            owner->accumulated_reward += 500.0f;
        }
        phys_enter_death_state(owner, target, now_ms, mode_respawn_delay_ms(local_state.game_mode), hit_vx, hit_vz);
    }

    if (now_ms >= target->stun_immune_until_ms) {
        unsigned int stun_end = now_ms + 100;
        if (stun_end > target->stunned_until_ms) target->stunned_until_ms = stun_end;
        target->stun_immune_until_ms = now_ms + 250;
    }
}

static void update_projectiles(unsigned int now_ms) {
    for (int i=0; i<MAX_PROJECTILES; i++) {
        Projectile *p = &local_state.projectiles[i];
        if (!p->active) continue;

        phys_set_scene(p->scene_id);

        float next_x = p->x + p->vx;
        float next_y = p->y + p->vy;
        float next_z = p->z + p->vz;

        float hit_x, hit_y, hit_z, nx, ny, nz;
        if (trace_map(p->x, p->y, p->z, next_x, next_y, next_z, &hit_x, &hit_y, &hit_z, &nx, &ny, &nz)) {
            if (p->bounces_left > 0) {
                reflect_vector(&p->vx, &p->vy, &p->vz, nx, ny, nz);
                p->x = hit_x; p->y = hit_y; p->z = hit_z;
                p->bounces_left--;
            } else {
                p->active = 0;
            }
        } else {
            p->x = next_x; p->y = next_y; p->z = next_z;
        }

        if (p->active) {
            for (int t = 0; t < MAX_CLIENTS; t++) {
                PlayerState *target = &local_state.players[t];
                if (!target->active || target->state == STATE_DEAD) continue;
                if (t == p->owner_id) continue;
                if (target->scene_id != p->scene_id) continue;
                float dx = target->x - p->x;
                float dy = (target->y + EYE_HEIGHT) - p->y;
                float dz = target->z - p->z;
                float dist_sq = dx * dx + dy * dy + dz * dz;
                if (dist_sq < 4.0f) {
                    PlayerState *owner = NULL;
                    if (p->owner_id >= 0 && p->owner_id < MAX_CLIENTS) {
                        owner = &local_state.players[p->owner_id];
                    }
                    apply_projectile_damage(owner, target, p->damage, now_ms, p->vx, p->vz);
                    p->active = 0;
                    break;
                }
            }
        }

        if (p->x > 4000 || p->x < -4000 || p->z > 4000 || p->z < -4000 || p->y > 2000) p->active = 0;
    }
}

void local_update(float fwd, float str, float yaw, float pitch, int shoot, int weapon_req, int jump, int crouch, int reload, int ability, void *server_context, unsigned int cmd_time) {
    PlayerState *p0 = &local_state.players[0];
    if (local_state.game_mode == MODE_STORY) {
        if (local_state.story_phase_start_ms == 0) {
            local_state.story_phase_start_ms = cmd_time;
            if (local_state.story_cutscene.active && local_state.story_cutscene.start_ms == 0) {
                local_state.story_cutscene.start_ms = cmd_time;
            }
        }
        if (local_state.story_phase == STORY_PHASE_INTRO_CUTSCENE ||
            local_state.story_phase == STORY_PHASE_POST_BOSS_BREACH_CUTSCENE) {
            fwd = 0.0f; str = 0.0f; shoot = 0; jump = 0; crouch = 0; reload = 0; ability = 0;
            story_cutscene_update(cmd_time);
            if (local_state.story_cutscene.camera.active) {
                float dx = local_state.story_cutscene.camera.look[0] - p0->x;
                float dz = local_state.story_cutscene.camera.look[2] - p0->z;
                yaw = atan2f(dx, dz) * (180.0f / 3.14159f);
            }
            if (local_state.story_phase == STORY_PHASE_INTRO_CUTSCENE &&
                local_state.story_cutscene.finished &&
                local_state.story_cutscene.id == STORY_CUTSCENE_INTRO) {
                local_state.story_phase = STORY_PHASE_BOSS_PLAYING;
                local_state.story_phase_start_ms = cmd_time;
                printf("[STORY] intro cutscene complete -> boss phase\n");
            }
            if (local_state.story_phase == STORY_PHASE_POST_BOSS_BREACH_CUTSCENE &&
                local_state.story_cutscene.finished &&
                local_state.story_cutscene.id == STORY_CUTSCENE_BOSS_DEFEAT_BREACH &&
                !local_state.story_cutscene.completion_marked) {
                local_state.story_cutscene.completion_marked = 1;
                local_state.story_phase = STORY_PHASE_COMPLETE;
                local_state.story_phase_start_ms = cmd_time;
                local_state.match_over = 1;
                printf("[STORY] breach cutscene complete -> story complete\n");
            }
        } else if (local_state.story_phase == STORY_PHASE_COMPLETE || local_state.story_phase == STORY_PHASE_FAILED) {
            fwd = 0.0f; str = 0.0f; shoot = 0; jump = 0; crouch = 0; reload = 0; ability = 0;
        }
    }
    if (local_state.match_over && (local_state.game_mode == MODE_TDMB || local_state.game_mode == MODE_CTFB)) {
        fwd = 0.0f; str = 0.0f; shoot = 0; jump = 0; crouch = 0; reload = 0; ability = 0;
    }
    scene_tick_transition();
    if (local_state.transition_timer > 0) {
        fwd = 0.0f;
        str = 0.0f;
        shoot = 0;
        jump = 0;
        crouch = 0;
        reload = 0;
        ability = 0;
    }
    if (!(local_state.game_mode == MODE_STORY &&
          (local_state.story_phase == STORY_PHASE_INTRO_CUTSCENE ||
           local_state.story_phase == STORY_PHASE_POST_BOSS_BREACH_CUTSCENE))) {
        p0->yaw = yaw; p0->pitch = pitch;
    }
    p0->in_fwd = fwd;
    p0->in_strafe = str;
    if (weapon_req >= 0 && weapon_req < MAX_WEAPONS) p0->current_weapon = weapon_req;
    if (p0->state == STATE_DEAD) {
        fwd = 0.0f; str = 0.0f; shoot = 0; jump = 0; crouch = 0; reload = 0; ability = 0;
    }
    if (p0->state != STATE_DEAD && !(p0->in_vehicle && p0->vehicle_type == VEH_HELICOPTER) &&
        !(p0->in_vehicle && p0->vehicle_type == VEH_BUGGY)) {
        {
            MoveIntent move_intent = {
                .forward = fwd,
                .strafe = str,
                .control_yaw_deg = yaw,
                .wants_jump = jump,
                .wants_sprint = 0
            };
            MoveWish move_wish = shankpit_move_wish_from_intent(move_intent);
            accelerate(p0, move_wish.dir_x, move_wish.dir_z, move_wish.magnitude * MAX_SPEED, ACCEL);
        }
    }
    
    int fresh_jump_press = (jump && !was_holding_jump);
    // --- PHASE 485: TUNED SLIDE JUMP ---
    if (p0->state != STATE_DEAD && jump && p0->on_ground) {
        float speed = sqrtf(p0->vx*p0->vx + p0->vz*p0->vz);
        if (p0->crouching && speed > 0.5f && fresh_jump_press) {
            float boost_mult = 1.0f + (0.25f / speed);
            if (boost_mult > 1.4f) boost_mult = 1.4f;
            if (boost_mult < 1.02f) boost_mult = 1.02f;
            p0->vx *= boost_mult;
            p0->vz *= boost_mult;
        }
        p0->y += 0.1f;
        p0->vy += JUMP_FORCE;
    }
    p0->in_shoot = shoot; p0->in_reload = reload; p0->crouching = crouch;
    p0->in_jump = jump; 
    p0->in_ability = ability;
    was_holding_jump = jump;
    
    for (int hi = 0; hi < MAX_HELICOPTERS; hi++) {
        HelicopterState *h = &local_state.helicopters[hi];
        if (!h->active) continue;
        if (h->occupant_player_id >= 0 && h->occupant_player_id < MAX_CLIENTS) {
            PlayerState *occ = &local_state.players[h->occupant_player_id];
            h->input.forward = occ->in_fwd;
            h->input.yaw = occ->in_strafe;
            h->input.strafe = occ->in_ability ? -1.0f : (occ->in_bike ? 1.0f : 0.0f);
            h->input.ascend = occ->in_jump;
            h->input.descend = occ->crouching;
            heli_simulate_step(h, SHANKPIT_NET_FIXED_DT);
            occ->x = h->x; occ->y = h->y; occ->z = h->z;
            occ->yaw = h->yaw; occ->vx = occ->vy = occ->vz = 0.0f;
            occ->on_ground = h->grounded;
        } else {
            h->input.forward = 0.0f; h->input.yaw = 0.0f; h->input.strafe = 0.0f;
            h->input.ascend = 0; h->input.descend = 0;
            heli_simulate_step(h, SHANKPIT_NET_FIXED_DT);
        }
    }

    for(int i=0; i<MAX_CLIENTS; i++) {
        PlayerState *p = &local_state.players[i];
        if (!p->active) continue;
        if (p->state == STATE_DEAD) {
            if (local_state.game_mode == MODE_STORY && i > 0) {
                p->respawn_time = 0;
                continue;
            }
            if (p->in_vehicle && p->vehicle_type == VEH_BUGGY) {
                for (int bi = 0; bi < MAX_BUGGIES; bi++) {
                    if (local_state.buggies[bi].active && local_state.buggies[bi].occupant_player_id == i) {
                        local_state.buggies[bi].occupant_player_id = -1;
                    }
                }
                p->in_vehicle = 0;
                p->vehicle_type = VEH_NONE;
            }
            if (p->respawn_time != 0 && cmd_time >= p->respawn_time) {
                phys_respawn(p, cmd_time);
                p->respawn_time = 0;
                p->carried_flag_team_id = -1;
                p->in_shoot = 0;
                p->in_reload = 0;
                p->in_use = 0;
                p->in_jump = 0;
                p->in_ability = 0;
                p->is_shooting = 0;
                p->attack_cooldown = 0;
                p->reload_timer = 0;
                p->stunned_until_ms = 0;
                p->stun_immune_until_ms = 0;
#if CTFB_RESPAWN_DEBUG_LOG
                printf("[CTFB] respawn player %d at %u\n", p->id, cmd_time);
#endif
            }
        }
        if (p->in_vehicle && (p->vehicle_type == VEH_HELICOPTER || p->vehicle_type == VEH_BUGGY)) {
            p->use_was_down = p->in_use;
            continue;
        }
        if (i > 0 && p->active && p->state != STATE_DEAD) {
            float b_fwd=0, b_yaw=p->yaw;
            int b_btns=0;
            bot_think(i, local_state.players, &b_fwd, &b_yaw, &b_btns);
            p->yaw = b_yaw;
            if (!p->in_vehicle) {
                float brad = b_yaw * 3.14159f / 180.0f;
                float bx = sinf(brad) * b_fwd;
                float bz = cosf(brad) * b_fwd;
                accelerate(p, bx, bz, MAX_SPEED, ACCEL);
            }
            p->in_shoot = (b_btns & BTN_ATTACK);
            p->in_jump = (b_btns & BTN_JUMP);
            p->in_reload = (b_btns & BTN_RELOAD);
            p->crouching = (b_btns & BTN_CROUCH);
            p->in_use = ((b_btns & BTN_USE) != 0);
            p->in_ability = 0;
            if ((b_btns & BTN_JUMP) && p->on_ground) { p->y += 0.1f; p->vy += JUMP_FORCE; }
        }
        if (local_state.game_mode == MODE_CTFB) {
            ctf_handle_use_interactions(p, cmd_time);
            if (p->in_shoot && p->carried_flag_team_id >= 0) {
                ctf_try_carry_melee(p, cmd_time);
                p->in_shoot = 0;
            }
        }
        phys_set_scene(p->scene_id);
        update_entity(p, 0.016f, server_context, cmd_time);
        if (local_state.game_mode == MODE_CTFB) ctf_try_capture(p, cmd_time);
        p->use_was_down = p->in_use;
    }
    buggy_tick_all();
    update_projectiles(cmd_time);
    if (local_state.game_mode == MODE_STORY && local_state.story_phase == STORY_PHASE_BOSS_PLAYING) {
        if (p0->is_shooting >= 5 || (p0->current_weapon == WPN_KNIFE && p0->in_shoot) || (p0->current_weapon == WPN_KATANA && p0->in_shoot)) {
            story_boss_apply_player_hit(p0, cmd_time);
        }
        story_boss_tick(p0, cmd_time);
    }
    if (local_state.game_mode == MODE_CTFB) {
        ctf_tick_flags(cmd_time);
        ctf_training_on_step(cmd_time);
    }
    if (mode_uses_team_scores(local_state.game_mode)) {
        for (int i = 0; i < MAX_CLIENTS; i++) {
            PlayerState *pp = &local_state.players[i];
            int prev = tdmb_last_kills[i];
            if (pp->kills > prev && team_id_is_valid(pp->team_id)) {
                int delta = pp->kills - prev;
                if (local_state.game_mode != MODE_CTFB) local_state.team_scores[pp->team_id] += delta;
                if (!local_state.match_over && local_state.score_limit > 0 && local_state.team_scores[pp->team_id] >= local_state.score_limit) {
                    local_state.match_over = 1;
                    local_state.winning_team = pp->team_id;
                }
            }
            tdmb_last_kills[i] = pp->kills;
        }
    }
}

void local_init_match(int num_players, int mode) {
    memset(&local_state, 0, sizeof(ServerState));
    memset(tdmb_last_kills, 0, sizeof(tdmb_last_kills));
    local_state.game_mode = mode;
    scene_set_game_mode(mode);
    local_state.pending_scene = -1;
    local_state.transition_timer = 0;
    local_state.winning_team = -1;
    local_state.score_limit = (mode == MODE_TDMB || mode == MODE_TDMO) ? TDMB_SCORE_LIMIT : (mode == MODE_CTFB ? CTFB_SCORE_LIMIT : 0);
    local_state.story_phase = (mode == MODE_STORY) ? STORY_PHASE_INTRO_CUTSCENE : STORY_PHASE_BOSS_PLAYING;
    local_state.story_phase_start_ms = 0;
    story_cutscene_clear_runtime(&local_state.story_cutscene);

    if (mode == MODE_TDMB) {
        num_players = 1 + TDMB_BLUE_BOTS + TDMB_RED_BOTS;
        local_state.scene_id = scene_random_tdmb_map();
        printf("[TDMB] random map selected: %s\n", scene_name_debug(local_state.scene_id));
    } else if (mode == MODE_CTFB) {
        num_players = 1 + TDMB_BLUE_BOTS + TDMB_RED_BOTS;
        local_state.scene_id = SCENE_OIL_TANKER;
    } else if (mode == MODE_STORY) {
        num_players = 1;
        local_state.scene_id = SCENE_VOXWORLD;
        printf("[STORY] starting Voxworld boss encounter\n");
    } else {
        local_state.scene_id = SCENE_GARAGE_OSAKA;
    }

    phys_set_scene(local_state.scene_id);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        local_state.players[i].id = i;
    }

    local_state.players[0].active = 1;
    local_state.players[0].is_bot = 0;
    local_state.players[0].team_id = (mode == MODE_TDMB || mode == MODE_TDMO || mode == MODE_CTFB) ? TDMB_BLUE_TEAM : ((mode == MODE_TDM || mode == MODE_CTF) ? 0 : -1);
    local_state.players[0].scene_id = local_state.scene_id;
    local_state.players[0].carried_flag_team_id = -1;
    phys_respawn(&local_state.players[0], 0);
    local_state.players[0].yaw = 0.0f;
    local_state.players[0].pitch = 0.0f;
    local_state.players[0].current_weapon = WPN_AR;

    for(int i=1; i<num_players; i++) {
        local_state.players[i].active = 1;
        local_state.players[i].is_bot = 1;
        if (mode == MODE_TDMB || mode == MODE_CTFB) {
            local_state.players[i].team_id = (i <= TDMB_BLUE_BOTS) ? TDMB_BLUE_TEAM : TDMB_RED_TEAM;
        } else {
            local_state.players[i].team_id = (mode == MODE_TDM || mode == MODE_CTF || mode == MODE_TDMO) ? (i % 2) : -1;
        }
        local_state.players[i].scene_id = local_state.scene_id;
        local_state.players[i].carried_flag_team_id = -1;
        phys_respawn(&local_state.players[i], i*100);
        init_genome(&local_state.players[i].brain);
    }
    scene_load(local_state.scene_id);
    if (mode == MODE_STORY) {
        StoryBossState *boss = &local_state.story_boss;
        boss->active = 1;
        boss->defeated = 0;
        boss->x = 0.0f;
        boss->z = -420.0f;
        boss->y = voxworld_height_at(boss->x, boss->z) + 42.0f;
        boss->yaw = 180.0f;
        boss->max_health = 1000.0f;
        boss->health = boss->max_health;
        boss->last_attack_ms = 0;
        boss->hurt_flash_until_ms = 0;
        printf("[STORY] boss spawned at %.1f/%.1f/%.1f hp=%.1f\n", boss->x, boss->y, boss->z, boss->max_health);
        story_cutscene_start_intro(0);
    }
    if (mode == MODE_CTFB) {
        ctf_init_match_state(local_state.scene_id);
        ctf_training_on_episode_begin();
    }
}

#endif
