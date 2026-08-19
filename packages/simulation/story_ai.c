#include "story_ai.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define STORY_AI_MAX_ATTACKERS 3
#define STORY_AI_HEARING_MEMORY_MS 2200U
#define STORY_AI_SEEN_MEMORY_MS 3200U
#define STORY_AI_SEARCH_MIN_MS 5000U
#define STORY_AI_SEARCH_VAR_MS 3000U


static float ai_angle_diff(float a, float b) {
    float d = a - b;
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

typedef struct {
    int visible;
    int heard;
    float dist;
    float to_player_x;
    float to_player_z;
} AIPerception;

static AIController g_story_ai[STORY_AI_MAX];
static AIWorldBlackboard g_story_bb;
static AIPerception g_story_perception[STORY_AI_MAX];
static unsigned int g_story_debug_next_log_ms = 0;

static float ai_len2(float x, float z) { return sqrtf(x * x + z * z); }
static float ai_angle_to(float dx, float dz) { return atan2f(dx, dz) * (180.0f / 3.14159265f); }

static float ai_clamp(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void ai_set_mode(AIController *ai, AIMode next_mode, unsigned int now_ms) {
    if (ai->mode == next_mode) return;
    ai->previous_mode = ai->mode;
    ai->mode = next_mode;
    ai->mode_entered_ms = now_ms;
}


#if STORY_AI_DEBUG
static const char *ai_mode_name(AIMode mode) {
    switch (mode) {
        case AI_MODE_PATROL: return "PATROL";
        case AI_MODE_INVESTIGATE: return "INVESTIGATE";
        case AI_MODE_COMBAT: return "COMBAT";
        case AI_MODE_SEARCH: return "SEARCH";
        case AI_MODE_ALLY_FOLLOW: return "ALLY";
        default: return "DISABLED";
    }
}

static const char *ai_role_name(AIRole role) {
    switch (role) {
        case AI_ROLE_RIFT_HOUND: return "RIFT_HOUND";
        case AI_ROLE_SHAMBLER_TROOPER: return "SHAMBLER";
        case AI_ROLE_GORE_BRUTE: return "GORE_BRUTE";
        case AI_ROLE_STORY_ALLY: return "ALLY";
        case AI_ROLE_GUARD: return "GUARD";
        case AI_ROLE_STORM_CALLER: return "STORM_CALLER";
        case AI_ROLE_BOMBARDIER: return "BOMBARDIER";
        default: return "UNKNOWN";
    }
}
#endif

static void ai_reset_input(PlayerState *p) {
    p->in_fwd = 0.0f;
    p->in_strafe = 0.0f;
    p->in_shoot = 0;
    p->in_reload = 0;
    p->in_jump = 0;
    p->in_use = 0;
    p->in_ability = 0;
    p->crouching = 0;
}

static void ai_turn_towards(PlayerState *p, float target_yaw, float max_turn_deg) {
    float diff = ai_angle_diff(target_yaw, p->yaw);
    diff = ai_clamp(diff, -max_turn_deg, max_turn_deg);
    p->yaw += diff;
}

static void ai_move_towards(PlayerState *p, float tx, float tz, float speed_scale, float turn_speed) {
    float dx = tx - p->x;
    float dz = tz - p->z;
    float yaw = ai_angle_to(dx, dz);
    ai_turn_towards(p, yaw, turn_speed);
    p->in_fwd = ai_clamp(speed_scale, -1.0f, 1.0f);
}

static void ai_assign_role_defaults(AIController *ai, PlayerState *p) {
    p->current_weapon = WPN_AR;
    ai->vision_range = 180.0f;
    ai->vision_fov_deg = 100.0f;
    ai->hearing_range = 64.0f;
    ai->attack_range = 26.0f;
    ai->preferred_range = 28.0f;
    ai->courage = 0.5f;
    ai->aggression = 0.6f;
    ai->aim_error_deg = 5.0f;
    ai->move_speed_scale = 0.7f;

    switch (ai->role) {
        case AI_ROLE_RIFT_HOUND:
            p->current_weapon = WPN_KATANA;
            ai->vision_range = 165.0f;
            ai->vision_fov_deg = 120.0f;
            ai->hearing_range = 90.0f;
            ai->attack_range = 20.0f;
            ai->preferred_range = 12.0f;
            ai->courage = 0.8f;
            ai->aggression = 0.9f;
            ai->aim_error_deg = 8.0f;
            ai->move_speed_scale = 1.0f;
            break;
        case AI_ROLE_SHAMBLER_TROOPER:
            p->current_weapon = WPN_AR;
            ai->vision_range = 220.0f;
            ai->vision_fov_deg = 95.0f;
            ai->hearing_range = 100.0f;
            ai->attack_range = 72.0f;
            ai->preferred_range = 44.0f;
            ai->courage = 0.55f;
            ai->aggression = 0.65f;
            ai->aim_error_deg = 4.0f;
            ai->move_speed_scale = 0.72f;
            break;
        case AI_ROLE_GORE_BRUTE:
            p->current_weapon = WPN_SHOTGUN;
            ai->vision_range = 165.0f;
            ai->vision_fov_deg = 85.0f;
            ai->hearing_range = 90.0f;
            ai->attack_range = 24.0f;
            ai->preferred_range = 18.0f;
            ai->courage = 1.0f;
            ai->aggression = 0.85f;
            ai->aim_error_deg = 10.0f;
            ai->move_speed_scale = 0.5f;
            break;
        case AI_ROLE_STORY_ALLY:
            p->current_weapon = WPN_AR;
            ai->vision_range = 210.0f;
            ai->vision_fov_deg = 110.0f;
            ai->hearing_range = 120.0f;
            ai->attack_range = 65.0f;
            ai->preferred_range = 42.0f;
            ai->courage = 0.7f;
            ai->aggression = 0.55f;
            ai->aim_error_deg = 6.0f;
            ai->move_speed_scale = 0.78f;
            break;
        case AI_ROLE_GUARD:
            p->current_weapon = WPN_AR;
            ai->vision_range = 200.0f;
            ai->vision_fov_deg = 100.0f;
            ai->hearing_range = 90.0f;
            ai->attack_range = 52.0f;
            ai->preferred_range = 36.0f;
            ai->move_speed_scale = 0.65f;
            break;
        case AI_ROLE_STORM_CALLER:
            /* Long-range sniper threat -- keeps far back, real storm-charge
             * burst ability (100% the existing WPN_SNIPER mechanic, see
             * ai_combat_storm_caller). Lowest courage/aggression of the
             * roster on purpose: a storm caller that closes distance loses
             * its entire identity, so it should actively avoid doing that. */
            p->current_weapon = WPN_SNIPER;
            ai->vision_range = 320.0f;
            ai->vision_fov_deg = 90.0f;
            ai->hearing_range = 70.0f;
            ai->attack_range = 260.0f;
            ai->preferred_range = 200.0f;
            ai->courage = 0.35f;
            ai->aggression = 0.45f;
            ai->aim_error_deg = 2.0f;
            ai->move_speed_scale = 0.55f;
            break;
        case AI_ROLE_BOMBARDIER:
            /* Heavy AOE spammer -- WPN_MISSILE as the BASE weapon (not an
             * ability), so every shot is already a real splash-damage hit
             * via the same spawn_projectile/explode_splash pipeline Frag
             * Toss/Ground Slam reuse. Slow and low-FOV on purpose (a heavy
             * unit that also tracks/moves like a Rift Hound would be
             * unfair, not "challenging" in an interesting way) -- the real
             * threat is area denial, not precision or speed. */
            p->current_weapon = WPN_MISSILE;
            ai->vision_range = 200.0f;
            ai->vision_fov_deg = 80.0f;
            ai->hearing_range = 80.0f;
            ai->attack_range = 140.0f;
            ai->preferred_range = 100.0f;
            ai->courage = 0.9f;
            ai->aggression = 0.5f;
            ai->aim_error_deg = 9.0f;
            ai->move_speed_scale = 0.42f;
            break;
        default:
            break;
    }
}

void story_ai_reset(ServerState *s) {
    int i;
    memset(g_story_ai, 0, sizeof(g_story_ai));
    memset(&g_story_bb, 0, sizeof(g_story_bb));
    memset(g_story_perception, 0, sizeof(g_story_perception));
    g_story_debug_next_log_ms = 0;
    if (!s) return;
    for (i = 1; i < MAX_CLIENTS; i++) {
        s->players[i].active = 0;
        s->players[i].is_bot = 1;
        s->players[i].team_id = -1;
        s->players[i].scene_id = s->scene_id;
        s->players[i].carried_flag_team_id = -1;
        s->players[i].state = STATE_ALIVE;
    }
}

int story_ai_spawn_enemy(ServerState *s, AIRole role, float x, float y, float z) {
    int slot = -1;
    int i;
    AIController *ai = NULL;
    PlayerState *p;
    if (!s) return -1;

    for (i = 1; i < MAX_CLIENTS; i++) {
        if (!s->players[i].active) { slot = i; break; }
    }
    if (slot < 0) return -1;

    for (i = 0; i < STORY_AI_MAX; i++) {
        if (!g_story_ai[i].active) { ai = &g_story_ai[i]; break; }
    }
    if (!ai) return -1;

    p = &s->players[slot];
    memset(ai, 0, sizeof(*ai));
    p->active = 1;
    p->is_bot = 1;
    p->scene_id = s->scene_id;
    p->team_id = -1;
    p->state = STATE_ALIVE;
    p->health = 100;
    p->shield = 0;
    p->x = x;
    p->z = z;
    p->y = y;
    p->vx = p->vy = p->vz = 0.0f;
    p->yaw = 180.0f;
    p->pitch = 0.0f;
    p->carried_flag_team_id = -1;

    ai->active = 1;
    ai->player_id = slot;
    ai->role = role;
    ai->mode = (role == AI_ROLE_STORY_ALLY) ? AI_MODE_ALLY_FOLLOW : AI_MODE_PATROL;
    ai->previous_mode = AI_MODE_DISABLED;
    ai->target_player_id = 0;
    ai->last_known_x = s->players[0].x;
    ai->last_known_y = s->players[0].y;
    ai->last_known_z = s->players[0].z;
    ai->next_decision_ms = 0;
    ai->next_attack_ms = 0;
    ai_assign_role_defaults(ai, p);

    return slot;
}

static void ai_set_patrol(AIController *ai, int idx, float x, float y, float z, unsigned int wait_ms, int hint) {
    if (!ai || idx < 0 || idx >= STORY_AI_PATROL_MAX_POINTS) return;
    ai->patrol[idx].x = x;
    ai->patrol[idx].y = y;
    ai->patrol[idx].z = z;
    ai->patrol[idx].wait_ms = wait_ms;
    ai->patrol[idx].behavior_hint = hint;
    if (ai->patrol_count < idx + 1) ai->patrol_count = idx + 1;
}

static int ai_player_recently_fired(const PlayerState *player) {
    return player->is_shooting > 0 || player->in_shoot;
}

static void ai_gather_perception(ServerState *s, AIController *ai, AIPerception *out, unsigned int now_ms) {
    PlayerState *bot = &s->players[ai->player_id];
    PlayerState *hero = &s->players[0];
    float dx = hero->x - bot->x;
    float dz = hero->z - bot->z;
    float dist = ai_len2(dx, dz);
    float to_yaw = ai_angle_to(dx, dz);
    float fov_half = ai->vision_fov_deg * 0.5f;
    float diff = fabsf(ai_angle_diff(to_yaw, bot->yaw));
    int in_fov = diff <= fov_half;

    memset(out, 0, sizeof(*out));
    out->dist = dist;
    out->to_player_x = dx;
    out->to_player_z = dz;

    if (dist <= ai->vision_range && in_fov && hero->state != STATE_DEAD) {
        out->visible = 1;
        ai->last_seen_ms = now_ms;
        ai->last_known_x = hero->x;
        ai->last_known_y = hero->y;
        ai->last_known_z = hero->z;
    }

    if (!out->visible && hero->state != STATE_DEAD) {
        if (dist <= ai->hearing_range || (dist <= ai->hearing_range * 1.6f && ai_player_recently_fired(hero))) {
            out->heard = 1;
            ai->last_heard_ms = now_ms;
            ai->last_known_x = hero->x;
            ai->last_known_y = hero->y;
            ai->last_known_z = hero->z;
        }
    }
}

static void ai_run_patrol(ServerState *s, AIController *ai, unsigned int now_ms) {
    PlayerState *p = &s->players[ai->player_id];
    AIPatrolPoint *pt;
    float dx, dz, dist;
    if (ai->patrol_count <= 0) {
        p->in_fwd = 0.2f;
        p->in_strafe = 0.2f;
        p->yaw += 0.35f;
        return;
    }

    if (ai->patrol_index >= ai->patrol_count) ai->patrol_index = 0;
    pt = &ai->patrol[ai->patrol_index];
    dx = pt->x - p->x;
    dz = pt->z - p->z;
    dist = ai_len2(dx, dz);

    if (dist < 4.0f) {
        if (ai->wait_until_ms == 0) {
            ai->wait_until_ms = now_ms + pt->wait_ms;
        }
        if (now_ms >= ai->wait_until_ms) {
            ai->wait_until_ms = 0;
            ai->patrol_index = (ai->patrol_index + 1) % ai->patrol_count;
        } else {
            p->in_fwd = 0.0f;
            p->in_strafe = 0.12f;
            p->yaw += 0.55f;
            return;
        }
    }

    ai_move_towards(p, pt->x, pt->z, 0.45f * ai->move_speed_scale, 4.0f);
}

static void ai_run_investigate(ServerState *s, AIController *ai, unsigned int now_ms) {
    PlayerState *p = &s->players[ai->player_id];
    float dx = ai->last_known_x - p->x;
    float dz = ai->last_known_z - p->z;
    float dist = ai_len2(dx, dz);
    (void)now_ms;
    ai_move_towards(p, ai->last_known_x, ai->last_known_z, 0.65f * ai->move_speed_scale, 6.0f);
    if (dist < 7.0f) ai_set_mode(ai, AI_MODE_SEARCH, now_ms);
}

static void ai_run_search(ServerState *s, AIController *ai, unsigned int now_ms) {
    PlayerState *p = &s->players[ai->player_id];
    float t = (float)(now_ms - ai->mode_entered_ms) * 0.001f;
    float orbit_x = ai->last_known_x + cosf(t + (float)ai->player_id) * 10.0f;
    float orbit_z = ai->last_known_z + sinf(t + (float)ai->player_id) * 10.0f;
    ai_move_towards(p, orbit_x, orbit_z, 0.42f * ai->move_speed_scale, 3.0f);
    p->in_strafe = sinf(t * 1.3f) * 0.55f;
}

static void ai_run_ally_follow(ServerState *s, AIController *ai, unsigned int now_ms) {
    PlayerState *p = &s->players[ai->player_id];
    PlayerState *hero = &s->players[0];
    float dx = hero->x - p->x;
    float dz = hero->z - p->z;
    float dist = ai_len2(dx, dz);
    (void)now_ms;
    if (dist > 20.0f) ai_move_towards(p, hero->x, hero->z, 0.70f * ai->move_speed_scale, 7.0f);
    else if (dist < 9.0f) ai_move_towards(p, p->x - dx, p->z - dz, 0.40f * ai->move_speed_scale, 6.0f);
    else p->in_fwd = 0.0f;
}

static void ai_combat_hound(ServerState *s, AIController *ai, PlayerState *p, const AIPerception *per, unsigned int now_ms) {
    PlayerState *hero = &s->players[0];
    float target_yaw = ai_angle_to(per->to_player_x, per->to_player_z);
    ai_turn_towards(p, target_yaw, 10.0f);
    p->in_fwd = 1.0f * ai->move_speed_scale;
    p->in_strafe = ((now_ms / 260U + (unsigned int)ai->player_id) % 2U) ? 0.85f : -0.85f;
    if (per->dist <= ai->attack_range && now_ms >= ai->next_attack_ms) {
        p->in_shoot = 1;
        ai->next_attack_ms = now_ms + 380U;
    }
    if (p->on_ground && ((now_ms / 900U + (unsigned int)ai->player_id) % 3U == 0U) && per->dist < 24.0f) {
        p->in_jump = 1;
    }
    /* S181-05 real ability: gap-closer dash (WPN_KATANA's own existing
     * dash mechanic, physics.h katana_try_start_dash -- triggered here,
     * not invented here). Only worth firing while there's real distance
     * to close; dashing at point-blank range wastes the cooldown for no
     * gain, dashing from very far overshoots past a reasonable engage. */
    if (p->ability_cooldown == 0 && per->dist > 30.0f && per->dist < 70.0f) {
        p->in_ability = 1;
    }
    ai->last_known_x = hero->x;
    ai->last_known_z = hero->z;
}

static void ai_combat_trooper(ServerState *s, AIController *ai, PlayerState *p, const AIPerception *per, unsigned int now_ms) {
    float target_yaw = ai_angle_to(per->to_player_x, per->to_player_z);
    ai_turn_towards(p, target_yaw, 7.0f);
    if (per->dist > ai->preferred_range + 8.0f) p->in_fwd = 0.72f * ai->move_speed_scale;
    else if (per->dist < ai->preferred_range - 12.0f) p->in_fwd = -0.48f * ai->move_speed_scale;
    else p->in_fwd = 0.08f;

    p->in_strafe = ((now_ms / 680U + (unsigned int)ai->player_id) % 2U) ? 0.35f : -0.35f;
    if (per->dist <= ai->attack_range && now_ms >= ai->next_attack_ms) {
        unsigned int burst_gate = (now_ms / 170U) % 5U;
        p->in_shoot = (burst_gate < 3U) ? 1 : 0;
        ai->next_attack_ms = now_ms + 110U;
    }
    /* S181-05 real ability: Frag Toss (physics.h's new WPN_AR ability
     * branch -- real splash-damage projectile, not a stub). Fired while
     * the player is in real engagement range, same window normal shots
     * already use, so it reads as "a periodic special mixed into a
     * kiting gunfight," not a separate disconnected behavior. */
    if (p->ability_cooldown == 0 && per->dist <= ai->attack_range) {
        p->in_ability = 1;
    }
    (void)s;
}

static void ai_combat_brute(ServerState *s, AIController *ai, PlayerState *p, const AIPerception *per, unsigned int now_ms) {
    float target_yaw = ai_angle_to(per->to_player_x, per->to_player_z);
    ai_turn_towards(p, target_yaw, 4.0f);
    p->in_fwd = 0.55f * ai->move_speed_scale;
    p->in_strafe = 0.0f;
    if (per->dist <= ai->attack_range && now_ms >= ai->next_attack_ms) {
        p->in_shoot = 1;
        ai->next_attack_ms = now_ms + 520U;
    }
    /* S181-05 real ability: Ground Slam (physics.h's new WPN_SHOTGUN
     * ability branch -- real splash+stun, not a stub). Only once the
     * Brute has actually closed to melee range -- "gets in your face,
     * then slams" is the whole tank identity; firing it at range would
     * just waste the cooldown on nothing since the splash radius (6.0)
     * is tuned for close quarters. */
    if (p->ability_cooldown == 0 && per->dist < 18.0f) {
        p->in_ability = 1;
    }
    (void)s;
}

static void ai_combat_storm_caller(ServerState *s, AIController *ai, PlayerState *p, const AIPerception *per, unsigned int now_ms) {
    float target_yaw = ai_angle_to(per->to_player_x, per->to_player_z);
    ai_turn_towards(p, target_yaw, 5.0f);
    /* Actively maintains distance -- backs away if the player closes
     * inside preferred_range, unlike Trooper which just stops advancing.
     * A storm caller that lets itself get out-ranged into melee has lost
     * its entire reason to exist as a distinct threat. */
    if (per->dist > ai->preferred_range + 20.0f) p->in_fwd = 0.5f * ai->move_speed_scale;
    else if (per->dist < ai->preferred_range - 15.0f) p->in_fwd = -0.6f * ai->move_speed_scale;
    else p->in_fwd = 0.0f;
    p->in_strafe = ((now_ms / 900U + (unsigned int)ai->player_id) % 2U) ? 0.3f : -0.3f;

    if (per->dist <= ai->attack_range && now_ms >= ai->next_attack_ms) {
        p->in_shoot = 1;
        ai->next_attack_ms = now_ms + 260U;
    }
    /* Real ability: the 5-round storm-charge burst (physics.h's existing
     * WPN_SNIPER branch, unmodified -- this role is the first AI to
     * actually use it as a deliberate tactical choice instead of it being
     * silently unreachable). Only pops it once real engagement range is
     * confirmed -- popping it while the player is still out of vision
     * would burn the cooldown on nothing. */
    if (p->ability_cooldown == 0 && per->dist <= ai->attack_range) {
        p->in_ability = 1;
    }
    (void)s;
}

static void ai_combat_bombardier(ServerState *s, AIController *ai, PlayerState *p, const AIPerception *per, unsigned int now_ms) {
    float target_yaw = ai_angle_to(per->to_player_x, per->to_player_z);
    ai_turn_towards(p, target_yaw, 4.0f);
    if (per->dist > ai->preferred_range + 15.0f) p->in_fwd = 0.5f * ai->move_speed_scale;
    else if (per->dist < ai->preferred_range - 20.0f) p->in_fwd = -0.35f * ai->move_speed_scale;
    else p->in_fwd = 0.0f;
    /* No dedicated in_ability trigger -- WPN_MISSILE is this role's BASE
     * weapon, so every normal shot is already a real splash-damage hit
     * via the same spawn_projectile/explode_splash pipeline Frag Toss and
     * Ground Slam borrow for their own abilities. The threat here is
     * sustained area denial from a slow-moving heavy, not a special move. */
    if (per->dist <= ai->attack_range && now_ms >= ai->next_attack_ms) {
        p->in_shoot = 1;
        ai->next_attack_ms = now_ms + 950U;
    }
    (void)s;
}

static void ai_combat_ally(ServerState *s, AIController *ai, PlayerState *p, const AIPerception *per, unsigned int now_ms) {
    PlayerState *hero = &s->players[0];
    float target_yaw = ai_angle_to(per->to_player_x, per->to_player_z);
    ai_turn_towards(p, target_yaw, 6.0f);
    if (per->dist > ai->preferred_range) p->in_fwd = 0.4f;
    else if (per->dist < 15.0f) p->in_fwd = -0.25f;
    if (per->dist <= ai->attack_range && now_ms >= ai->next_attack_ms) {
        p->in_shoot = ((now_ms / 190U + (unsigned int)ai->player_id) % 4U) < 2U;
        ai->next_attack_ms = now_ms + 180U;
    }
    if (ai_len2(hero->x - p->x, hero->z - p->z) < 7.0f) p->in_strafe = 0.6f;
}

static void ai_run_combat(ServerState *s, AIController *ai, const AIPerception *per, unsigned int now_ms) {
    PlayerState *p = &s->players[ai->player_id];
    if (ai->role == AI_ROLE_RIFT_HOUND) ai_combat_hound(s, ai, p, per, now_ms);
    else if (ai->role == AI_ROLE_GORE_BRUTE) ai_combat_brute(s, ai, p, per, now_ms);
    else if (ai->role == AI_ROLE_STORY_ALLY) ai_combat_ally(s, ai, p, per, now_ms);
    else if (ai->role == AI_ROLE_STORM_CALLER) ai_combat_storm_caller(s, ai, p, per, now_ms);
    else if (ai->role == AI_ROLE_BOMBARDIER) ai_combat_bombardier(s, ai, p, per, now_ms);
    else ai_combat_trooper(s, ai, p, per, now_ms);
}

void story_ai_tick(ServerState *s, unsigned int now_ms) {
    int i;
    int attackers = 0;
    if (!s) return;
    if (s->game_mode != MODE_STORY || s->story_phase != STORY_PHASE_PLAYING) return;

    memset(&g_story_bb, 0, sizeof(g_story_bb));

    for (i = 0; i < STORY_AI_MAX; i++) {
        AIController *ai = &g_story_ai[i];
        if (!ai->active) continue;
        if (ai->player_id <= 0 || ai->player_id >= MAX_CLIENTS) continue;
        if (!s->players[ai->player_id].active || s->players[ai->player_id].state == STATE_DEAD) continue;
        ai_gather_perception(s, ai, &g_story_perception[i], now_ms);
        if (g_story_perception[i].visible) {
            g_story_bb.player_visible_count++;
            g_story_bb.last_known_player_x = ai->last_known_x;
            g_story_bb.last_known_player_y = ai->last_known_y;
            g_story_bb.last_known_player_z = ai->last_known_z;
            g_story_bb.last_global_alert_ms = now_ms;
        }
    }

    g_story_bb.alert_level = (g_story_bb.player_visible_count > 0) ? 2 : ((now_ms - g_story_bb.last_global_alert_ms) < 2500U ? 1 : 0);

    for (i = 0; i < STORY_AI_MAX; i++) {
        AIController *ai = &g_story_ai[i];
        AIPerception *per = &g_story_perception[i];
        unsigned int search_timeout;
        int wants_combat;
        if (!ai->active) continue;
        if (!s->players[ai->player_id].active || s->players[ai->player_id].state == STATE_DEAD) continue;

        wants_combat = per->visible;
        search_timeout = STORY_AI_SEARCH_MIN_MS + (((unsigned int)ai->player_id * 317U) % STORY_AI_SEARCH_VAR_MS);

        if (ai->role == AI_ROLE_STORY_ALLY) {
            if (wants_combat) ai_set_mode(ai, AI_MODE_COMBAT, now_ms);
            else ai_set_mode(ai, AI_MODE_ALLY_FOLLOW, now_ms);
            continue;
        }

        if (wants_combat && attackers < STORY_AI_MAX_ATTACKERS) {
            ai_set_mode(ai, AI_MODE_COMBAT, now_ms);
            attackers++;
            continue;
        }

        if (wants_combat && attackers >= STORY_AI_MAX_ATTACKERS) {
            ai_set_mode(ai, AI_MODE_SEARCH, now_ms);
            continue;
        }

        if ((now_ms - ai->last_heard_ms) < STORY_AI_HEARING_MEMORY_MS) {
            ai_set_mode(ai, AI_MODE_INVESTIGATE, now_ms);
            continue;
        }

        if ((now_ms - ai->last_seen_ms) < STORY_AI_SEEN_MEMORY_MS) {
            ai_set_mode(ai, AI_MODE_SEARCH, now_ms);
            continue;
        }

        if (ai->mode == AI_MODE_SEARCH && (now_ms - ai->mode_entered_ms) < search_timeout) {
            continue;
        }

        ai_set_mode(ai, AI_MODE_PATROL, now_ms);
    }

    g_story_bb.active_attackers = attackers;

    for (i = 0; i < STORY_AI_MAX; i++) {
        AIController *ai = &g_story_ai[i];
        PlayerState *p;
        if (!ai->active) continue;
        if (!s->players[ai->player_id].active || s->players[ai->player_id].state == STATE_DEAD) continue;
        p = &s->players[ai->player_id];
        ai_reset_input(p);
        if (ai->mode == AI_MODE_PATROL) ai_run_patrol(s, ai, now_ms);
        else if (ai->mode == AI_MODE_INVESTIGATE) ai_run_investigate(s, ai, now_ms);
        else if (ai->mode == AI_MODE_SEARCH) ai_run_search(s, ai, now_ms);
        else if (ai->mode == AI_MODE_ALLY_FOLLOW) ai_run_ally_follow(s, ai, now_ms);
        else if (ai->mode == AI_MODE_COMBAT) ai_run_combat(s, ai, &g_story_perception[i], now_ms);
    }

#if STORY_AI_DEBUG
    if (now_ms >= g_story_debug_next_log_ms) {
        g_story_debug_next_log_ms = now_ms + 1000U;
        for (i = 0; i < STORY_AI_MAX; i++) {
            AIController *ai = &g_story_ai[i];
            if (!ai->active) continue;
            printf("[STORY_AI] id=%d role=%s mode=%s dist=%.1f visible=%d attackers=%d alert=%d\n",
                   ai->player_id,
                   ai_role_name(ai->role),
                   ai_mode_name(ai->mode),
                   g_story_perception[i].dist,
                   g_story_perception[i].visible,
                   g_story_bb.active_attackers,
                   g_story_bb.alert_level);
        }
    }
#endif
}

void story_ai_seed_voxworld_encounter(ServerState *s) {
    int id_a, id_b, id_h, id_g, id_sc, id_bm;
    float cx = 0.0f;
    float cz = -260.0f;
    if (!s || s->scene_id != SCENE_VOXWORLD) return;

    id_a = story_ai_spawn_enemy(s, AI_ROLE_SHAMBLER_TROOPER, cx - 42.0f, 8.0f, cz - 20.0f);
    id_b = story_ai_spawn_enemy(s, AI_ROLE_SHAMBLER_TROOPER, cx + 38.0f, 8.0f, cz + 16.0f);
    id_h = story_ai_spawn_enemy(s, AI_ROLE_RIFT_HOUND, cx + 4.0f, 8.0f, cz - 54.0f);
    id_g = story_ai_spawn_enemy(s, AI_ROLE_GORE_BRUTE, cx - 4.0f, 8.0f, cz + 46.0f);
    /* S181-05: "many enemies with different threats" -- Storm Caller
     * spawns furthest back (long-range role needs real distance to be
     * itself), Bombardier spawns at a flank (heavy area-denial role,
     * doesn't need to lead the engagement). */
    id_sc = story_ai_spawn_enemy(s, AI_ROLE_STORM_CALLER, cx + 60.0f, 8.0f, cz - 90.0f);
    id_bm = story_ai_spawn_enemy(s, AI_ROLE_BOMBARDIER, cx - 60.0f, 8.0f, cz - 10.0f);

    if (id_a > 0) {
        AIController *ai = NULL;
        for (int i = 0; i < STORY_AI_MAX; i++) if (g_story_ai[i].active && g_story_ai[i].player_id == id_a) { ai = &g_story_ai[i]; break; }
        if (ai) {
            ai_set_patrol(ai, 0, cx - 60.0f, 0.0f, cz - 40.0f, 550U, 0);
            ai_set_patrol(ai, 1, cx - 28.0f, 0.0f, cz - 8.0f, 400U, 0);
            ai_set_patrol(ai, 2, cx - 54.0f, 0.0f, cz + 20.0f, 600U, 0);
        }
    }
    if (id_b > 0) {
        AIController *ai = NULL;
        for (int i = 0; i < STORY_AI_MAX; i++) if (g_story_ai[i].active && g_story_ai[i].player_id == id_b) { ai = &g_story_ai[i]; break; }
        if (ai) {
            ai_set_patrol(ai, 0, cx + 20.0f, 0.0f, cz + 34.0f, 450U, 0);
            ai_set_patrol(ai, 1, cx + 56.0f, 0.0f, cz + 2.0f, 450U, 0);
            ai_set_patrol(ai, 2, cx + 26.0f, 0.0f, cz - 28.0f, 650U, 0);
        }
    }
    if (id_h > 0) {
        AIController *ai = NULL;
        for (int i = 0; i < STORY_AI_MAX; i++) if (g_story_ai[i].active && g_story_ai[i].player_id == id_h) { ai = &g_story_ai[i]; break; }
        if (ai) {
            ai_set_patrol(ai, 0, cx - 8.0f, 0.0f, cz - 80.0f, 250U, 0);
            ai_set_patrol(ai, 1, cx + 48.0f, 0.0f, cz - 42.0f, 300U, 0);
            ai_set_patrol(ai, 2, cx + 2.0f, 0.0f, cz + 8.0f, 350U, 0);
            ai_set_patrol(ai, 3, cx - 52.0f, 0.0f, cz - 30.0f, 250U, 0);
        }
    }
    if (id_g > 0) {
        AIController *ai = NULL;
        for (int i = 0; i < STORY_AI_MAX; i++) if (g_story_ai[i].active && g_story_ai[i].player_id == id_g) { ai = &g_story_ai[i]; break; }
        if (ai) {
            ai_set_mode(ai, AI_MODE_PATROL, 0);
            ai_set_patrol(ai, 0, cx - 5.0f, 0.0f, cz + 54.0f, 900U, 0);
            ai_set_patrol(ai, 1, cx + 9.0f, 0.0f, cz + 38.0f, 900U, 0);
        }
    }

    if (id_sc > 0) {
        AIController *ai = NULL;
        for (int i = 0; i < STORY_AI_MAX; i++) if (g_story_ai[i].active && g_story_ai[i].player_id == id_sc) { ai = &g_story_ai[i]; break; }
        if (ai) {
            /* Short patrol, deliberately small radius -- a storm caller
             * holding one general area (not chasing across the whole map)
             * matches its own "keeps distance, doesn't close" combat
             * behavior above. */
            ai_set_patrol(ai, 0, cx + 60.0f, 0.0f, cz - 90.0f, 800U, 0);
            ai_set_patrol(ai, 1, cx + 46.0f, 0.0f, cz - 104.0f, 800U, 0);
        }
    }
    if (id_bm > 0) {
        AIController *ai = NULL;
        for (int i = 0; i < STORY_AI_MAX; i++) if (g_story_ai[i].active && g_story_ai[i].player_id == id_bm) { ai = &g_story_ai[i]; break; }
        if (ai) {
            ai_set_patrol(ai, 0, cx - 60.0f, 0.0f, cz - 10.0f, 900U, 0);
            ai_set_patrol(ai, 1, cx - 44.0f, 0.0f, cz + 6.0f, 900U, 0);
        }
    }

#if STORY_AI_DEBUG
    printf("[STORY_AI] encounter spawned ids: trooper=%d trooper=%d hound=%d brute=%d storm_caller=%d bombardier=%d\n",
           id_a, id_b, id_h, id_g, id_sc, id_bm);
#endif
}
