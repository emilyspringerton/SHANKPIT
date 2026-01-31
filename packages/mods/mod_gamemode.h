#ifndef SHANKPIT_MOD_GAMEMODE_H
#define SHANKPIT_MOD_GAMEMODE_H

#include <string.h>

#include "../../include/mod_api.h"

#define MOD_MAX_GAMEMODES 32

typedef struct {
    char mod_id[64];
    mod_gamemode_t gamemode;
    int priority;
    unsigned int order;
} ModGameModeEntry;

typedef struct {
    ModGameModeEntry entries[MOD_MAX_GAMEMODES];
    int count;
    unsigned int next_order;
} ModGameModeRegistry;

static inline void mod_gamemode_registry_init(ModGameModeRegistry *registry) {
    memset(registry, 0, sizeof(ModGameModeRegistry));
}

static inline void mod_gamemode_registry_sort(ModGameModeRegistry *registry) {
    for (int i = 0; i < registry->count; i++) {
        for (int j = i + 1; j < registry->count; j++) {
            ModGameModeEntry *a = &registry->entries[i];
            ModGameModeEntry *b = &registry->entries[j];
            if (a->priority < b->priority ||
                (a->priority == b->priority && a->order > b->order)) {
                ModGameModeEntry tmp = *a;
                *a = *b;
                *b = tmp;
            }
        }
    }
}

static inline int mod_gamemode_register(ModGameModeRegistry *registry,
                                        const char *mod_id,
                                        const mod_gamemode_t *gm,
                                        int priority) {
    if (!gm || !gm->id || gm->id[0] == '\0') return -1;
    if (registry->count >= MOD_MAX_GAMEMODES) return -1;
    for (int i = 0; i < registry->count; i++) {
        if (strcmp(registry->entries[i].gamemode.id, gm->id) == 0) return -1;
    }

    ModGameModeEntry *entry = &registry->entries[registry->count++];
    memset(entry, 0, sizeof(ModGameModeEntry));
    strncpy(entry->mod_id, mod_id, sizeof(entry->mod_id) - 1);
    entry->gamemode = *gm;
    entry->priority = priority;
    entry->order = registry->next_order++;
    mod_gamemode_registry_sort(registry);
    return 0;
}

static inline int mod_gamemode_unregister(ModGameModeRegistry *registry,
                                          const char *mod_id,
                                          const char *gm_id) {
    int removed = 0;
    int write = 0;
    for (int i = 0; i < registry->count; i++) {
        ModGameModeEntry *entry = &registry->entries[i];
        if (strcmp(entry->gamemode.id, gm_id) == 0 &&
            (!mod_id || strcmp(entry->mod_id, mod_id) == 0)) {
            removed = 1;
            continue;
        }
        if (write != i) registry->entries[write] = *entry;
        write++;
    }
    registry->count = write;
    return removed ? 0 : -1;
}

static inline const mod_gamemode_t *mod_gamemode_find(ModGameModeRegistry *registry,
                                                      const char *gm_id,
                                                      const char **out_mod_id) {
    if (!gm_id) return NULL;
    for (int i = 0; i < registry->count; i++) {
        ModGameModeEntry *entry = &registry->entries[i];
        if (strcmp(entry->gamemode.id, gm_id) == 0) {
            if (out_mod_id) *out_mod_id = entry->mod_id;
            return &entry->gamemode;
        }
    }
    return NULL;
}

#endif
