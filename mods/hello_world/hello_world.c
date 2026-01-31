#include "../../include/mod_api.h"

static const mod_api_t *api_ref = NULL;
static const char *mod_id_ref = "hello_world";

static int mod_hw_cmd(mod_hook_t hook, void *payload, void *user_ctx) {
    (void)hook;
    (void)user_ctx;
    mod_console_command_t *cmd = (mod_console_command_t *)payload;
    if (!cmd || cmd->handled) return 0;
    api_ref->log(mod_id_ref, 1, "hw_hello invoked with %d args", cmd->argc);
    return 1;
}

int mod_init(const mod_api_t *api, const char *mod_id) {
    api_ref = api;
    mod_id_ref = mod_id;
    api_ref->log(mod_id_ref, 1, "Hello from hello_world");
    api_ref->register_console_cmd(mod_id_ref, "hw_hello", mod_hw_cmd, NULL);
    return 0;
}

void mod_shutdown(void) {
}
