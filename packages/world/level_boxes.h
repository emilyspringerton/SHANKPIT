#ifndef SHANKPIT_LEVEL_BOXES_H
#define SHANKPIT_LEVEL_BOXES_H

// level_boxes.h -- real, native JSON loader for a level authored in NOCK's SHANKPIT level editor
// (EMILY/BACKLOG.md SECTION 459), closing the real gap named directly by the founder: "get the
// level loading to work." Deliberately mirrors BRAWLPIT/packages/common/level_format.h's own real
// technique field-for-field (a real, small, dependency-free scanner scoped exactly to one known,
// flat, versioned JSON shape -- no general-purpose JSON library exists anywhere in this
// monorepo's C code) -- same real "smallest real thing" precedent, applied to a different game.
//
// REAL, FOUND, LIVE CORRECTION (2026-09-14): the level EDITOR (IDUNA/internal/shankpit,
// ShankpitLevelEditor.tsx) was built against packages/map/map.h's own `Wall` struct -- but that
// struct is used ONLY by services/game-server/src/server.c, a real, separate, currently-BROKEN
// (unrelated pre-existing compile errors), NOT-in-CI prototype, not the real, actual, CI-verified
// SHANKPIT client (apps/lobby + apps/server). That real client already has its own real,
// currently-used static-geometry primitive: packages/common/physics.h's own `Box{x,y,z,w,h,d}`
// (center x/y/z + full extents w/h/d -- confirmed against resolve_collision's own real
// `b.x - b.w/2 .. b.x + b.w/2` collision math), selected per-scene via `map_geo`/`map_count`.
// This file bridges NOCK's export JSON (shaped like `Wall`: id/x/y/z/sx/sy/sz/r/g/b/friction) into
// plain arrays a caller can hand to physics.h's own new `phys_set_custom_level` (see that file's
// own doc comment) -- deliberately NOT including physics.h itself, so this loader has zero
// dependency on the huge, scene-heavy physics header and can't accidentally couple to it.
//
// Field mapping, NOCK export -> this loader's own output (collision-relevant fields only; id and
// friction are read but not currently used -- named, not silently dropped, real future work once
// per-box friction/material actually matters to physics.h's own collision resolution):
//   sx -> w, sy -> h, sz -> d (full extents both sides -- exact match, no unit conversion needed)

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define LEVEL_BOXES_MAX 100 /* matches IDUNA/internal/shankpit.MaxWalls exactly -- the real,
                                already-enforced cap on the editor side, so a level saved there
                                can never exceed what this loader is willing to read back */
#define LEVEL_BOXES_MAX_NAME 64

typedef struct {
    float x, y, z;    /* center, world units -- matches physics.h's own real Box convention */
    float w, h, d;     /* full extents (NOT half-extents) -- matches NOCK's own sx/sy/sz exactly */
    float r, g, b;      /* real OpenGL-convention [0,1] color, for rendering only */
    float friction;      /* read, not yet consumed by collision -- real, named future work */
} LevelBox;

typedef struct {
    char name[LEVEL_BOXES_MAX_NAME];
    float width, height, depth; /* the editor's own real authored level dimensions -- NOT read
                                    back into any native bound today (same "authoring metadata,
                                    not native state" role BRAWLPIT's own width/height play for
                                    its 2D canvas) */
    LevelBox boxes[LEVEL_BOXES_MAX];
    int count;
} CustomLevelData;

static inline const char *level_boxes_skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

// level_boxes_find_key mirrors BRAWLPIT/level_format.h's own level_find_key exactly -- a real,
// minimal key search (not a full tokenizer), robust to key-order variation.
static inline const char *level_boxes_find_key(const char *start, const char *end, const char *key) {
    size_t keylen = strlen(key);
    char pattern[LEVEL_BOXES_MAX_NAME + 2];
    if (keylen + 2 >= sizeof(pattern)) return NULL;
    pattern[0] = '"';
    memcpy(pattern + 1, key, keylen);
    pattern[keylen + 1] = '"';
    pattern[keylen + 2] = '\0';

    const char *p = start;
    while (p < end) {
        const char *found = strstr(p, pattern);
        if (!found || found >= end) return NULL;
        const char *after = found + keylen + 2;
        after = level_boxes_skip_ws(after);
        if (*after == ':') return level_boxes_skip_ws(after + 1);
        p = found + 1;
    }
    return NULL;
}

static inline int level_boxes_parse_number(const char *p, float *out) {
    char *endptr = NULL;
    float v = strtof(p, &endptr);
    if (endptr == p) return 0;
    *out = v;
    return 1;
}

static inline int level_boxes_parse_string(const char *p, char *out, size_t outsize) {
    if (*p != '"') return 0;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < outsize) out[i++] = *p++;
    out[i] = '\0';
    return *p == '"';
}

