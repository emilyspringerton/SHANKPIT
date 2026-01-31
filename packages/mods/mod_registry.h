#ifndef SHANKPIT_MOD_REGISTRY_H
#define SHANKPIT_MOD_REGISTRY_H

#include <stdio.h>
#include <string.h>
#include "../../include/mod_api.h"

#define MOD_MAX_HANDLERS 128

typedef struct {
    char mod_id[64];
    mod_hook_t hook;
    mod_hook_handler_t handler;
    void *user_ctx;
    int priority;
    unsigned int order;
} ModHookEntry;

typedef struct {
    ModHookEntry entries[MOD_MAX_HANDLERS];
    int count;
    unsigned int next_order;
} ModHookRegistry;

static inline void mod_registry_init(ModHookRegistry *registry) {
    memset(registry, 0, sizeof(ModHookRegistry));
}

static inline void mod_registry_sort(ModHookRegistry *registry) {
    for (int i = 0; i < registry->count; i++) {
        for (int j = i + 1; j < registry->count; j++) {
            ModHookEntry *a = &registry->entries[i];
            ModHookEntry *b = &registry->entries[j];
            if (a->hook != b->hook) continue;
            if (a->priority < b->priority ||
                (a->priority == b->priority && a->order > b->order)) {
                ModHookEntry tmp = *a;
                *a = *b;
                *b = tmp;
            }
        }
    }
}

static inline int mod_registry_register(ModHookRegistry *registry,
                                        const char *mod_id,
                                        mod_hook_t hook,
                                        mod_hook_handler_t handler,
                                        void *user_ctx,
                                        int priority) {
    if (registry->count >= MOD_MAX_HANDLERS) return -1;
    ModHookEntry *entry = &registry->entries[registry->count++];
    memset(entry, 0, sizeof(ModHookEntry));
    strncpy(entry->mod_id, mod_id, sizeof(entry->mod_id) - 1);
    entry->hook = hook;
    entry->handler = handler;
    entry->user_ctx = user_ctx;
    entry->priority = priority;
    entry->order = registry->next_order++;
    mod_registry_sort(registry);
    return 0;
}

static inline void mod_registry_unregister(ModHookRegistry *registry, const char *mod_id) {
    int write = 0;
    for (int i = 0; i < registry->count; i++) {
        if (strcmp(registry->entries[i].mod_id, mod_id) == 0) continue;
        if (write != i) registry->entries[write] = registry->entries[i];
        write++;
    }
    registry->count = write;
}

static inline int mod_registry_dispatch(ModHookRegistry *registry, mod_hook_t hook, void *payload) {
    int handled = 0;
    for (int i = 0; i < registry->count; i++) {
        ModHookEntry *entry = &registry->entries[i];
        if (entry->hook != hook) continue;
        int res = entry->handler(hook, payload, entry->user_ctx);
        if (res < 0) {
            fprintf(stderr, "[mod] handler error for %s\n", entry->mod_id);
        }
        if (res > 0) handled = res;
    }
    return handled;
}

#endif
