/* tools/local_nock_host.c -- the thin C host for tools/local_nock.prn.
 *
 * Only what the VS0 emitter cannot express yet (named in the .prn header): the arena the generated
 * code runs in, env/argv, a session-id generator, the session strings that outlive one request, and
 * plain file/directory access (the emitter has no pointer-typed return and rejects set! on a
 * top-level def). Routing, JSON shapes, level validation and the git commit are PARENA.
 */
#include "local_nock_host.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static Arena g_arena;
static Arena g_scratch;

typedef struct {
    char id[64];
    char draft[4 << 20];
    char log[1 << 20];
    char seq[32];
    char repo[512];
    char levels[1024];
    char editor[1024];
} NockState;

static NockState g_state;

Arena *ln_scratch(void) { return &g_scratch; }

int ln_port(void) {
    const char *p = getenv("LOCAL_NOCK_PORT");
    return (p && *p) ? atoi(p) : 8787;
}

char *ln_new_id(Arena *dest) {
    static const char *hex = "0123456789abcdef";
    char *out = (char *)arena_alloc(dest, 33);
    srand((unsigned)(time(NULL) ^ (long)getpid()));
    for (int i = 0; i < 32; i++) out[i] = hex[rand() & 0xf];
    out[32] = 0;
    return out;
}

char *ln_state_get(const char *field, Arena *dest) {
    const char *v = "";
    if (!strcmp(field, "id")) v = g_state.id;
    else if (!strcmp(field, "draft")) v = g_state.draft;
    else if (!strcmp(field, "log")) v = g_state.log;
    else if (!strcmp(field, "seq")) v = g_state.seq;
    else if (!strcmp(field, "repo")) v = g_state.repo;
    else if (!strcmp(field, "levels")) v = g_state.levels;
    else if (!strcmp(field, "editor")) v = g_state.editor;
    size_t n = strlen(v);
    char *out = (char *)arena_alloc(dest, n + 1);
    memcpy(out, v, n + 1);
    return out;
}

int ln_state_set(const char *field, const char *value) {
    char *dst = NULL;
    size_t cap = 0;
    if (!strcmp(field, "id")) { dst = g_state.id; cap = sizeof g_state.id; }
    else if (!strcmp(field, "draft")) { dst = g_state.draft; cap = sizeof g_state.draft; }
    else if (!strcmp(field, "log")) { dst = g_state.log; cap = sizeof g_state.log; }
    else if (!strcmp(field, "seq")) { dst = g_state.seq; cap = sizeof g_state.seq; }
    if (!dst) return 0;
    snprintf(dst, cap, "%s", value ? value : "");
    return 1;
}

char *ln_level_path(const char *name, Arena *dest) {
    char buf[1200];
    snprintf(buf, sizeof buf, "%s/%s.json", g_state.levels, name ? name : "");
    size_t n = strlen(buf);
    char *out = (char *)arena_alloc(dest, n + 1);
    memcpy(out, buf, n + 1);
    return out;
}

char *ln_read_file(const char *path, Arena *dest) {
    FILE *f = fopen(path, "rb");
    if (!f) return (char *)"";
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return ""; }
    char *buf = (char *)arena_alloc(dest, (size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = 0;
    return buf;
}

int ln_write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    size_t n = strlen(text);
    size_t wrote = fwrite(text, 1, n, f);
    int ok = (wrote == n);
    if (fclose(f) != 0) ok = 0;
    return ok ? 1 : 0;
}

char *ln_editor_html(Arena *dest) { return ln_read_file(g_state.editor, dest); }

/* ln_log_event -- append one posted event ({"source":..,"kind":..,"data":..}) to the room's log,
 * injecting its seq. Kept in the host only because the seq counter itself has to outlive a request;
 * the event shape stays whatever the client sent (the C client reads x/y/z/yaw/mode out of `data` by
 * scanning the raw text, so no re-serialisation is needed). */
int ln_log_event(const char *body) {
    char event[8192];
    const char *payload = (body && body[0] == '{') ? body + 1 : (body ? body : "");
    size_t used = strlen(g_state.log);
    int n = snprintf(event, sizeof event, "%s{\"seq\":%s,%s",
                     used ? "," : "", g_state.seq, payload);
    if (n <= 0) return 0;
    size_t room = sizeof g_state.log - used - 1;
    size_t add = strlen(event);
    if (add > room) add = room;
    memcpy(g_state.log + used, event, add);
    g_state.log[used + add] = 0;
    snprintf(g_state.seq, sizeof g_state.seq, "%d", atoi(g_state.seq) + 1);
    return 1;
}

char *ln_session_json(Arena *dest) {
    size_t cap = strlen(g_state.log) + 256;
    char *out = (char *)arena_alloc(dest, cap);
    snprintf(out, cap, "{\"id\":\"%s\",\"seq\":%s,\"events\":[%s]}",
             g_state.id, g_state.seq, g_state.log);
    return out;
}

/* ln_levels_json -- {"levels":[{"name":..,"file":..,"bytes":N},..]} for the editor's level list. */
char *ln_levels_json(Arena *dest) {
    size_t cap = 1 << 16, len = 0;
    char *out = (char *)arena_alloc(dest, cap);
    len += (size_t)snprintf(out + len, cap - len, "{\"levels\":[");
    DIR *d = opendir(g_state.levels);
    int first = 1;
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            size_t m = strlen(e->d_name);
            if (m < 6 || strcmp(e->d_name + m - 5, ".json") != 0) continue;
            char path[2048];
            snprintf(path, sizeof path, "%s/%s", g_state.levels, e->d_name);
            FILE *f = fopen(path, "rb");
            long bytes = 0;
            if (f) { fseek(f, 0, SEEK_END); bytes = ftell(f); fclose(f); }
            char stem[512];
            snprintf(stem, sizeof stem, "%.*s", (int)(m - 5), e->d_name);
            len += (size_t)snprintf(out + len, cap - len,
                                    "%s{\"name\":\"%s\",\"file\":\"levels/%s\",\"bytes\":%ld}",
                                    first ? "" : ",", stem, e->d_name, bytes);
            first = 0;
        }
        closedir(d);
    }
    snprintf(out + len, cap - len, "]}");
    return out;
}

int nock_run(Arena *dest);

int main(void) {
    arena_init(&g_arena);
    arena_init(&g_scratch);

    const char *repo = getenv("LOCAL_NOCK_REPO");
    snprintf(g_state.repo, sizeof g_state.repo, "%s", (repo && *repo) ? repo : ".");
    const char *levels = getenv("LOCAL_NOCK_LEVELS");
    if (levels && *levels) snprintf(g_state.levels, sizeof g_state.levels, "%s", levels);
    else snprintf(g_state.levels, sizeof g_state.levels, "%s/levels", g_state.repo);
    const char *editor = getenv("LOCAL_NOCK_EDITOR");
    if (editor && *editor) snprintf(g_state.editor, sizeof g_state.editor, "%s", editor);
    else snprintf(g_state.editor, sizeof g_state.editor, "%s/tools/nock_editor.html", g_state.repo);
    snprintf(g_state.seq, sizeof g_state.seq, "0");

    return nock_run(&g_arena);
}