// level_boxes_parse_json parses buf (a null-terminated JSON document, the exact shape IDUNA's
// GET /api/v1/shankpit-levels/:id/export returns) into *out. Returns 1 on success, 0 on any real
// failure (missing "walls" array, a malformed wall object, more walls than LEVEL_BOXES_MAX) --
// *out is left partially written on failure, matching level_format.h's own established contract.
static inline int level_boxes_parse_json(const char *buf, CustomLevelData *out) {
    memset(out, 0, sizeof(*out));
    size_t len = strlen(buf);
    const char *end = buf + len;

    const char *name_val = level_boxes_find_key(buf, end, "name");
    if (name_val) {
        level_boxes_parse_string(name_val, out->name, sizeof(out->name));
    } else {
        strncpy(out->name, "Untitled", sizeof(out->name) - 1);
    }

    const char *width_val = level_boxes_find_key(buf, end, "width");
    if (width_val) level_boxes_parse_number(width_val, &out->width);
    const char *height_val = level_boxes_find_key(buf, end, "height");
    if (height_val) level_boxes_parse_number(height_val, &out->height);
    const char *depth_val = level_boxes_find_key(buf, end, "depth");
    if (depth_val) level_boxes_parse_number(depth_val, &out->depth);

    const char *arr = level_boxes_find_key(buf, end, "walls");
    if (!arr) return 0;
    arr = level_boxes_skip_ws(arr);
    if (*arr != '[') return 0;
    arr++;

    const char *arr_end = strrchr(arr, ']');
    if (!arr_end) return 0;

    int count = 0;
    const char *cursor = arr;
    while (cursor < arr_end) {
        cursor = level_boxes_skip_ws(cursor);
        if (cursor >= arr_end) break;
        if (*cursor == ',') { cursor++; continue; }
        if (*cursor != '{') { cursor++; continue; }

        const char *obj_start = cursor;
        const char *obj_end = strchr(obj_start, '}');
        if (!obj_end || obj_end > arr_end) return 0;

        if (count >= LEVEL_BOXES_MAX) return 0; // real, honest bound -- refuse silent truncation

        float x = 0, y = 0, z = 0, sx = 0, sy = 0, sz = 0, r = 0, g = 0, b = 0, friction = 0;
        const char *v;
        int ok = 1;
        if ((v = level_boxes_find_key(obj_start, obj_end, "x"))) ok &= level_boxes_parse_number(v, &x); else ok = 0;
        if ((v = level_boxes_find_key(obj_start, obj_end, "y"))) ok &= level_boxes_parse_number(v, &y); else ok = 0;
        if ((v = level_boxes_find_key(obj_start, obj_end, "z"))) ok &= level_boxes_parse_number(v, &z); else ok = 0;
        if ((v = level_boxes_find_key(obj_start, obj_end, "sx"))) ok &= level_boxes_parse_number(v, &sx); else ok = 0;
        if ((v = level_boxes_find_key(obj_start, obj_end, "sy"))) ok &= level_boxes_parse_number(v, &sy); else ok = 0;
        if ((v = level_boxes_find_key(obj_start, obj_end, "sz"))) ok &= level_boxes_parse_number(v, &sz); else ok = 0;
        // Color/friction are real but optional here -- an older/hand-written file missing them
        // shouldn't fail the whole load; they just default to 0 (rendered as black, matching this
        // repo's own established "0 is a real, honest sentinel, not an error" convention).
        if ((v = level_boxes_find_key(obj_start, obj_end, "r"))) level_boxes_parse_number(v, &r);
        if ((v = level_boxes_find_key(obj_start, obj_end, "g"))) level_boxes_parse_number(v, &g);
        if ((v = level_boxes_find_key(obj_start, obj_end, "b"))) level_boxes_parse_number(v, &b);
        if ((v = level_boxes_find_key(obj_start, obj_end, "friction"))) level_boxes_parse_number(v, &friction);
        if (!ok) return 0;

        LevelBox *box = &out->boxes[count];
        box->x = x; box->y = y; box->z = z;
        box->w = sx; box->h = sy; box->d = sz;
        box->r = r; box->g = g; box->b = b;
        box->friction = friction;
        count++;
        cursor = obj_end + 1;
    }

    out->count = count;
    return 1;
}

// level_boxes_load_from_file reads path and parses it into *out. Returns 1 on success, 0 if the
// file doesn't exist, can't be read, or fails to parse -- *out is left untouched on any failure
// so a caller can safely fall back to a compiled-in default scene (matches
// level_load_from_file's own established contract exactly).
static inline int level_boxes_load_from_file(const char *path, CustomLevelData *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 1 << 20) { fclose(f); return 0; } // real, sane 1MB bound on a level file
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) { fclose(f); return 0; }
    size_t read = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[read] = '\0';

    CustomLevelData tmp;
    int ok = level_boxes_parse_json(buf, &tmp);
    free(buf);
    if (!ok) return 0;
    *out = tmp;
    return 1;
}

#endif
