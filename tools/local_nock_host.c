/* tools/local_nock_host.c -- the thin C host for tools/local_nock.prn.
 *
 * Only what a PARENA program cannot do itself yet or at all: arenas, env, an id generator, and
 * file/directory access. Routing, JSON shapes, session state, validation and the git commit are
 * PARENA in local_nock.prn.
 */
#include "local_nock_host.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
/* Platform split, the same one PARENA's own runtime makes for tcp/process: this host is otherwise
 * POSIX-only, and the bundled dw_nock.exe (make local-nock-win) needs the same server. */
#ifdef _WIN32
  #include <winsock2.h>
  #include <windows.h>
  #include <io.h>
  #include <process.h>
  #include <direct.h>
  #define ln_getpid() ((int)_getpid())
#else
  #include <dirent.h>
  #include <unistd.h>
  #define ln_getpid() ((int)getpid())
#endif

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

/* ---- command line --------------------------------------------------------------------------
 * The SHANKPIT NOCK app (apps/lobby, APP_NOCK) spawns this binary as
 * `<server> --port 8787 --root <shankpit dir>` instead of setting LOCAL_NOCK_* variables: spawning
 * with an explicit command line is one call on Windows (CreateProcessA), setting an environment
 * block for a child is not. The variables still work for `make local-nock` / a hand start; flags
 * win over them. */
static int g_cli_port = 0;
static const char *g_cli_repo = NULL;
static const char *g_cli_levels = NULL;
static const char *g_cli_editor = NULL;
static char g_root_levels[1024];
static char g_root_editor[1024];

static const char *cli_value(int argc, char **argv, const char *flag) {
    for (int i = 1; i + 1 < argc; i++)
        if (strcmp(argv[i], flag) == 0) return argv[i + 1];
    return NULL;
}

int ln_port(void) {
    if (g_cli_port > 0) return g_cli_port;
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
    const char *v = NULL;
    if (key && strcmp(key, "LOCAL_NOCK_REPO") == 0) v = g_cli_repo;
    else if (key && strcmp(key, "LOCAL_NOCK_LEVELS") == 0) v = g_cli_levels;
    else if (key && strcmp(key, "LOCAL_NOCK_EDITOR") == 0) v = g_cli_editor;
    if ((!v || !*v) && key) v = getenv(key);
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
        srand((unsigned)(time(NULL) ^ (long)ln_getpid()));
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
    if (ok) {
#ifdef _WIN32
        /* POSIX rename() replaces an existing target, Windows's MoveFileEx needs to be told to. */
        ok = MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING) ? 1 : 0;
#else
        ok = (rename(tmp, path) == 0);
#endif
    }
    if (!ok) remove(tmp);
    free(tmp);
    return ok ? 1 : 0;
}

/* ln_dir_names -- every plain file name in `dir` (never . or ..), one per LN_NAME_MAX slot.
 * POSIX: readdir. Windows: FindFirstFileA. The listing rule is the host's job precisely because
 * it is the one thing that cannot be expressed the same way on both platforms. */
#ifdef _WIN32
int ln_dir_names(const char *dir, char (*names)[LN_NAME_MAX], int max, Arena *dest) {
    char pattern[2048];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int n = 0;
    (void)dest;
    snprintf(pattern, sizeof pattern, "%s/*", dir);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (n >= max) break;
        snprintf(names[n], LN_NAME_MAX, "%s", fd.cFileName);
        n++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}
#else
int ln_dir_names(const char *dir, char (*names)[LN_NAME_MAX], int max, Arena *dest) {
    DIR *d;
    struct dirent *e;
    int n = 0;
    (void)dest;
    d = opendir(dir);
    if (!d) return 0;
    while ((e = readdir(d)) != NULL) {
        if (n >= max) break;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        snprintf(names[n], LN_NAME_MAX, "%s", e->d_name);
        n++;
    }
    closedir(d);
    return n;
}
#endif

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
    char (*names)[LN_NAME_MAX] = (char (*)[LN_NAME_MAX])arena_alloc(dest, (size_t)LN_MAX_FILES * LN_NAME_MAX);
    int nfiles, fi;
    int first = 1;
    len += (size_t)snprintf(out + len, cap - len, "{\"levels\":[");
    nfiles = names ? ln_dir_names(dir, names, LN_MAX_FILES, dest) : 0;
    for (fi = 0; fi < nfiles; fi++) {
        const char *fname = names[fi];
        size_t m = strlen(fname);
        char stem[512];
        char path[2048];
        char *text;
        size_t bytes;
        if (m < 6 || strcmp(fname + m - 5, ".json") != 0) continue;
        if (m - 5 >= sizeof stem) continue;
        memcpy(stem, fname, m - 5);
        stem[m - 5] = 0;
        if (!valid_name(stem)) continue;
        snprintf(path, sizeof path, "%s/%s", dir, fname);
        text = ln_read_file(path, dest);
        bytes = strlen(text);
        if (len + 512 > cap) break;
        len += (size_t)snprintf(out + len, cap - len,
                                "%s{\"name\":\"%s\",\"file\":\"levels/%s\",\"walls\":%d,\"bytes\":%zu}",
                                first ? "" : ",", stem, fname, count_walls(text), bytes);
        first = 0;
    }
    snprintf(out + len, cap - len, "]}");
    return out;
}

int main(int argc, char **argv) {
    arena_init(&g_arena);
    arena_init(&g_state);
    arena_init(&g_request);
    arena_init(&g_draft);

    const char *port = cli_value(argc, argv, "--port");
    if (port && *port) g_cli_port = atoi(port);
    /* --root is the single flag the NOCK app passes: DIR/levels + DIR/tools/nock_editor.html, and
     * git commits run in DIR, so saved levels land in the SHANKPIT repo the lobby came from. */
    const char *root = cli_value(argc, argv, "--root");
    if (root && *root) {
        g_cli_repo = root;
        snprintf(g_root_levels, sizeof g_root_levels, "%s/levels", root);
        snprintf(g_root_editor, sizeof g_root_editor, "%s/tools/nock_editor.html", root);
        g_cli_levels = g_root_levels;
        g_cli_editor = g_root_editor;
    }
    const char *repo = cli_value(argc, argv, "--repo");
    if (repo && *repo) g_cli_repo = repo;
    const char *levels = cli_value(argc, argv, "--levels");
    if (levels && *levels) g_cli_levels = levels;
    const char *editor = cli_value(argc, argv, "--editor");
    if (editor && *editor) g_cli_editor = editor;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("local NOCK -- local map tools server (PARENA)\n"
                   "  --port N        listen port (default 8787, or LOCAL_NOCK_PORT)\n"
                   "  --root DIR      DIR/levels + DIR/tools/nock_editor.html; commits run in DIR\n"
                   "                  (this is what the SHANKPIT NOCK app passes)\n"
                   "  --repo DIR      git repo saved levels are committed into (default: cwd)\n"
                   "  --levels DIR    level JSON directory (default: <repo>/levels)\n"
                   "  --editor FILE   editor HTML served at /\n");
            return 0;
        }
    }
    return nock_run(&g_arena);
}
