/* ADDON METADATA (.toc) -- parse and order, without a heap and without a filesystem.
 *
 * One .toc per add-on: "## Key: value" directives, "#" comments, and bare lines naming the files to
 * load IN ORDER. Parsing takes a text buffer so the caller decides where it came from (a file now, an
 * archive later) and so this is testable headless. Unknown "##" keys are IGNORED, as WoW does -- an
 * add-on from a newer client must not fail to load because of a key we have never heard of.
 *
 * ORDERING is a real dependency problem, not a sort: required dependencies must be loaded first. The
 * walk is depth-first over a fixed array, and both failures are named instead of guessed at:
 * addon_toc_order returns -2 for a dependency CYCLE and -1 for a required dependency that is NOT
 * INSTALLED. There is no exit(), no partial load, and no implicit "just use alphabetical order".
 */
#ifndef SHANKPIT_UI_ADDON_TOC_H
#define SHANKPIT_UI_ADDON_TOC_H

#include <string.h>

#define TOC_ADDONS_MAX 32
#define TOC_FILES_MAX  128
#define TOC_DEPS_MAX     8
#define TOC_NAME_MAX    40
#define TOC_PATH_MAX    96
#define TOC_TEXT_MAX   128

typedef struct {
    char          name[TOC_NAME_MAX];       /* set by the caller from the directory name */
    unsigned long interface_version;
    char          title[TOC_TEXT_MAX];
    char          version[24];
    char          author[64];
    char          saved_variables[TOC_TEXT_MAX];
    char          deps[TOC_DEPS_MAX][TOC_NAME_MAX];       int dep_count;
    char          optional[TOC_DEPS_MAX][TOC_NAME_MAX];   int optional_count;
    char          files[TOC_FILES_MAX][TOC_PATH_MAX];     int file_count;
    int           bad_lines;                /* directive lines with no ':' -- reported, not fatal */
} AddonToc;

static inline char *toc_trim(char *s) {
    char *e;
    if (!s) return s;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = '\0';
    return s;
}

static inline void toc_copy(char *dst, size_t cap, const char *src) {
    size_t n = src ? strlen(src) : 0;
    if (cap == 0) return;
    if (n >= cap) n = cap - 1;
    if (n) memcpy(dst, src, n);
    dst[n] = '\0';
}

/* Comma or whitespace separated list into a fixed table. Returns the number stored, or -1 if it does
   not fit -- a silently truncated dependency list would change load order, which is the one thing this
   file exists to get right. */
static inline int toc_list_into(char dst[][TOC_NAME_MAX], int cap, const char *csv) {
    int n = 0;
    if (!csv) return 0;
    while (*csv) {
        char word[TOC_NAME_MAX];
        size_t len = 0;
        while (*csv == ',' || *csv == ' ' || *csv == '\t') csv++;
        if (!*csv) break;
        while (*csv && *csv != ',' && *csv != ' ' && *csv != '\t') {
            if (len + 1 >= sizeof word) return -1;
            word[len++] = *csv++;
        }
        word[len] = '\0';
        if (n >= cap) return -1;
        toc_copy(dst[n++], TOC_NAME_MAX, word);
    }
    return n;
}

/* Parse a .toc buffer. Returns the number of load files found, or -1 if a list overflowed or a path was
   too long -- an add-on that cannot be loaded exactly as written is an error, not a best effort. */
static inline int addon_toc_parse(const char *text, AddonToc *t) {
    const char *p = text;
    if (!t) return 0;
    if (t->name[0] == '\0') t->name[0] = '\0';
    t->interface_version = 0;
    t->title[0] = t->version[0] = t->author[0] = t->saved_variables[0] = '\0';
    t->dep_count = t->optional_count = t->file_count = t->bad_lines = 0;
    if (!text) return 0;
    while (*p) {
        char line[TOC_PATH_MAX * 2];
        size_t len = 0;
        while (*p && *p != '\n') {
            if (len + 1 >= sizeof line) return -1;
            line[len++] = *p++;
        }
        line[len] = '\0';
        if (*p == '\n') p++;
        {
            char *s = toc_trim(line);
            if (!*s) continue;
            if (s[0] == '#' && s[1] == '#') {
                char *key = toc_trim(s + 2);
                char *colon = strchr(key, ':');
                char *val;
                if (!colon) { t->bad_lines++; continue; }
                *colon = '\0';
                val = toc_trim(colon + 1);
                key = toc_trim(key);
                if (strcmp(key, "Interface") == 0) {
                    unsigned long v = 0;
                    const char *d = val;
                    while (*d >= '0' && *d <= '9') v = v * 10 + (unsigned long)(*d++ - '0');
                    t->interface_version = v;
                } else if (strcmp(key, "Title") == 0) {
                    toc_copy(t->title, sizeof t->title, val);
                } else if (strcmp(key, "Version") == 0) {
                    toc_copy(t->version, sizeof t->version, val);
                } else if (strcmp(key, "Author") == 0) {
                    toc_copy(t->author, sizeof t->author, val);
                } else if (strcmp(key, "SavedVariables") == 0 ||
                           strcmp(key, "SavedVariablesPerCharacter") == 0) {
                    toc_copy(t->saved_variables, sizeof t->saved_variables, val);
                } else if (strcmp(key, "Dependencies") == 0) {
                    int n = toc_list_into(t->deps, TOC_DEPS_MAX, val);
                    if (n < 0) return -1;
                    t->dep_count = n;
                } else if (strcmp(key, "OptionalDependencies") == 0) {
                    int n = toc_list_into(t->optional, TOC_DEPS_MAX, val);
                    if (n < 0) return -1;
                    t->optional_count = n;
                }   /* anything else: a key from a newer client, ignored on purpose */
            } else if (s[0] == '#') {
                continue;
            } else {
                if (strlen(s) >= TOC_PATH_MAX) return -1;
                if (t->file_count >= TOC_FILES_MAX) return -1;
                toc_copy(t->files[t->file_count++], TOC_PATH_MAX, s);
            }
        }
    }
    return t->file_count;
}

/* 0 ok, -1 a required dependency is not installed (*bad = the add-on that wanted it), -2 a cycle
   (*bad = where the walk closed). Flat lazy DFS: no allocation, and depth is bounded by the table. */
static inline int addon_toc_visit(AddonToc *list, int n, int i, int *state, int *order, int *count, int *bad) {
    int d, r;
    if (state[i] == 1) { if (bad) *bad = i; return -2; }
    if (state[i] == 2) return 0;
    state[i] = 1;
    for (d = 0; d < list[i].dep_count; d++) {
        for (r = 0; r < n; r++)
            if (strcmp(list[r].name, list[i].deps[d]) == 0) break;
        if (r == n) { if (bad) *bad = i; return -1; }
        {
            int rc = addon_toc_visit(list, n, r, state, order, count, bad);
            if (rc) return rc;
        }
    }
    state[i] = 2;
    if (count && order) order[(*count)++] = i;
    return 0;
}

static inline int addon_toc_order(AddonToc *list, int n, int *order, int *bad) {
    int state[TOC_ADDONS_MAX];
    int count = 0, i;
    if (bad) *bad = -1;
    if (!list || n < 0 || n > TOC_ADDONS_MAX) return -1;
    if (n == 0) return 0;
    memset(state, 0, sizeof state);
    for (i = 0; i < n; i++) {
        int rc = addon_toc_visit(list, n, i, state, order, &count, bad);
        if (rc) return rc;
    }
    return count;
}

#endif /* SHANKPIT_UI_ADDON_TOC_H */
