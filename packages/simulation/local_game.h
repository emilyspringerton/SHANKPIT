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
#define STORY_BEAR_COUNT 7
#define STORY_END_TRIGGER_Z 1210.0f
#define STORY_END_TRIGGER_RADIUS 72.0f
#define STORY_CUTSCENE_DURATION_MS 5000

typedef enum {
    STORY_BEAR_IDLE = 0,
    STORY_BEAR_ALERTED = 1,
    STORY_BEAR_CHASING = 2,
    STORY_BEAR_ATTACKING = 3,
    STORY_BEAR_DEAD = 4
} StoryBearState;

static const Vec2 story_bear_spawn_points[STORY_BEAR_COUNT] = {
    { 0.0f, -760.0f },
    {-38.0f, -340.0f },
    { 42.0f, -280.0f },
    { 60.0f, 130.0f },
    {-36.0f, 420.0f },
    { 28.0f, 780.0f },
    {-42.0f, 910.0f }
};
static StoryBearState story_bear_states[MAX_CLIENTS];

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
            throttle = occ->in_fwd;
            float yaw_err = norm_yaw_deg(occ->yaw - b->yaw);
            if (yaw_err > 180.0f) yaw_err -= 360.0f;
            if (yaw_err < -180.0f) yaw_err += 360.0f;
            steer_intent = yaw_err / 75.0f;
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
            occ->yaw = b->yaw;
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

static void story_update_bear_state(PlayerState *bear, PlayerState *target, float dist) {
    StoryBearState state = story_bear_states[bear->id];
    if (bear->state == STATE_DEAD) {
        story_bear_states[bear->id] = STORY_BEAR_DEAD;
        return;
    }
    switch (state) {
        case STORY_BEAR_IDLE:
            if (dist < 210.0f) story_bear_states[bear->id] = STORY_BEAR_ALERTED;
            break;
        case STORY_BEAR_ALERTED:
            story_bear_states[bear->id] = (dist < 170.0f) ? STORY_BEAR_CHASING : STORY_BEAR_IDLE;
            break;
        case STORY_BEAR_CHASING:
            if (dist < 9.0f) story_bear_states[bear->id] = STORY_BEAR_ATTACKING;
            else if (dist > 280.0f) story_bear_states[bear->id] = STORY_BEAR_IDLE;
            break;
        case STORY_BEAR_ATTACKING:
            if (dist > 12.0f) story_bear_states[bear->id] = STORY_BEAR_CHASING;
            break;
        case STORY_BEAR_DEAD:
            break;
    }
    (void)target;
}

// --- BOT AI ---
void bot_think(int bot_idx, PlayerState *players, float *out_fwd, float *out_yaw, int *out_buttons) {
    PlayerState *me = &players[bot_idx];
    if (me->state == STATE_DEAD || local_state.match_over) { *out_buttons = 0; return; }
    if (local_state.game_mode == MODE_STORY) {
        PlayerState *hero = &players[0];
        float dx = hero->x - me->x;
        float dz = hero->z - me->z;
        float dist = sqrtf(dx*dx + dz*dz);
        float target_yaw = atan2f(dx, dz) * (180.0f / 3.14159f);
        float diff = angle_diff(target_yaw, me->yaw);
        if (diff > 7.0f) diff = 7.0f;
        if (diff < -7.0f) diff = -7.0f;
        *out_yaw = me->yaw + diff;
        story_update_bear_state(me, hero, dist);
        StoryBearState state = story_bear_states[bot_idx];
        if (state == STORY_BEAR_ALERTED || state == STORY_BEAR_CHASING) *out_fwd = 0.9f;
        if (state == STORY_BEAR_ATTACKING) {
            me->current_weapon = WPN_KNIFE;
            *out_buttons |= BTN_ATTACK;
        }
        return;
    }
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
    int story_cutscene = (local_state.game_mode == MODE_STORY && local_state.story_phase == STORY_PHASE_CUTSCENE);
    if (local_state.game_mode == MODE_STORY) {
        if (local_state.story_phase == STORY_PHASE_CUTSCENE) {
            yaw = p0->yaw;
            pitch = p0->pitch;
            fwd = 0.0f; str = 0.0f; shoot = 0; jump = 0; crouch = 0; reload = 0; ability = 0;
            if (cmd_time - local_state.story_phase_start_ms >= STORY_CUTSCENE_DURATION_MS) {
                local_state.story_phase = STORY_PHASE_PLAYING;
                local_state.story_phase_start_ms = cmd_time;
                story_cutscene = 0;
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
    if (!story_cutscene) {
        p0->yaw = yaw; p0->pitch = pitch;
    }
    p0->in_fwd = fwd;
    p0->in_strafe = str;
    if (!story_cutscene && weapon_req >= 0 && weapon_req < MAX_WEAPONS) p0->current_weapon = weapon_req;
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
    if (local_state.game_mode == MODE_STORY && local_state.story_phase == STORY_PHASE_PLAYING) {
        float ex = p0->x - 0.0f;
        float ez = p0->z - STORY_END_TRIGGER_Z;
        if (ex * ex + ez * ez <= STORY_END_TRIGGER_RADIUS * STORY_END_TRIGGER_RADIUS) {
            local_state.story_phase = STORY_PHASE_COMPLETE;
            local_state.story_phase_start_ms = cmd_time;
            local_state.match_over = 1;
        }
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
    local_state.story_phase = (mode == MODE_STORY) ? STORY_PHASE_CUTSCENE : STORY_PHASE_PLAYING;
    local_state.story_phase_start_ms = 0;
    memset(story_bear_states, 0, sizeof(story_bear_states));

    if (mode == MODE_TDMB) {
        num_players = 1 + TDMB_BLUE_BOTS + TDMB_RED_BOTS;
        local_state.scene_id = scene_random_tdmb_map();
        printf("[TDMB] random map selected: %s\n", scene_name_debug(local_state.scene_id));
    } else if (mode == MODE_CTFB) {
        num_players = 1 + TDMB_BLUE_BOTS + TDMB_RED_BOTS;
        local_state.scene_id = SCENE_OIL_TANKER;
    } else if (mode == MODE_STORY) {
        num_players = 1 + STORY_BEAR_COUNT;
        local_state.scene_id = SCENE_STORY_CAVE;
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
        if (mode == MODE_STORY) {
            int bear_slot = i - 1;
            if (bear_slot >= 0 && bear_slot < STORY_BEAR_COUNT) {
                local_state.players[i].x = story_bear_spawn_points[bear_slot].x;
                local_state.players[i].z = story_bear_spawn_points[bear_slot].y;
                local_state.players[i].y = 6.0f;
                local_state.players[i].yaw = 180.0f;
                local_state.players[i].current_weapon = WPN_KNIFE;
                local_state.players[i].health = (bear_slot >= STORY_BEAR_COUNT - 2) ? 100 : 80;
                local_state.players[i].shield = 0;
                story_bear_states[i] = STORY_BEAR_IDLE;
            }
        }
    }
    scene_load(local_state.scene_id);
    if (mode == MODE_CTFB) {
        ctf_init_match_state(local_state.scene_id);
        ctf_training_on_episode_begin();
    }
}

#endif
