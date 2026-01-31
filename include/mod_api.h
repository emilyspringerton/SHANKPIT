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
    MOD_HOOK_CUSTOM
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

typedef struct mod_api_t {
    int (*register_hook)(const char *mod_id, mod_hook_t hook, mod_hook_handler_t handler, void *user_ctx, int priority);
    void (*unregister_mod)(const char *mod_id);
    void (*log)(const char *mod_id, int level, const char *fmt, ...);
    int (*register_console_cmd)(const char *mod_id, const char *cmd_name, mod_hook_handler_t handler, void *user_ctx);
    const char *(*mod_config_path)(const char *mod_id);
    const void *(*get_asset)(const char *path, size_t *out_size);
    int (*publish_event)(const char *event_name, void *payload);
} mod_api_t;

#ifdef __cplusplus
}
#endif

#endif
