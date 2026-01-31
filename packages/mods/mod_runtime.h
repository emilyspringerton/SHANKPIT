#ifndef SHANKPIT_MOD_RUNTIME_H
#define SHANKPIT_MOD_RUNTIME_H

#include "../../include/mod_api.h"
#include "mod_gamemode.h"
#include "mod_registry.h"

static ModHookRegistry *mod_runtime_registry = NULL;
static ModGameModeRegistry *mod_runtime_gamemode_registry = NULL;

static inline void mod_runtime_set_registry(ModHookRegistry *registry) {
    mod_runtime_registry = registry;
}

static inline void mod_runtime_set_gamemode_registry(ModGameModeRegistry *registry) {
    mod_runtime_gamemode_registry = registry;
}

static inline int mod_runtime_dispatch(mod_hook_t hook, void *payload) {
    if (!mod_runtime_registry) return 0;
    return mod_registry_dispatch(mod_runtime_registry, hook, payload);
}

static inline int mod_runtime_register_gamemode(const char *mod_id,
                                                const mod_gamemode_t *gm,
                                                int priority) {
    if (!mod_runtime_gamemode_registry) return -1;
    return mod_gamemode_register(mod_runtime_gamemode_registry, mod_id, gm, priority);
}

static inline int mod_runtime_unregister_gamemode(const char *mod_id, const char *gm_id) {
    if (!mod_runtime_gamemode_registry) return -1;
    return mod_gamemode_unregister(mod_runtime_gamemode_registry, mod_id, gm_id);
}

static inline const mod_gamemode_t *mod_runtime_find_gamemode(const char *gm_id,
                                                              const char **out_mod_id) {
    if (!mod_runtime_gamemode_registry) return NULL;
    return mod_gamemode_find(mod_runtime_gamemode_registry, gm_id, out_mod_id);
}

#endif
