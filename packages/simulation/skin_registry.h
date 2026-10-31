/* skin_registry.h -- imported character sets as a list the engine can offer.
 *
 * A set is a directory under assets/goldenband/clipsets/<name>/ with the thirteen contract clips and
 * a skin.json naming the mesh (skin.gskel/.gmesh in the parent). This scans them so a host does not
 * need a hand-maintained table: every set NOCK imported is a candidate skin the moment it exists,
 * which is the whole point of the import affordance.
 *
 * Header-only and free of GL, so the scan is testable headlessly and the lobby can use it directly.
 * A directory without skin.json is NOT offered: a clip is channel data and channels only mean
 * something against one skeleton, so a set that does not name its mesh cannot be drawn. Refusing it
 * here is what stops a bundle being silently attached to whatever rig happens to be loaded.
 */
#ifndef SHANKPIT_SKIN_REGISTRY_H
#define SHANKPIT_SKIN_REGISTRY_H

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include "anim_state.h" /* the clip-name contract */

#define SKIN_REGISTRY_MAX 16
#define SKIN_REGISTRY_NAME_MAX 64
#define SKIN_REGISTRY_PATH_MAX 512

typedef struct {
    char name[SKIN_REGISTRY_NAME_MAX]; /* the directory name, which is the set's id */
    char dir[SKIN_REGISTRY_PATH_MAX];  /* full path to the set */
    char mesh[SKIN_REGISTRY_NAME_MAX]; /* skin.gskel/.gmesh stem, from skin.json */
    int bound;                         /* how many of the thirteen clips are present */
    int complete;                      /* bound == ANIM_STATE_COUNT */
} SkinEntry;

typedef struct {
    SkinEntry entries[SKIN_REGISTRY_MAX];
    int count;
    int skipped; /* directories found but not offered (no skin.json, no mesh, ...) */
} SkinRegistry;

/* skin_copy -- bounded copy. Every string here comes from the filesystem (a directory entry can be
   255 bytes, a nested path longer still), so the copies are precision-bounded rather than left to
   snprintf's silent truncation -- the compiler is right to refuse a format that claims it fits. */
static inline void skin_copy(char *dst, int dst_sz, const char *src) {
    if (!dst || dst_sz <= 0) return;
    snprintf(dst, (size_t)dst_sz, "%.*s", dst_sz - 1, src ? src : "");
}

/* read skin.json's "mesh" into out (empty on failure). A tiny scan, not a JSON parser: one known
   key, one known shape, same discipline the level scanners use. */
static inline int skin_registry_read_mesh(const char *set_dir, char *out, int out_sz) {
    if (!set_dir || !out || out_sz <= 1) return 0;
    char path[SKIN_REGISTRY_PATH_MAX];
    snprintf(path, sizeof path, "%.*s/skin.json", (int)sizeof path - 11, set_dir);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char buf[2048];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';

    const char *p = strstr(buf, "\"mesh\"");
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '"') p++;
    int k = 0;
    while (*p && *p != '"' && k < out_sz - 1) out[k++] = *p++;
    out[k] = '\0';
    return k > 0;
}

static inline int skin_registry_count_clips(const char *set_dir) {
    int found = 0;
    for (int s = 0; s < ANIM_STATE_COUNT; s++) {
        char path[SKIN_REGISTRY_PATH_MAX + 64];
        struct stat st;
        snprintf(path, sizeof path, "%.*s/%s.gband", (int)sizeof path - 12, set_dir,
                 anim_state_clip_name(s));
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) found++;
    }
    return found;
}

/* skin_registry_scan -- fill `reg` from `clipsets_dir`. Returns the number of offers (reg->count).
 * A missing directory is not an error: no imported skins is the normal state of a fresh checkout. */
static inline int skin_registry_scan(SkinRegistry *reg, const char *clipsets_dir) {
    if (!reg) return 0;
    memset(reg, 0, sizeof *reg);
    if (!clipsets_dir) return 0;
    DIR *d = opendir(clipsets_dir);
    if (!d) return 0;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && reg->count < SKIN_REGISTRY_MAX) {
        if (ent->d_name[0] == '.') continue;
        char dir[SKIN_REGISTRY_PATH_MAX];
        snprintf(dir, sizeof dir, "%.*s/%s", (int)sizeof dir - 257, clipsets_dir, ent->d_name);
        struct stat st;
        if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        char mesh[SKIN_REGISTRY_NAME_MAX] = "";
        if (!skin_registry_read_mesh(dir, mesh, sizeof mesh)) { reg->skipped++; continue; }

        SkinEntry *e = &reg->entries[reg->count];
        skin_copy(e->name, (int)sizeof e->name, ent->d_name);
        skin_copy(e->dir, (int)sizeof e->dir, dir);
        skin_copy(e->mesh, (int)sizeof e->mesh, mesh);
        e->bound = skin_registry_count_clips(dir);
        e->complete = (e->bound == ANIM_STATE_COUNT);
        /* Idle and walk are what the current draw path needs to show a character at all; a set
           missing them is not offered even if it has the other eleven. */
        {
            char p[SKIN_REGISTRY_PATH_MAX + 64];
            struct stat is, ws;
            snprintf(p, sizeof p, "%s/anim_idle.gband", dir);
            int has_idle = stat(p, &is) == 0;
            snprintf(p, sizeof p, "%s/anim_walk.gband", dir);
            int has_walk = stat(p, &ws) == 0;
            if (!has_idle || !has_walk) { reg->skipped++; continue; }
        }
        reg->count++;
    }
    closedir(d);
    return reg->count;
}

#endif /* SHANKPIT_SKIN_REGISTRY_H */
