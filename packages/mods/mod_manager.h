#ifndef SHANKPIT_MOD_MANAGER_H
#define SHANKPIT_MOD_MANAGER_H

#ifndef _WIN32
#include <dirent.h>
#endif
#include <stdio.h>
#include <string.h>

#include "lua_interpreter.h"

#define MAX_MODS 16

typedef struct {
    char file[128];
    char name[64];
    int enabled;
    float head_scale;
} ModEntry;

typedef struct {
    ModEntry mods[MAX_MODS];
    int count;
    int selection;
} ModRegistry;

static inline void mod_apply_assignment(const char *key, const LuaValue *value, void *ctx) {
    ModEntry *mod = (ModEntry *)ctx;
    if (strcmp(key, "name") == 0 && value->type == LUA_VALUE_STRING) {
        strncpy(mod->name, value->string, sizeof(mod->name) - 1);
    } else if (strcmp(key, "enabled") == 0 && value->type == LUA_VALUE_BOOL) {
        mod->enabled = value->boolean;
    } else if (strcmp(key, "head_scale") == 0 && value->type == LUA_VALUE_NUMBER) {
        mod->head_scale = value->number;
    }
}

static inline void mods_init(ModRegistry *reg) {
    memset(reg, 0, sizeof(ModRegistry));
}

static inline void mods_load(ModRegistry *reg, const char *dir_path) {
#ifdef _WIN32
    (void)reg;
    (void)dir_path;
    return;
#else
    DIR *dir = opendir(dir_path);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (reg->count >= MAX_MODS) break;
        const char *name = entry->d_name;
        size_t len = strlen(name);
        if (len < 5) continue;
        if (strcmp(name + len - 4, ".lua") != 0) continue;

        ModEntry *mod = &reg->mods[reg->count];
        memset(mod, 0, sizeof(ModEntry));
        snprintf(mod->file, sizeof(mod->file), "%s/%s", dir_path, name);
        strncpy(mod->name, name, sizeof(mod->name) - 1);
        mod->enabled = 1;
        mod->head_scale = 1.0f;
        lua_exec_script(mod->file, mod_apply_assignment, mod);
        reg->count++;
    }
    closedir(dir);
#endif
}

static inline float mods_head_scale(const ModRegistry *reg) {
    float scale = 1.0f;
    for (int i = 0; i < reg->count; i++) {
        if (!reg->mods[i].enabled) continue;
        if (reg->mods[i].head_scale > scale) scale = reg->mods[i].head_scale;
    }
    return scale;
}

static inline void mods_on_match_start(ModRegistry *reg) {
    (void)reg;
}

#endif
