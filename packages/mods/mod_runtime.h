#ifndef SHANKPIT_MOD_RUNTIME_H
#define SHANKPIT_MOD_RUNTIME_H

#include "../../include/mod_api.h"
#include "mod_registry.h"

static ModHookRegistry *mod_runtime_registry = NULL;

static inline void mod_runtime_set_registry(ModHookRegistry *registry) {
    mod_runtime_registry = registry;
}

static inline int mod_runtime_dispatch(mod_hook_t hook, void *payload) {
    if (!mod_runtime_registry) return 0;
    return mod_registry_dispatch(mod_runtime_registry, hook, payload);
}

#endif
