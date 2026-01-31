#ifndef SHANKPIT_MOD_API_H
#define SHANKPIT_MOD_API_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MOD_HOOK_STARTUP,
    MOD_HOOK_SHUTDOWN,
    MOD_HOOK_FRAME,
    MOD_HOOK_INPUT_EVENT,
    MOD_HOOK_ENTITY_SPAWN,
    MOD_HOOK_ENTITY_DESTROY,
    MOD_HOOK_ENTITY_DAMAGE,
    MOD_HOOK_RENDER,
    MOD_HOOK_ASSET_LOAD,
    MOD_HOOK_CONSOLE_COMMAND,
    MOD_HOOK_SAVE_GAME,
    MOD_HOOK_LOAD_GAME,
    MOD_HOOK_CUSTOM,
    MOD_HOOK_ROUND_START,
    MOD_HOOK_ROUND_END,
    MOD_HOOK_PLAYER_JOIN,
    MOD_HOOK_PLAYER_LEAVE,
    MOD_HOOK_PLAYER_KILL,
    MOD_HOOK_PLAYER_RESPAWN,
    MOD_HOOK_WEAPON_ASSIGN,
    MOD_HOOK_SCORE_UPDATE,
    MOD_HOOK_SCOREBOARD_QUERY,
    MOD_HOOK_GAMESTATE_SAVE,
    MOD_HOOK_GAMESTATE_LOAD
} mod_hook_t;

typedef struct {
    uint64_t tick;
    float dt;
} mod_frame_t;

typedef struct {
    int device;
    int code;
    int action;
    void *raw;
} mod_input_event_t;

typedef struct {
    uint32_t entity_id;
    const char *type;
    void *user_data;
} mod_entity_spawn_t;

typedef struct {
    uint32_t entity_id;
    float amount;
    uint32_t source_id;
} mod_entity_damage_t;

typedef struct {
    uint64_t round_id;
    int round_time_seconds;
    const char *mode_name;
    void *server_state;
} mod_round_start_t;

typedef struct {
    uint64_t round_id;
    const char *result;
    void *server_state;
} mod_round_end_t;

typedef struct {
    uint32_t player_id;
    const char *player_name;
    void *connection;
} mod_player_join_t;

typedef struct {
    uint32_t player_id;
    const char *player_name;
    const char *reason;
    void *connection;
} mod_player_leave_t;

typedef struct {
    uint32_t attacker_id;
    uint32_t victim_id;
    uint32_t weapon_id;
    int headshot;
    float damage;
    uint64_t tick;
    void *game_state;
    int handled;
} mod_player_kill_t;

typedef struct {
    uint32_t player_id;
    int respawn_point_id;
    void *game_state;
} mod_player_respawn_t;

typedef struct {
    uint32_t player_id;
    uint32_t weapon_id;
    const void *weapon_data;
    size_t weapon_data_size;
    int handled;
} mod_weapon_assign_t;

typedef struct {
    uint32_t player_id;
    int score_delta;
    int total_score;
    const char *reason;
} mod_score_update_t;

typedef struct {
    const char *path;
    const char *type;
    const void **out_data;
    size_t *out_size;
    int handled;
} mod_asset_load_t;

typedef struct {
    const char *command;
    int argc;
    const char **argv;
    int handled;
} mod_console_command_t;

typedef int (*mod_hook_handler_t)(mod_hook_t hook, void *payload, void *user_ctx);

typedef struct mod_gamemode_t {
    const char *id;
    const char *name;
    const char *description;
    const char *version;
    const char *default_map;
    int min_players;
    int max_players;
    int team_count;
    int score_limit;
    int time_limit_seconds;
    int uses_teams;
    int (*on_register)(void *userdata);
    int (*on_unregister)(void *userdata);
    int (*on_start_round)(mod_round_start_t *payload);
    int (*on_end_round)(mod_round_end_t *payload);
    int (*on_player_join)(mod_player_join_t *payload);
    int (*on_player_leave)(mod_player_leave_t *payload);
    int (*on_player_kill)(mod_player_kill_t *payload);
    int (*on_score_update)(mod_score_update_t *payload);
    void *userdata;
} mod_gamemode_t;

typedef struct mod_api_t {
    int (*register_hook)(const char *mod_id, mod_hook_t hook, mod_hook_handler_t handler, void *user_ctx, int priority);
    void (*unregister_mod)(const char *mod_id);
    void (*log)(const char *mod_id, int level, const char *fmt, ...);
    int (*register_console_cmd)(const char *mod_id, const char *cmd_name, mod_hook_handler_t handler, void *user_ctx);
    const char *(*mod_config_path)(const char *mod_id);
    const void *(*get_asset)(const char *path, size_t *out_size);
    int (*publish_event)(const char *event_name, void *payload);
    int (*register_gamemode)(const char *mod_id, const mod_gamemode_t *gm);
    int (*unregister_gamemode)(const char *mod_id, const char *gm_id);
    int (*net_broadcast_event)(const char *event_name, const void *payload, size_t payload_size);
    int (*net_send_to_client)(uint32_t player_id, const char *event_name, const void *payload, size_t payload_size);
    int (*atomic_update_score)(uint32_t player_id, int delta, int *out_total);
    int (*is_server)(void);
} mod_api_t;

#ifdef __cplusplus
}
#endif

#endif
