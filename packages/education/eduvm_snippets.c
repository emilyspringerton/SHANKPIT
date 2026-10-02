#include <stdio.h>
#include <string.h>
#include "eduvm_snippets.h"

/* reads a JSON string starting at *p (which points at the opening quote); writes decoded text to out; advances *p past the closing quote */
static int read_string(const char **p, char *out, int cap) {
    const char *s = *p;
    if (*s != '"') return 0;
    s++;
    int n = 0, overflow = 0;
    while (*s && *s != '"') {
        char c = *s++;
        if (c == '\\') {
            char e = *s++;
            switch (e) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case '/': c = '/'; break;
                case '\\': c = '\\'; break;
                case '"': c = '"'; break;
                case 'u': {
                    unsigned v = 0;
                    for (int i = 0; i < 4; i++) {
                        char h = s[i];
                        if (h >= '0' && h <= '9') v = v * 16 + (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') v = v * 16 + (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') v = v * 16 + (unsigned)(h - 'A' + 10);
                        else return 0;
                    }
                    s += 4;
                    c = v < 128 ? (char)v : '?';
                    break;
                }
                default: return 0;
            }
        }
        if (n < cap - 1) out[n++] = c; else overflow = 1;
    }
    if (*s != '"') return 0;
    out[n] = 0;
    *p = s + 1;
    return overflow ? -1 : 1; /* -1: parsed but truncated */
}

static void skip_ws(const char **p) { while (**p == ' ' || **p == '\n' || **p == '\t' || **p == '\r') (*p)++; }

/* skips one JSON value of any type (strings handled with escapes; nested containers by depth) */
static int skip_value(const char **p) {
    skip_ws(p);
    if (**p == '"') { char tmp[2]; const char *s = *p + 1; (void)tmp; while (*s && *s != '"') { if (*s == '\\' && s[1]) s++; s++; } if (*s != '"') return 0; *p = s + 1; return 1; }
    if (**p == '{' || **p == '[') {
        int depth = 0;
        const char *s = *p;
        do {
            if (*s == '"') { s++; while (*s && *s != '"') { if (*s == '\\' && s[1]) s++; s++; } if (!*s) return 0; }
            else if (*s == '{' || *s == '[') depth++;
            else if (*s == '}' || *s == ']') depth--;
            s++;
        } while (*s && depth > 0);
        if (depth != 0) return 0;
        *p = s;
        return 1;
    }
    while (**p && **p != ',' && **p != '}' && **p != ']') (*p)++;
    return 1;
}

int edu_snippets_parse(const char *json, EduSnippet *out, int max) {
    const char *p = json;
    int count = 0;
    skip_ws(&p);
    if (*p != '[') return 0;
    p++;
    for (;;) {
        skip_ws(&p);
        if (*p == ']' || !*p) break;
        if (*p == ',') { p++; continue; }
        if (*p != '{') return count;
        p++;
        EduSnippet sn;
        memset(&sn, 0, sizeof sn);
        int have_name = 0, have_src = 0, src_truncated = 0;
        for (;;) {
            skip_ws(&p);
            if (*p == '}') { p++; break; }
            if (*p == ',') { p++; continue; }
            char key[32];
            if (read_string(&p, key, sizeof key) == 0) return count;
            skip_ws(&p);
            if (*p != ':') return count;
            p++;
            skip_ws(&p);
            if (strcmp(key, "name") == 0 && *p == '"') {
                int r = read_string(&p, sn.name, (int)sizeof sn.name);
                if (r == 0) return count;
                have_name = r > 0;
            } else if (strcmp(key, "source") == 0 && *p == '"') {
                int r = read_string(&p, sn.source, (int)sizeof sn.source);
                if (r == 0) return count;
                have_src = 1;
                src_truncated = (r < 0);
            } else if (!skip_value(&p)) return count;
        }
        if (have_name && have_src && !src_truncated && count < max) out[count++] = sn;
        if (count >= max) break;
    }
    return count;
}

static int put(char *out, int cap, int *n, const char *s) {
    int l = (int)strlen(s);
    if (*n + l >= cap) return 0;
    memcpy(out + *n, s, (size_t)l);
    *n += l;
    out[*n] = 0;
    return 1;
}

static int put_escaped(char *out, int cap, int *n, const char *s) {
    if (!put(out, cap, n, "\"")) return 0;
    for (; *s; s++) {
        char b[8];
        switch (*s) {
            case '"': snprintf(b, sizeof b, "\\\""); break;
            case '\\': snprintf(b, sizeof b, "\\\\"); break;
            case '\n': snprintf(b, sizeof b, "\\n"); break;
            case '\t': snprintf(b, sizeof b, "\\t"); break;
            case '\r': snprintf(b, sizeof b, "\\r"); break;
            default:
                if ((unsigned char)*s < 32) snprintf(b, sizeof b, "\\u%04x", (unsigned)*s);
                else { b[0] = *s; b[1] = 0; }
        }
        if (!put(out, cap, n, b)) return 0;
    }
    return put(out, cap, n, "\"");
}

int edu_snippets_serialize(const EduScriptSystem *sys, char *out, int cap) {
    int n = 0, first = 1;
    out[0] = 0;
    if (!put(out, cap, &n, "[")) return -1;
    for (int i = 0; i < EDU_MAX_SCRIPTS; i++) {
        const EduScriptSlot *sl = &sys->slots[i];
        if (sl->source[0] == 0) continue;
        if (!first && !put(out, cap, &n, ",")) return -1;
        first = 0;
        if (!put(out, cap, &n, "{\"name\":") || !put_escaped(out, cap, &n, sl->name) ||
            !put(out, cap, &n, ",\"source\":") || !put_escaped(out, cap, &n, sl->source) || !put(out, cap, &n, "}")) return -1;
    }
    return put(out, cap, &n, "]") ? n : -1;
}

static void set_slot(EduScriptSlot *sl, const EduSnippet *sn) {
    snprintf(sl->name, sizeof sl->name, "%s", sn->name);
    snprintf(sl->source, sizeof sl->source, "%s", sn->source);
    sl->source_len = (int)strlen(sl->source);
    sl->compile_ok = 0;
}

int edu_snippets_fill_empty(EduScriptSystem *sys, const EduSnippet *sn, int n, int first) {
    int placed = 0;
    for (int slot = first < 0 ? 0 : first; slot < EDU_MAX_SCRIPTS && placed < n; slot++) {
        if (sys->slots[slot].source[0] != 0) continue;
        set_slot(&sys->slots[slot], &sn[placed++]);
    }
    return placed;
}

int edu_snippets_load_into(EduScriptSystem *sys, const EduSnippet *sn, int n) {
    int placed = 0;
    for (int i = 0; i < n && i < EDU_MAX_SCRIPTS; i++) { set_slot(&sys->slots[i], &sn[i]); placed++; }
    return placed;
}
