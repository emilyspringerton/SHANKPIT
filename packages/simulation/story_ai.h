#ifndef STORY_AI_H
#define STORY_AI_H

#include "../common/protocol.h"

#define STORY_AI_MAX 16
#define STORY_AI_PATROL_MAX_POINTS 8

#ifndef STORY_AI_DEBUG
#define STORY_AI_DEBUG 0
#endif

typedef enum {
    AI_MODE_DISABLED = 0,
    AI_MODE_PUPPET,
    AI_MODE_PATROL,
    AI_MODE_INVESTIGATE,
    AI_MODE_COMBAT,
    AI_MODE_SEARCH,
    AI_MODE_FLEE,
    AI_MODE_ALLY_FOLLOW,
    AI_MODE_SCRIPTED
} AIMode;

typedef enum {
    AI_ROLE_RIFT_HOUND = 0,
    AI_ROLE_SHAMBLER_TROOPER,
    AI_ROLE_GORE_BRUTE,
    AI_ROLE_STORY_ALLY,
    AI_ROLE_GUARD
} AIRole;

typedef struct {
    float x, y, z;
    unsigned int wait_ms;
    int behavior_hint;
} AIPatrolPoint;

typedef struct {
    int active;
    int player_id;
    AIRole role;
    AIMode mode;
    AIMode previous_mode;

    AIPatrolPoint patrol[STORY_AI_PATROL_MAX_POINTS];
    int patrol_count;
    int patrol_index;
    unsigned int wait_until_ms;

    int target_player_id;
    float last_known_x, last_known_y, last_known_z;
    unsigned int last_seen_ms;
    unsigned int last_heard_ms;
    unsigned int mode_entered_ms;

    float vision_range;
    float vision_fov_deg;
    float hearing_range;
    float attack_range;
    float preferred_range;
    float courage;
    float aggression;
    float aim_error_deg;
    float move_speed_scale;

    unsigned int next_attack_ms;
    unsigned int next_decision_ms;
} AIController;

typedef struct {
    int alert_level;
    int player_visible_count;
    int active_attackers;
    int active_flankers;
    float last_known_player_x;
    float last_known_player_y;
    float last_known_player_z;
    unsigned int last_global_alert_ms;
} AIWorldBlackboard;

void story_ai_reset(ServerState *s);
int story_ai_spawn_enemy(ServerState *s, AIRole role, float x, float y, float z);
void story_ai_tick(ServerState *s, unsigned int now_ms);
void story_ai_seed_voxworld_encounter(ServerState *s);

#endif
