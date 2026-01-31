#ifndef SHANKPIT_MOD_LOADER_H
#define SHANKPIT_MOD_LOADER_H

#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <dirent.h>
#endif

#include "mod_manifest.h"

#define MOD_MAX_MANIFESTS 32

typedef struct {
    ModManifest manifests[MOD_MAX_MANIFESTS];
    int count;
} ModManifestList;

static inline void mod_manifest_list_init(ModManifestList *list) {
    memset(list, 0, sizeof(ModManifestList));
}

static inline void mod_manifest_list_add(ModManifestList *list, const ModManifest *manifest) {
    if (list->count >= MOD_MAX_MANIFESTS) return;
    list->manifests[list->count++] = *manifest;
}

static inline void mod_discover_manifests(ModManifestList *list, const char *mods_dir) {
#ifdef _WIN32
    (void)list;
    (void)mods_dir;
    return;
#else
    DIR *dir = opendir(mods_dir);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char path[256];
        snprintf(path, sizeof(path), "%s/%s/mod.json", mods_dir, entry->d_name);
        ModManifest manifest;
        if (mod_manifest_load(path, &manifest)) {
            mod_manifest_list_add(list, &manifest);
        }
    }
    closedir(dir);
#endif
}

static inline void mod_log_manifest(const ModManifest *manifest) {
    printf("[mods] %s (%s) type=%s entry=%s priority=%d\n",
           manifest->id, manifest->version, manifest->type, manifest->entry, manifest->priority);
}

#endif
