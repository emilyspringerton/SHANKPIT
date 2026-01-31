#include "../../include/mod_api.h"

static const mod_api_t *api_ref = NULL;
static const char *mod_id_ref = "health_tweak";

static int on_spawn(mod_hook_t hook, void *payload, void *user_ctx) {
    (void)hook;
    (void)user_ctx;
    mod_entity_spawn_t *spawn = (mod_entity_spawn_t *)payload;
    if (!spawn || !spawn->user_data) return 0;
    api_ref->log(mod_id_ref, 1, "spawned entity %u (%s)", spawn->entity_id, spawn->type);
    return 0;
}

int mod_init(const mod_api_t *api, const char *mod_id) {
    api_ref = api;
    mod_id_ref = mod_id;
    api_ref->register_hook(mod_id_ref, MOD_HOOK_ENTITY_SPAWN, on_spawn, NULL, 50);
    return 0;
}

void mod_shutdown(void) {
}
