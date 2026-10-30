/* tools/local_nock_host.c -- the thin C host for tools/local_nock.prn.
 *
 * Only what a PARENA program cannot do itself yet or at all: arenas, env, an id generator, and
 * file/directory access. Routing, JSON shapes, session state, validation and the git commit are
 * PARENA in local_nock.prn.
 */
#include "local_nock_host.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static Arena g_arena;
static Arena g_state;
static Arena g_request;
static Arena g_draft;

Arena *ln_state_arena(void) { return &g_state; }
Arena *ln_request_arena(void) { return &g_request; }
Arena *ln_draft_arena(void) { return &g_draft; }

int ln_request_reset(void) {
    arena_free_all(&g_request);
    return 1;
}

int ln_draft_reset(void) {
    arena_free_all(&g_draft);
    return 1;
}

int ln_port(void) {
    const char *p = getenv("LOCAL_NOCK_PORT");
    return (p && *p) ? atoi(p) : 8787;
}

static char *arena_strdup_local(const char *s, Arena *dest) {
    size_t n = strlen(s);
    char *out = (char *)arena_alloc(dest, n + 1);
    memcpy(out, s, n + 1);
    return out;
}

char *ln_env(const char *key, const char *fallback, Arena *dest) {
    const char *v = key ? getenv(key) : NULL;
    if (!v || !*v) v = fallback ? fallback : "";
    return arena_strdup_local(v, dest);
}

/* ln_new_id -- 32 lowercase hex chars, the shape edit_sync_create() checks for. The RNG is seeded
 * ONCE: re-seeding per call from time(NULL) ^ pid gave the same "random" id to every session
 * created inside the same second. */
char *ln_new_id(Arena *dest) {
    static int seeded = 0;
    static const char *hex = "0123456789abcdef";
    if (!seeded) {
        srand((unsigned)(time(NULL) ^ (long)getpid()));
        seeded = 1;
    }
    char *out = (char *)arena_alloc(dest, 33);
    for (int i = 0; i < 32; i++) out[i] = hex[rand() & 0xf];
    out[32] = 0;
    return out;
}

char *ln_level_path(const char *dir, const char *name, Arena *dest) {
    size_t cap = strlen(dir) + strlen(name) + 8;
    char *buf = (char *)arena_alloc(dest, cap);
    snprintf(buf, cap, "%s/%s.json", dir, name ? name : "");
    return buf;
}

char *ln_read_file(const char *path, Arena *dest) {
    FILE *f = fopen(path, "rb");
    if (!f) return arena_strdup_local("", dest);
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return arena_strdup_local("", dest); }
    long n = ftell(f);
    if (n < 0) { fclose(f); return arena_strdup_local("", dest); }
    rewind(f);
    char *buf = (char *)arena_alloc(dest, (size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = 0;
    return buf;
}

/* ln_write_file -- write-then-rename, so a reader never sees a half-written level. The temp file
 * lives beside the target (same directory, so rename is atomic on the same filesystem). */
int ln_write_file(const char *path, const char *text) {
    size_t plen = strlen(path);
    char *tmp = (char *)malloc(plen + 5);
    if (!tmp) return 0;
    memcpy(tmp, path, plen);
    memcpy(tmp + plen, ".tmp", 5);
    FILE *f = fopen(tmp, "wb");
    if (!f) { free(tmp); return 0; }
    size_t n = strlen(text);
    size_t wrote = fwrite(text, 1, n, f);
    int ok = (wrote == n);
    if (fclose(f) != 0) ok = 0;
    if (ok && rename(tmp, path) != 0) ok = 0;
    if (!ok) remove(tmp);
    free(tmp);
    return ok ? 1 : 0;
}

/* count_walls -- the number of top-level objects in the "walls" array. A directory listing that
 * claims a wall count has to count the array, not the file's every "sx": doors and other boxes
 * carry sx/sy/sz too. Scans for the walls array and counts '{' at depth 1, skipping strings so a
 * brace inside a string value cannot fool it. */
static int count_walls(const char *text) {
    const char *w = strstr(text, "\"walls\"");
    if (!w) return 0;
    const char *p = strchr(w, '[');
    if (!p) return 0;
    int depth = 0, count = 0, in_str = 0, esc = 0;
    for (; *p; p++) {
        if (in_str) {
            if (esc) esc = 0;
            else if (*p == '\\') esc = 1;
            else if (*p == '"') in_str = 0;
            continue;
        }
        if (*p == '"') { in_str = 1; continue; }
        if (*p == '[') depth++;
        else if (*p == ']') { depth--; if (depth == 0) break; }
        else if (*p == '{' && depth == 1) count++;
    }
    return count;
}

static int valid_name(const char *name) {
    size_t n = strlen(name);
    if (n == 0 || n > 64) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        if (!(isalnum(c) || c == '-' || c == '_')) return 0;
    }
    return 1;
}

/* ln_levels_json -- {"levels":[{name,walls,bytes,..},..]}. Only slug-shaped *.json files are
 * listed, the same rule the write path enforces, so a stray file in levels/ cannot be offered as a
 * level name that would later be rejected on save. */
char *ln_levels_json(const char *dir, Arena *dest) {
    size_t cap = 1 << 16, len = 0;
    char *out = (char *)arena_alloc(dest, cap);
    len += (size_t)snprintf(out + len, cap - len, "{\"levels\":[");
    DIR *d = opendir(dir);
    int first = 1;
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            size_t m = strlen(e->d_name);
            if (m < 6 || strcmp(e->d_name + m - 5, ".json") != 0) continue;
            char stem[512];
            if (m - 5 >= sizeof stem) continue;
            memcpy(stem, e->d_name, m - 5);
            stem[m - 5] = 0;
            if (!valid_name(stem)) continue;
            char path[2048];
            snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
            char *text = ln_read_file(path, dest);
            size_t bytes = strlen(text);
            if (len + 512 > cap) break;
            len += (size_t)snprintf(out + len, cap - len,
                                    "%s{\"name\":\"%s\",\"file\":\"levels/%s\",\"walls\":%d,\"bytes\":%zu}",
                                    first ? "" : ",", stem, e->d_name, count_walls(text), bytes);
            first = 0;
        }
        closedir(d);
    }
    snprintf(out + len, cap - len, "]}");
    return out;
}

int main(void) {
    arena_init(&g_arena);
    arena_init(&g_state);
    arena_init(&g_request);
    arena_init(&g_draft);
    return nock_run(&g_arena);
}
