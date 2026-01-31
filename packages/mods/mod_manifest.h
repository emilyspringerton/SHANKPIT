#ifndef SHANKPIT_MOD_MANIFEST_H
#define SHANKPIT_MOD_MANIFEST_H

#include <stdio.h>
#include <string.h>

typedef struct {
    char id[64];
    char name[128];
    char version[32];
    char api_version[16];
    char type[16];
    char entry[128];
    int priority;
    char capabilities[6][32];
    int capability_count;
} ModManifest;

static inline int manifest_extract_string(const char *src, const char *key, char *out, size_t out_len) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(src, pattern);
    if (!pos) return 0;
    pos = strchr(pos + strlen(pattern), ':');
    if (!pos) return 0;
    pos++;
    while (*pos && (*pos == ' ' || *pos == '\t')) pos++;
    if (*pos != '"') return 0;
    pos++;
    size_t i = 0;
    while (*pos && *pos != '"' && i + 1 < out_len) {
        out[i++] = *pos++;
    }
    out[i] = '\0';
    return i > 0;
}

static inline int manifest_extract_int(const char *src, const char *key, int *out) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(src, pattern);
    if (!pos) return 0;
    pos = strchr(pos + strlen(pattern), ':');
    if (!pos) return 0;
    pos++;
    *out = atoi(pos);
    return 1;
}

static inline void manifest_parse_capabilities(const char *src, ModManifest *out) {
    const char *cap = strstr(src, "\"capabilities\"");
    if (!cap) return;
    const char *start = strchr(cap, '[');
    if (!start) return;
    const char *end = strchr(start, ']');
    if (!end) return;
    const char *pos = start;
    while (pos < end && out->capability_count < 6) {
        const char *q = strchr(pos, '"');
        if (!q || q >= end) break;
        const char *q2 = strchr(q + 1, '"');
        if (!q2 || q2 >= end) break;
        size_t len = (size_t)(q2 - (q + 1));
        if (len > 0 && len < sizeof(out->capabilities[0])) {
            strncpy(out->capabilities[out->capability_count], q + 1, len);
            out->capabilities[out->capability_count][len] = '\0';
            out->capability_count++;
        }
        pos = q2 + 1;
    }
}

static inline int mod_manifest_load(const char *path, ModManifest *out) {
    memset(out, 0, sizeof(ModManifest));
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    char buf[2048];
    size_t len = fread(buf, 1, sizeof(buf) - 1, fp);
    buf[len] = '\0';
    fclose(fp);

    manifest_extract_string(buf, "id", out->id, sizeof(out->id));
    manifest_extract_string(buf, "name", out->name, sizeof(out->name));
    manifest_extract_string(buf, "version", out->version, sizeof(out->version));
    manifest_extract_string(buf, "api_version", out->api_version, sizeof(out->api_version));
    manifest_extract_string(buf, "type", out->type, sizeof(out->type));
    manifest_extract_string(buf, "entry", out->entry, sizeof(out->entry));
    manifest_extract_int(buf, "priority", &out->priority);
    manifest_parse_capabilities(buf, out);
    return out->id[0] != '\0';
}

#endif
