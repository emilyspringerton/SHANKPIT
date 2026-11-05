/* apps/game/level_catalog.c -- a small, strict parser for the level manifest.
 *
 * This is host glue, not a rule: the decisions (default mask, mandatory bits, sanitising an override)
 * are packages/game/level_mods.prn. The only thing here is reading a trusted server config file into
 * level_mods_register()/level_mods_apply_state() calls.
 *
 * The parser is intentionally narrow: an object array under "levels", integer id/env/mods and a plain
 * string name. It copes with whitespace, key order, and strings containing braces; it does not do
 * escapes, nested objects, or comments. A manifest outside that shape is refused rather than guessed
 * at, and every entry is parsed before ANY level is registered.
 */
#include "level_catalog.h"

#include "game_api.h"
#include "level_mods_host.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LEVEL_MANIFEST_MAX_BYTES 65536

static LevelDesc g_levels[LEVEL_MODS_MAX_LEVELS];
static int g_count;

void level_catalog_reset(void) {
    memset(g_levels, 0, sizeof(g_levels));
    g_count = 0;
}

static const char *skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

static const char *find_key(const char *obj, const char *key) {
    char pattern[48];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    return strstr(obj, pattern);
}

static int json_int(const char *obj, const char *key, int fallback) {
    const char *p = find_key(obj, key);
    if (!p) return fallback;
    p = strchr(p, ':');
    if (!p) return fallback;
    return (int)strtol(p + 1, NULL, 10);
}

/* Copy a plain JSON string value. Returns 1 only when the key exists, opens a quote, and the value is
 * non-empty and fits. No escape handling: a name with a quote or backslash is a malformed manifest. */
static int json_string(const char *obj, const char *key, char *out, size_t cap) {
    const char *p = find_key(obj, key);
    size_t n = 0;

    if (!p || cap == 0) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p = skip_ws(p + 1);
    if (*p != '"') return 0;
    p++;
    while (*p && *p != '"' && *p != '\\' && n + 1 < cap) out[n++] = *p++;
    out[n] = '\0';
    return n > 0 && *p == '"';
}

/* The matching close brace, ignoring braces inside strings. NULL when the object never closes. */
static const char *object_end(const char *p) {
    int depth = 0, in_string = 0, escaped = 0;
    for (; *p; p++) {
        char c = *p;
        if (in_string) {
            if (escaped) escaped = 0;
            else if (c == '\\') escaped = 1;
            else if (c == '"') in_string = 0;
            continue;
        }
        if (c == '"') { in_string = 1; continue; }
        if (c == '{') depth++;
        else if (c == '}') {
            depth--;
            if (depth == 0) return p;
        }
    }
    return NULL;
}

int level_catalog_load(const char *path) {
    FILE *f;
    long size;
    char *text;
    const char *levels, *p;
    LevelDesc parsed[LEVEL_MODS_MAX_LEVELS];
    int count = 0, i;

    if (!path || !(f = fopen(path, "rb"))) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -2; }
    size = ftell(f);
    if (size <= 0 || size > LEVEL_MANIFEST_MAX_BYTES) { fclose(f); return -2; }
    rewind(f);
    text = (char *)malloc((size_t)size + 1);
    if (!text) { fclose(f); return -2; }
    if (fread(text, 1, (size_t)size, f) != (size_t)size) {
        free(text);
        fclose(f);
        return -2;
    }
    fclose(f);
    text[size] = '\0';

    levels = strstr(text, "\"levels\"");
    if (!levels || !(p = strchr(levels, '['))) { free(text); return -2; }
    p++;

    while (*p) {
        const char *end;
        size_t len;
        char object[1024];
        LevelDesc *d;
        int id, env, mods;

        p = skip_ws(p);
        while (*p == ',') p = skip_ws(p + 1);
        if (*p == ']') break;
        if (*p != '{' || count == LEVEL_MODS_MAX_LEVELS) { free(text); return -2; }
        end = object_end(p);
        if (!end) { free(text); return -2; }
        len = (size_t)(end - p) + 1;
        if (len >= sizeof(object)) { free(text); return -2; }
        memcpy(object, p, len);
        object[len] = '\0';

        id = json_int(object, "id", -1);
        env = json_int(object, "env", -1);
        mods = json_int(object, "mods", -1);
        if (id <= 0 || id > 65535 || !lm_env_valid(env)) { free(text); return -3; }

        d = &parsed[count];
        memset(d, 0, sizeof(*d));
        if (!json_string(object, "name", d->name, sizeof(d->name))) { free(text); return -3; }
        d->id = id;
        d->env = env;
        d->mask = -1; /* filled after registration */

        for (i = 0; i < count; i++) {
            if (parsed[i].id == id) { free(text); return -3; }
        }
        if (mods >= 0) {
            /* Keep the override in the entry until the commit loop below, where it is sanitised. */
            d->revision = mods;
        } else {
            d->revision = -1;
        }
        count++;
        p = end + 1;
    }
    free(text);

    if (count == 0) return -2;

    /* Everything parsed and validated: now touch the live registry. */
    level_mods_reset();
    for (i = 0; i < count; i++) {
        int override = parsed[i].revision;
        if (!level_mods_register(parsed[i].id, parsed[i].env)) {
            level_mods_reset();
            return -2;
        }
        if (override >= 0) level_mods_apply_state(parsed[i].id, override, 1);
        parsed[i].mask = level_mods_mask(parsed[i].id);
        parsed[i].revision = level_mods_revision(parsed[i].id);
    }

    memset(g_levels, 0, sizeof(g_levels));
    memcpy(g_levels, parsed, sizeof(LevelDesc) * (size_t)count);
    g_count = count;
    return count;
}

int level_catalog_count(void) { return g_count; }

const LevelDesc *level_catalog_at(int index) {
    if (index < 0 || index >= g_count) return NULL;
    return &g_levels[index];
}

const LevelDesc *level_catalog_find(int level_id) {
    int i;
    for (i = 0; i < g_count; i++) {
        if (g_levels[i].id == level_id) return &g_levels[i];
    }
    return NULL;
}

const char *level_catalog_name(int level_id) {
    const LevelDesc *d = level_catalog_find(level_id);
    return d ? d->name : "";
}
