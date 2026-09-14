#ifndef SHANKPIT_SPRAY_REGISTRY_H
#define SHANKPIT_SPRAY_REGISTRY_H

// spray_registry.h -- native client access to SHANKPIT's real sprays registry (S459-23, founder
// real-time: "remap that key to spray please and ensure there is a spray interface in the client
// HAVE IT REPLACE TDMO in the shankpit menu"). Deliberately reuses level_boxes.h's own real
// curl-via-popen fetch (level_boxes_fetch_url) and hand-rolled JSON scanner helpers
// (level_boxes_find_key/level_boxes_parse_number/level_boxes_parse_string/level_boxes_parse_bool)
// instead of duplicating them -- same real "smallest real thing" precedent that header itself
// documents, applied one layer up.
//
// REAL, HONEST, NOT YET BUILT: this file only ever fetches a spray's NAME/ID/is_default -- never
// its PNG bytes. SHANKPIT's own proc_tex.c generates RGBA procedurally; it has no PNG decoder, so
// there is no real way to turn a spray's actual uploaded artwork into a GL texture natively yet
// (the same real gap S459-16's own material texture-override already named). draw_spray_decal
// (apps/lobby) renders a real, honest placeholder -- a flat-tinted quad, color derived from the
// spray's own id, not its real pixel content -- until a real image decoder lands.

#include "level_boxes.h"

#define SPRAY_REGISTRY_MAX_ENTRIES 64
#define SPRAY_REGISTRY_NAME_LEN LEVEL_BOXES_MAX_NAME
#define SPRAY_REGISTRY_BASE_URL "https://okemily.com/api/v1/shankpit-sprays"

typedef struct {
    int id;
    char name[SPRAY_REGISTRY_NAME_LEN];
    int is_default;
} SprayRegistryEntry;

// spray_registry_parse_list parses IDUNA's real GET /api/v1/shankpit-sprays response (a JSON
// array of {"id":N,"name":"...","is_default":bool,...} objects -- see
// internal/shankpit.SpraySummary's own real shape) -- same real "flat array of flat objects"
// scanner convention level_boxes_parse_registry_list already established.
static inline int spray_registry_parse_list(const char *json, SprayRegistryEntry *out, int max) {
    int count = 0;
    const char *cursor = json;
    while (*cursor && count < max) {
        const char *obj_start = strchr(cursor, '{');
        if (!obj_start) break;
        const char *obj_end = strchr(obj_start, '}');
        if (!obj_end) break;

        const char *id_val = level_boxes_find_key(obj_start, obj_end, "id");
        const char *name_val = level_boxes_find_key(obj_start, obj_end, "name");
        if (id_val && name_val) {
            float id_f;
            if (level_boxes_parse_number(id_val, &id_f)) {
                out[count].id = (int)id_f;
                level_boxes_parse_string(name_val, out[count].name, sizeof(out[count].name));
                out[count].is_default = 0;
                const char *def_val = level_boxes_find_key(obj_start, obj_end, "is_default");
                if (def_val) level_boxes_parse_bool(def_val, &out[count].is_default);
                count++;
            }
        }
        cursor = obj_end + 1;
    }
    return count;
}

// spray_registry_fetch_list fetches and parses the real, live sprays list. Returns the real entry
// count (0 if the registry is empty or unreachable -- a real network failure degrades to "no
// sprays shown," not a crash, matching level_boxes.h's own established convention).
static inline int spray_registry_fetch_list(SprayRegistryEntry *out, int max) {
    char *buf = NULL;
    long n = level_boxes_fetch_url(SPRAY_REGISTRY_BASE_URL, &buf);
    if (n <= 0) return 0;
    int count = spray_registry_parse_list(buf, out, max);
    free(buf);
    return count;
}

#endif
