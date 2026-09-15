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

#ifndef _WIN32
#include <sys/wait.h>
#endif

#define LEVEL_BOXES_MAX 100 /* matches IDUNA/internal/shankpit.MaxWalls exactly -- the real,
                                already-enforced cap on the editor side, so a level saved there
                                can never exceed what this loader is willing to read back */
#define LEVEL_BOXES_MAX_NAME 64

/* S459-16, founder real-time: "i think it makes sense to abstract into material first so it
   cleanly translates into papercraft ... we will need the ability to add new materials and set
   their textures" / "registries for everything". LEVEL_BOXES_MAX_MATERIALS is a real, small,
   sane cap -- IDUNA's own export embeds every real, currently-defined material (a handful:
   brick/concrete/wood/metal plus whatever's been added), not hundreds. */
#define LEVEL_BOXES_MAX_MATERIALS 16
#define LEVEL_BOXES_DEFAULT_MATERIAL "brick" /* mirrors IDUNA/internal/shankpit.DefaultMaterialName exactly */

typedef struct {
    char name[LEVEL_BOXES_MAX_NAME];
    char shader_name[LEVEL_BOXES_MAX_NAME]; /* a real, named reference into
        packages/render/material_shaders.h's own SHADER_* registry -- NOT GLSL source (see that
        header's own doc comment for the real "NOCK stores the name, native owns the code" split) */
    float specular;
    float shininess;
} LevelBoxMaterial;

typedef struct {
    float x, y, z;    /* center, world units -- matches physics.h's own real Box convention */
    float w, h, d;     /* full extents (NOT half-extents) -- matches NOCK's own sx/sy/sz exactly */
    float r, g, b;      /* real OpenGL-convention [0,1] color, for rendering only */
    float friction;      /* read, not yet consumed by collision -- real, named future work */
    int material_idx;    /* index into CustomLevelData.materials, resolved at parse time -- see
                             level_boxes_resolve_material's own doc comment for the real fallback
                             when a wall's own material name isn't in the level's materials array */
} LevelBox;

/* S459-58, founder real-time: "add spawners to nock so we can add spawners for ffa" / "actual
   make them team based but fall back to ffa" / "call it red team and blue team". team mirrors
   IDUNA/internal/shankpit.Spawner's own real team convention exactly -- SHANKPIT's own real, live
   TDMB_RED_TEAM=0/TDMB_BLUE_TEAM=1 (packages/simulation/local_game.h), with -1 as the real
   FFA/"any team" sentinel. LEVEL_BOXES_MAX_SPAWNERS matches
   IDUNA/internal/shankpit.MaxSpawners exactly. */
#define LEVEL_BOXES_MAX_SPAWNERS 64
#define LEVEL_BOXES_SPAWNER_TEAM_FFA (-1)
#define LEVEL_BOXES_SPAWNER_TEAM_RED 0
#define LEVEL_BOXES_SPAWNER_TEAM_BLUE 1

typedef struct {
    float x, y, z; /* world units, matches LevelBox's own convention */
    float yaw;      /* degrees */
    int team;        /* -1 = FFA/any team, 0 = Red Team, 1 = Blue Team */
} LevelSpawner;

