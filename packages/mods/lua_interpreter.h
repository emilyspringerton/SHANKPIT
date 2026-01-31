#ifndef SHANKPIT_LUA_INTERPRETER_H
#define SHANKPIT_LUA_INTERPRETER_H

#include <ctype.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    LUA_VALUE_NONE,
    LUA_VALUE_NUMBER,
    LUA_VALUE_STRING,
    LUA_VALUE_BOOL
} LuaValueType;

typedef struct {
    LuaValueType type;
    float number;
    int boolean;
    char string[128];
} LuaValue;

static inline const char *lua_skip_ws(const char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

static inline int lua_parse_identifier(const char **cursor, char *out, size_t out_len) {
    const char *s = lua_skip_ws(*cursor);
    size_t i = 0;
    if (!isalpha((unsigned char)*s) && *s != '_') return 0;
    while (*s && (isalnum((unsigned char)*s) || *s == '_')) {
        if (i + 1 < out_len) out[i++] = *s;
        s++;
    }
    out[i] = '\0';
    *cursor = s;
    return 1;
}

static inline int lua_parse_string(const char **cursor, char *out, size_t out_len) {
    const char *s = lua_skip_ws(*cursor);
    if (*s != '"' && *s != '\'') return 0;
    char quote = *s++;
    size_t i = 0;
    while (*s && *s != quote) {
        if (i + 1 < out_len) out[i++] = *s;
        s++;
    }
    if (*s != quote) return 0;
    out[i] = '\0';
    s++;
    *cursor = s;
    return 1;
}

static inline int lua_parse_number(const char **cursor, float *out) {
    const char *s = lua_skip_ws(*cursor);
    char *end = NULL;
    float val = strtof(s, &end);
    if (end == s) return 0;
    *out = val;
    *cursor = end;
    return 1;
}

static inline int lua_parse_bool(const char **cursor, int *out) {
    const char *s = lua_skip_ws(*cursor);
    if (strncmp(s, "true", 4) == 0) {
        *out = 1;
        *cursor = s + 4;
        return 1;
    }
    if (strncmp(s, "false", 5) == 0) {
        *out = 0;
        *cursor = s + 5;
        return 1;
    }
    return 0;
}

static inline int lua_parse_assignment(const char *line, char *out_key, size_t key_len, LuaValue *out_val) {
    const char *s = line;
    if (!lua_parse_identifier(&s, out_key, key_len)) return 0;
    s = lua_skip_ws(s);
    if (*s != '=') return 0;
    s++;
    if (lua_parse_string(&s, out_val->string, sizeof(out_val->string))) {
        out_val->type = LUA_VALUE_STRING;
        return 1;
    }
    if (lua_parse_number(&s, &out_val->number)) {
        out_val->type = LUA_VALUE_NUMBER;
        return 1;
    }
    if (lua_parse_bool(&s, &out_val->boolean)) {
        out_val->type = LUA_VALUE_BOOL;
        return 1;
    }
    out_val->type = LUA_VALUE_NONE;
    return 0;
}

static inline void lua_trim_comment(char *line) {
    for (size_t i = 0; line[i]; i++) {
        if (line[i] == '-' && line[i + 1] == '-') {
            line[i] = '\0';
            return;
        }
    }
}

static inline int lua_exec_script(const char *path,
                                  void (*on_assignment)(const char *key, const LuaValue *value, void *ctx),
                                  void *ctx) {
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        lua_trim_comment(line);
        char key[64];
        LuaValue val = {0};
        if (lua_parse_assignment(line, key, sizeof(key), &val)) {
            if (on_assignment) on_assignment(key, &val, ctx);
        }
    }
    fclose(fp);
    return 1;
}

#endif