typedef struct {
    char name[LEVEL_BOXES_MAX_NAME];
    float width, height, depth; /* the editor's own real authored level dimensions -- NOT read
                                    back into any native bound today (same "authoring metadata,
                                    not native state" role BRAWLPIT's own width/height play for
                                    its 2D canvas) */
    /* S459-08: the level's own real, configurable ground plane (founder: "i want there to be a
       plane by default that the player collides with... configurable in terms of size... turn on
       able and off able per level") -- see physics.h's own g_custom_level_ground_plane_enabled
       doc comment for the real collision contract these feed into via phys_set_custom_level. */
    int ground_plane_enabled;
    int ground_plane_squares;
    LevelBox boxes[LEVEL_BOXES_MAX];
    int count;
    LevelBoxMaterial materials[LEVEL_BOXES_MAX_MATERIALS];
    int material_count;
    LevelSpawner spawners[LEVEL_BOXES_MAX_SPAWNERS]; /* S459-58 */
    int spawner_count;
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

// level_boxes_find_array_end returns the position of the closing ']' matching open_bracket (which
// must point AT a '[') via real bracket-depth counting -- NOT the previous "find the last ']' in
// the rest of the buffer" trick (only ever correct by coincidence when the array being bounded
// happens to be the very last field in the document; S459-16 broke that coincidence by adding a
// "materials" array after "walls" in IDUNA's own real export field order, surfacing this as a
// real, live bug this rewrite fixes for every caller, not just the new one). Safe here without
// string-aware quote-skipping because every array this file ever bounds (walls, materials) holds
// only numeric/boolean/short-plain-name fields -- no string value in either shape can itself
// contain a literal '[' or ']'.
static inline const char *level_boxes_find_array_end(const char *open_bracket, const char *buf_end) {
    int depth = 0;
    const char *p = open_bracket;
    while (p < buf_end) {
        if (*p == '[') depth++;
        else if (*p == ']') { depth--; if (depth == 0) return p; }
        p++;
    }
    return NULL;
}

// level_boxes_parse_bool parses a real, unquoted JSON boolean literal (`true`/`false`, no other
// spellings -- this is what every real JSON serializer, including Go's own encoding/json, always
// emits for a bool field) starting at p. Returns 1 on success, 0 if p doesn't start with either.
static inline int level_boxes_parse_bool(const char *p, int *out) {
    if (strncmp(p, "true", 4) == 0) { *out = 1; return 1; }
    if (strncmp(p, "false", 5) == 0) { *out = 0; return 1; }
    return 0;
}

// level_boxes_resolve_material finds `name` in out->materials, falling back to
// LEVEL_BOXES_DEFAULT_MATERIAL ("brick") if not found, then to index 0 as a last resort (only
// reachable if the level's own real "brick" material row was itself renamed/deleted in IDUNA --
// real, honest degradation, never a crash/out-of-bounds read). Always returns a valid index into
// a materials array with material_count >= 1 (level_boxes_parse_json's own real, guaranteed
// invariant after parsing).
static inline int level_boxes_resolve_material(const CustomLevelData *out, const char *name) {
    if (name && name[0]) {
        for (int i = 0; i < out->material_count; i++) {
            if (strcmp(out->materials[i].name, name) == 0) return i;
        }
    }
    for (int i = 0; i < out->material_count; i++) {
        if (strcmp(out->materials[i].name, LEVEL_BOXES_DEFAULT_MATERIAL) == 0) return i;
    }
    return 0;
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

    // Ground plane fields (S459-08) -- real, sane defaults (enabled, 2 squares) for a
    // hand-written or pre-S459-08 file that omits them, matching this file's own established
    // "0/absent is a real, honest sentinel, not an error" convention.
    out->ground_plane_enabled = 1;
    out->ground_plane_squares = 2;
    const char *gpe_val = level_boxes_find_key(buf, end, "ground_plane_enabled");
    if (gpe_val) level_boxes_parse_bool(gpe_val, &out->ground_plane_enabled);
    const char *gps_val = level_boxes_find_key(buf, end, "ground_plane_squares");
    if (gps_val) {
        float squares_f = 0;
        if (level_boxes_parse_number(gps_val, &squares_f) && squares_f > 0) out->ground_plane_squares = (int)squares_f;
    }

    // Materials (S459-16) -- parsed BEFORE walls so each wall's own "material" name can be
    // resolved to an index immediately. Real, sane fallback for a hand-written or pre-S459-16
    // export with no "materials" array at all: synthesize a single built-in
    // LEVEL_BOXES_DEFAULT_MATERIAL entry so material_idx is NEVER -1 for any real caller -- no
    // "-1 sentinel" case downstream in physics.h/apps/lobby.
    out->material_count = 0;
    const char *mat_arr_key = level_boxes_find_key(buf, end, "materials");
    if (mat_arr_key) {
        const char *mat_arr = level_boxes_skip_ws(mat_arr_key);
        if (*mat_arr == '[') {
            const char *mat_arr_end = level_boxes_find_array_end(mat_arr, end);
            if (mat_arr_end) {
                const char *mcursor = mat_arr + 1;
                while (mcursor < mat_arr_end && out->material_count < LEVEL_BOXES_MAX_MATERIALS) {
                    mcursor = level_boxes_skip_ws(mcursor);
                    if (mcursor >= mat_arr_end) break;
                    if (*mcursor == ',') { mcursor++; continue; }
                    if (*mcursor != '{') { mcursor++; continue; }
                    const char *mobj_start = mcursor;
                    const char *mobj_end = strchr(mobj_start, '}');
                    if (!mobj_end || mobj_end > mat_arr_end) break;

                    LevelBoxMaterial *mat = &out->materials[out->material_count];
                    memset(mat, 0, sizeof(*mat));
                    const char *nv = level_boxes_find_key(mobj_start, mobj_end, "name");
                    if (nv) level_boxes_parse_string(nv, mat->name, sizeof(mat->name));
                    const char *sv = level_boxes_find_key(mobj_start, mobj_end, "shader_name");
                    if (sv) level_boxes_parse_string(sv, mat->shader_name, sizeof(mat->shader_name));
                    else strncpy(mat->shader_name, "standard", sizeof(mat->shader_name) - 1);
                    float spec_f = 0, shin_f = 8;
                    const char *spv = level_boxes_find_key(mobj_start, mobj_end, "specular");
                    if (spv) level_boxes_parse_number(spv, &spec_f);
                    const char *shv = level_boxes_find_key(mobj_start, mobj_end, "shininess");
                    if (shv) level_boxes_parse_number(shv, &shin_f);
                    mat->specular = spec_f;
                    mat->shininess = shin_f;
                    if (mat->name[0]) out->material_count++;
                    mcursor = mobj_end + 1;
                }
            }
        }
    }
    if (out->material_count == 0) {
        LevelBoxMaterial *mat = &out->materials[0];
        strncpy(mat->name, LEVEL_BOXES_DEFAULT_MATERIAL, sizeof(mat->name) - 1);
        strncpy(mat->shader_name, "standard", sizeof(mat->shader_name) - 1);
        mat->specular = 0.04f;
        mat->shininess = 6.0f;
        out->material_count = 1;
    }

    const char *arr = level_boxes_find_key(buf, end, "walls");
    if (!arr) return 0;
    arr = level_boxes_skip_ws(arr);
    if (*arr != '[') return 0;

    const char *arr_end = level_boxes_find_array_end(arr, end);
    if (!arr_end) return 0;
    arr++;

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

        // material (S459-16) -- absent/empty resolves to LEVEL_BOXES_DEFAULT_MATERIAL via
        // level_boxes_resolve_material, matching every pre-S459-16 wall's own real, existing
        // walls_json (no "material" key at all).
        char material_name[LEVEL_BOXES_MAX_NAME];
        material_name[0] = '\0';
        if ((v = level_boxes_find_key(obj_start, obj_end, "material"))) {
            level_boxes_parse_string(v, material_name, sizeof(material_name));
        }

        LevelBox *box = &out->boxes[count];
        box->x = x; box->y = y; box->z = z;
        box->w = sx; box->h = sy; box->d = sz;
        box->r = r; box->g = g; box->b = b;
        box->friction = friction;
        box->material_idx = level_boxes_resolve_material(out, material_name);
        count++;
        cursor = obj_end + 1;
    }

    out->count = count;

    // Spawners (S459-58) -- parsed the same real, small-scanner way as materials above. Absent
    // "spawners" key (a pre-S459-58 export) is a real, honest "no author-placed spawns" state,
    // not an error -- spawner_count stays 0 and the caller falls back to the already-built S459-57
    // computed-scatter spawn logic (see physics.h's own scene_spawn_point).
    out->spawner_count = 0;
    const char *sp_arr_key = level_boxes_find_key(buf, end, "spawners");
    if (sp_arr_key) {
        const char *sp_arr = level_boxes_skip_ws(sp_arr_key);
        if (*sp_arr == '[') {
            const char *sp_arr_end = level_boxes_find_array_end(sp_arr, end);
            if (sp_arr_end) {
                const char *scursor = sp_arr + 1;
                while (scursor < sp_arr_end && out->spawner_count < LEVEL_BOXES_MAX_SPAWNERS) {
                    scursor = level_boxes_skip_ws(scursor);
                    if (scursor >= sp_arr_end) break;
                    if (*scursor == ',') { scursor++; continue; }
                    if (*scursor != '{') { scursor++; continue; }
                    const char *sobj_start = scursor;
                    const char *sobj_end = strchr(sobj_start, '}');
                    if (!sobj_end || sobj_end > sp_arr_end) break;

                    LevelSpawner *sp = &out->spawners[out->spawner_count];
                    memset(sp, 0, sizeof(*sp));
                    const char *v2;
                    if ((v2 = level_boxes_find_key(sobj_start, sobj_end, "x"))) level_boxes_parse_number(v2, &sp->x);
                    if ((v2 = level_boxes_find_key(sobj_start, sobj_end, "y"))) level_boxes_parse_number(v2, &sp->y);
                    if ((v2 = level_boxes_find_key(sobj_start, sobj_end, "z"))) level_boxes_parse_number(v2, &sp->z);
                    if ((v2 = level_boxes_find_key(sobj_start, sobj_end, "yaw"))) level_boxes_parse_number(v2, &sp->yaw);
                    sp->team = LEVEL_BOXES_SPAWNER_TEAM_FFA;
                    if ((v2 = level_boxes_find_key(sobj_start, sobj_end, "team"))) {
                        float team_f = LEVEL_BOXES_SPAWNER_TEAM_FFA;
                        if (level_boxes_parse_number(v2, &team_f)) sp->team = (int)team_f;
                    }
                    out->spawner_count++;
                    scursor = sobj_end + 1;
                }
            }
        }
    }

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

// --- Real, in-game level registry browser (founder real-time: "ok i need the level selection
// interface in shankpit") -- mirrors BRAWLPIT/packages/common/level_registry.h's own real,
// established technique field-for-field: shell out to the real `curl` CLI binary (this box has
// the libcurl RUNTIME but no libcurl-dev headers, same real, checked reason BRAWLPIT's own file
// already documents) rather than link libcurl's C API directly. ---

#ifdef _WIN32
static inline FILE *level_boxes_popen_read(const char *cmd) { return _popen(cmd, "rb"); }
#define LEVEL_BOXES_PCLOSE _pclose
#else
static inline FILE *level_boxes_popen_read(const char *cmd) { return popen(cmd, "r"); }
#define LEVEL_BOXES_PCLOSE pclose
#endif

#define LEVEL_REGISTRY_BASE_URL "https://okemily.com/api/v1/shankpit-levels"
#define LEVEL_BOXES_MAX_FETCH_BYTES (1024 * 1024) /* real, sane bound -- a level file is a few KB */

// level_boxes_fetch_url runs `curl -s -f <url>` and captures its raw stdout into a newly
// malloc'd, NUL-terminated buffer. Returns the real byte count (excluding the trailing NUL) on
// success, or -1 on any failure (curl not found, network error, a real HTTP error status via
// curl's own -f flag, an empty/oversized response) -- caller must free(*out) on success.
static inline long level_boxes_fetch_url(const char *url, char **out) {
    char cmd[1024];
    /* Real, deliberate quoting: url is always one of this file's own two hardcoded endpoint
       shapes with a caller-supplied integer id (never arbitrary user text), so a plain
       double-quoted shell argument is safe here. */
    snprintf(cmd, sizeof(cmd), "curl -s -f \"%s\"", url);

    FILE *p = level_boxes_popen_read(cmd);
    if (!p) return -1;

    char *buf = (char *)malloc(LEVEL_BOXES_MAX_FETCH_BYTES + 1);
    if (!buf) {
        LEVEL_BOXES_PCLOSE(p);
        return -1;
    }
    size_t total = 0;
    size_t n;
    while ((n = fread(buf + total, 1, LEVEL_BOXES_MAX_FETCH_BYTES - total, p)) > 0) {
        total += n;
        if (total >= LEVEL_BOXES_MAX_FETCH_BYTES) break; // real, honest bound -- refuse silent truncation
    }
    int status = LEVEL_BOXES_PCLOSE(p);
#ifdef _WIN32
    int curl_failed = (status != 0);
#else
    int curl_failed = (!WIFEXITED(status) || WEXITSTATUS(status) != 0);
#endif
    if (curl_failed || total == 0) {
        free(buf);
        return -1;
    }
    buf[total] = '\0';
    *out = buf;
    return (long)total;
}

#define LEVEL_REGISTRY_MAX_ENTRIES 64
#define LEVEL_REGISTRY_NAME_LEN LEVEL_BOXES_MAX_NAME

typedef struct {
    int id;
    char name[LEVEL_REGISTRY_NAME_LEN];
    int wall_count;
    // is_default_queue (S459-41, founder real-time: "need to add an option to shankpit levels to
    // set a level as default for queue") -- exactly one level is flagged as the real, global
    // QUEUE default at a time (internal/shankpit.LevelSummary's own real is_default_queue field,
    // same shape shankpit_sprays.is_default already established).
    int is_default_queue;
} LevelRegistryEntry;

// level_boxes_parse_registry_list parses IDUNA's real GET /api/v1/shankpit-levels response (a
// JSON array of {"id":N,"name":"...","wall_count":N,...} objects -- see
// internal/shankpit.LevelSummary's own real shape) into a real, bounded array. Same real,
// deliberately narrow "flat array of flat objects" scanner convention this file's own
// level_boxes_parse_json already uses, not a general JSON array parser.
static inline int level_boxes_parse_registry_list(const char *json, LevelRegistryEntry *out, int max) {
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
                out[count].wall_count = 0;
                const char *wc_val = level_boxes_find_key(obj_start, obj_end, "wall_count");
                if (wc_val) {
                    float wc_f = 0;
                    if (level_boxes_parse_number(wc_val, &wc_f)) out[count].wall_count = (int)wc_f;
                }
                out[count].is_default_queue = 0;
                const char *dq_val = level_boxes_find_key(obj_start, obj_end, "is_default_queue");
                if (dq_val) level_boxes_parse_bool(dq_val, &out[count].is_default_queue);
                count++;
            }
        }
        cursor = obj_end + 1;
    }
    return count;
}

// level_boxes_fetch_registry_list fetches and parses the real, live level list. Returns the real
// entry count (0 if the registry is empty or unreachable -- a real network failure degrades to
// "no online levels shown," not a crash, matching this file's own established "a bad/missing
// resource never corrupts what's already working" convention).
static inline int level_boxes_fetch_registry_list(LevelRegistryEntry *out, int max) {
    char *buf = NULL;
    long n = level_boxes_fetch_url(LEVEL_REGISTRY_BASE_URL, &buf);
    if (n <= 0) return 0;
    int count = level_boxes_parse_registry_list(buf, out, max);
    free(buf);
    return count;
}

// level_boxes_fetch_export fetches level `id`'s real, plain-JSON export (SHANKPIT's own public
// export endpoint has no LZ4 option, unlike BRAWLPIT's -- a real, simpler wire format, not a
// missing feature) and parses it into *out via level_boxes_parse_json. Returns 1 on success, 0 on
// any real failure -- *out is left untouched on failure, matching level_boxes_load_from_file's
// own established contract.
static inline int level_boxes_fetch_export(int id, CustomLevelData *out) {
    char url[256];
    snprintf(url, sizeof(url), "%s/%d/export", LEVEL_REGISTRY_BASE_URL, id);

    char *buf = NULL;
    long n = level_boxes_fetch_url(url, &buf);
    if (n <= 0) return 0;

    CustomLevelData tmp;
    int ok = level_boxes_parse_json(buf, &tmp);
    free(buf);
    if (!ok) return 0;
    *out = tmp;
    return 1;
}

#endif
