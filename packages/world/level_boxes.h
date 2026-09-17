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
// a Wall's own per-box `friction` are read but still not consumed -- real, deliberate, S478b:
// ground friction resolves per-MATERIAL now (LevelBoxMaterial.friction below, consumed by
// physics.h's own apply_friction via material_idx), not per-box, matching how shader_name/
// specular/shininess already work. A Wall's own bare `friction` field stays parsed-but-ignored
// legacy, not silently dropped -- named here, not a regression):
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
    float friction; /* S478b, founder real-time: "make the material friction stuff working per
        cube" -- real, live ground friction, resolved per-box via material_idx and consumed by
        packages/common/physics.h's own apply_friction (NOT this file -- level_boxes.h stays
        physics.h-free by design, see this header's own top-of-file doc comment). Absent in a
        pre-S478b export defaults to 0.30, matching physics.h's own tuned global FRICTION
        baseline exactly (see this field's own parse site below). */
} LevelBoxMaterial;

typedef struct {
    float x, y, z;    /* center, world units -- matches physics.h's own real Box convention */
    float w, h, d;     /* full extents (NOT half-extents) -- matches NOCK's own sx/sy/sz exactly */
    float r, g, b;      /* real OpenGL-convention [0,1] color, for rendering only */
    float friction;      /* read, deliberately NOT consumed by collision -- S478b resolves ground
                             friction per-MATERIAL instead (materials[material_idx].friction),
                             not per-box; this legacy field stays parsed but ignored */
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

/* Story System Phase 1 (docs/STORY_SYSTEM_NORTHSTAR.md Part 2) -- the first real scriptable map
   object, door only for this pass (ladder/screen/character/trigger are named future kinds, not
   yet implemented). A door is a real, existing LevelBox (box_index, 0-based into boxes[]) whose
   collision the server toggles open/closed based on a compiled PARENA script's own decision
   (see apps/server/src/main.c's DoorRuntime and phys_set_custom_level_box_y). script_path is a
   real local filesystem path to a compiled .so, still supported as a local-dev fallback; the
   real, live path is script_url (S459-81/82: IDUNA's nock_door_scripts repository compiles
   PARENA -> .so and serves it for download) -- and, as of 2026-09-17 (founder real-time: "how do
   i put doors in my levels?"), NOCK's own ShankpitLevelEditor.tsx has a real UI to attach one of
   those scripts to a placed wall (IDUNA/internal/shankpit.Door, exported via doorsForExport).
   The one thing that stays real, named, deferred future work: a door authored this way can only
   reference one of a level's own ROOT walls, never a wall contributed by a nested composed
   object (matching flattenObjects' own already-established scope limit for ground planes). */
#define LEVEL_BOXES_MAX_DOORS 16
#define LEVEL_BOXES_SCRIPT_PATH_LEN 256

typedef struct {
    int box_index;                              /* which boxes[] entry this door controls */
    /* subscribe_button_id (S485, REFLUX pub/sub -- founder real-time: "the bridge listens for a
       button the button has no idea the bridge exists"). -1 (the default, every pre-S485 level's
       own real absent-key JSON) means "normal auto-proximity door," completely unchanged. >= 0
       means this door ignores distance-to-player ENTIRELY and instead toggles open/closed only
       when a REFLUX_ACTION_BUTTON_PRESSED action with a == this value appears in the shared
       REFLUX log (packages/reflux/reflux_runtime.h) -- i.e. this door subscribes to whichever
       LevelButton was authored with that same button_id, by convention, with zero direct
       reference between the two objects beyond that shared integer. See story_doors.h's own
       story_doors_tick for the real polling/toggle logic. */
    int subscribe_button_id;
    char script_path[LEVEL_BOXES_SCRIPT_PATH_LEN]; /* local path to a compiled door_tick .so --
        used directly if set; empty means "use script_url instead" */
    char script_url[LEVEL_BOXES_SCRIPT_PATH_LEN]; /* S459-82: a real, downloadable URL (e.g.
        IDUNA's own GET /api/v1/nock-door-scripts/:id/download, internal/http/handlers/
        nock_door_scripts_public.go) -- story_doors.h downloads this once at level load and
        caches it locally before dlopen, same real "no local script authoring toolchain needed"
        closing of the original "via the nock tools" gap this whole system started from. Empty
        script_path AND script_url (the real, common case: a door with no custom PARENA script
        attached at all) is NOT an error -- see door_tick_builtin_proximity below, the real
        default behavior every door gets without requiring any script. */
} LevelDoor;

/* LevelButton (S485, REFLUX pub/sub) -- a real, author-placed, reusable interact trigger.
 * Founder real-time: "we need buttons - how am i gonna put a button on a wall next to a door to
 * open a door?" -> "we need a reusable button that can be put in different places... we dont want
 * those [interaction paths] to be totally different code paths unless there is a good reason."
 * Deliberately generic: a button knows NOTHING about what it controls -- pressing it (a real,
 * edge-triggered BTN_USE interact within range, see story_buttons.h) only ever dispatches
 * REFLUX_ACTION_BUTTON_PRESSED(button_id, player_id, 0) into the shared REFLUX log. Any number of
 * doors (or, real future work, other REFLUX subscribers -- vents, bridges, AI reactions) can
 * independently subscribe to that same button_id with zero coupling to the button or to each
 * other -- matching the founder's own explicit "the button has no idea the bridge exists."
 * button_id is author-assigned (NOCK's own concern, not validated for uniqueness here -- two
 * buttons sharing an id is a real, valid "either one presses the same subscribers" design, not
 * an error). box_index is which boxes[] entry is this button's own physical, walkable-up-to
 * panel (its OWN box, distinct from whatever door(s) it controls). */
#define LEVEL_BOXES_MAX_BUTTONS 16
typedef struct {
    int box_index;
    int button_id;
} LevelButton;

/* door_tick_builtin_proximity -- S473 follow-up (found live, 2026-09-17, founder real-time: "i
   dont know why this never moved forward i kept asking for doors please make doors actually
   work"). Real root cause: a door with no script attached used to still emit a real script_url
   pointing at a nonexistent script id (id 0), a guaranteed 404 -- so a door placed without first
   writing+attaching a custom PARENA door_tick script silently did nothing at all, on every
   platform including the dedicated server, with zero visible feedback to the level author. This
   is the real, working default every door now gets automatically: opens once a player closes to
   within OPEN_DIST, closes once they're back out past CLOSE_DIST (a real hysteresis band between
   the two, CLOSE_DIST > OPEN_DIST, so a door standing exactly at the boundary doesn't flicker
   open/closed every tick) -- same real `(dist, state) -> new_state` contract door_tick_fn already
   uses server-side, so this slots in as a real fallback there with zero changes to DoorRuntime's
   own tick-invocation code, AND is dlfcn-free (pure static C, no dynamic loading at all) so it's
   the ONLY door behavior currently available in the lobby build (dlopen/dlfcn.h is POSIX-only --
   the lobby's own Windows-cross-compiled client has no dlopen equivalent wired up, a real,
   separate, not-yet-attempted lift, not something this pass silently promises). A door WITH a
   real, attached custom script keeps using that script's own real logic unchanged -- this is
   purely additive for the no-script case. */
#define LEVEL_BOXES_DOOR_BUILTIN_OPEN_DIST 8.0
#define LEVEL_BOXES_DOOR_BUILTIN_CLOSE_DIST 12.0
/* STORY_DOOR_OPEN_THRESHOLD -- the real, shared boolean interpretation of a door's own continuous
   state value (>= this means "open enough to disable collision"), used identically by both
   story_doors.h's dlopen'd-script path (server-only) and the lobby's own builtin-only path below
   -- moved here (was originally story_doors.h-only) once a real second, dlfcn-free consumer
   existed. A script MAY return intermediate values for a future opening/closing animation state;
   only this threshold's own collision toggle is acted on today. */
#define STORY_DOOR_OPEN_THRESHOLD 0.5
static inline double door_tick_builtin_proximity(double dist, double state) {
    if (dist <= LEVEL_BOXES_DOOR_BUILTIN_OPEN_DIST) return 1.0;
    if (dist >= LEVEL_BOXES_DOOR_BUILTIN_CLOSE_DIST) return 0.0;
    return state;
}

/* S461-01/S464 -- a real, author-placed waypoint/cover node, same JSON authoring convention as
   LevelDoor above (founder real-time: "we are going to need a waypoint system in the levels and
   maps northstar it" / "continue filling in the gaps in our level editor"). Mirrors
   packages/simulation/ai_nav.h's own AINavNode field-for-field (x/y/z, is_cover, cover_dir,
   neighbors), so loading one of these into an AINavGraph via ai_nav_add_node/ai_nav_link is a
   direct field copy, not a translation. neighbors are 0-based indices into THIS SAME nav_nodes
   array (matching how box_index above indexes into boxes[]), not a persisted node id -- the
   IDUNA export layer resolves each node's own real id-based neighbor references into array
   positions before this loader ever sees them (see IDUNA/internal/shankpit.navNodesForExport). */
#define LEVEL_BOXES_MAX_NAV_NODES 32
#define LEVEL_BOXES_MAX_NAV_NEIGHBORS 4

typedef struct {
    float x, y, z;
    int is_cover;
    float cover_dir_x, cover_dir_z;
    int neighbor_count;
    int neighbors[LEVEL_BOXES_MAX_NAV_NEIGHBORS];
} LevelNavNode;

/* S467 follow-up (STORY_SYSTEM_NORTHSTAR.md Phase 2's own "character" kind, founder real-time:
   "continue filling in the gaps in our level editor scriptable env characters etc") -- a real,
   author-placed story_ai NPC. `role` is the same integer AIRole value story_ai_spawn_enemy
   already takes (packages/simulation/story_ai.h) -- this loader has no dependency on story_ai.h
   itself (matching the same "flat data in, no simulation-layer type" boundary LevelDoor/
   LevelNavNode already hold themselves to), so an out-of-range role is validated by the CALLER
   (server_apply_custom_level), not here. Only ever meaningful in MODE_STORY/MODE_STORY_CAVE --
   see server_apply_custom_level's own real game-mode gate for why spawning here unconditionally
   would be unsafe in MODE_QUEUE (repeated per-round loads, no reset, real slot-exhaustion risk). */
typedef struct {
    int role;
    float x, y, z;
} LevelCharacter;

#define LEVEL_BOXES_MAX_CHARACTERS 16 /* matches STORY_AI_MAX, packages/simulation/story_ai.h */

/* S473, STORY_LEVEL_SEQUENCING_NORTHSTAR.md Phase 1 (founder real-time: "we need the loading
   points or whatever the opposite of the spawners is") -- a real, author-placed exit trigger
   volume. A player entering it (server-authoritative distance check, MODE_STORY only) transitions
   to this LEVEL's own real next_level_id below -- every exit volume in a level leads to the SAME
   next level (v0 is a chain, not a per-exit destination; see the NORTHSTAR doc for why a general
   graph is deliberately deferred). Mirrors LevelCharacter's own "no cross-reference" simplicity,
   plus a radius. */
typedef struct {
    float x, y, z;
    float radius;
} LevelExit;

#define LEVEL_BOXES_MAX_LEVEL_EXITS 8 /* matches IDUNA/internal/shankpit.MaxLevelExits exactly */

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
    LevelDoor doors[LEVEL_BOXES_MAX_DOORS]; /* Story System Phase 1 */
    int door_count;
    LevelButton buttons[LEVEL_BOXES_MAX_BUTTONS]; /* S485, REFLUX pub/sub */
    int button_count;
    LevelNavNode nav_nodes[LEVEL_BOXES_MAX_NAV_NODES]; /* S461-01/S464 */
    int nav_node_count;
    LevelCharacter characters[LEVEL_BOXES_MAX_CHARACTERS]; /* S467 */
    int character_count;
    LevelExit level_exits[LEVEL_BOXES_MAX_LEVEL_EXITS]; /* S473 */
    int level_exit_count;
    /* next_level_id (S473) -- 0 is the real "no next level" sentinel (matches this loader's own
       established "0/absent is a real, honest sentinel, not an error" convention elsewhere --
       IDUNA's own real primary keys start at 1, so 0 never collides with a real level id). */
    int next_level_id;
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

// level_boxes_parse_int_array scans a `[n, n, n]` bracket (open_bracket points at the `[`) for
// up to max real integers, same small-bounded-scanner discipline as the rest of this file --
// used for LevelNavNode's own "neighbors" field. Returns the count actually written (0 if
// open_bracket doesn't start with '[' or the array is empty).
static inline int level_boxes_parse_int_array(const char *open_bracket, const char *buf_end, int *out, int max) {
    if (*open_bracket != '[') return 0;
    const char *arr_end = level_boxes_find_array_end(open_bracket, buf_end);
    if (!arr_end) return 0;
    int count = 0;
    const char *p = open_bracket + 1;
    while (p < arr_end && count < max) {
        p = level_boxes_skip_ws(p);
        if (p >= arr_end) break;
        if (*p == ',') { p++; continue; }
        float f;
        if (!level_boxes_parse_number(p, &f)) { p++; continue; }
        out[count++] = (int)f;
        while (p < arr_end && *p != ',' ) p++;
    }
    return count;
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

    // next_level_id (S473) -- absent key is a real, honest "end of the story"/"not part of a
    // chain" state, not an error -- out->next_level_id stays 0.
    out->next_level_id = 0;
    const char *nli_val = level_boxes_find_key(buf, end, "next_level_id");
    if (nli_val) {
        float nli_f = 0;
        if (level_boxes_parse_number(nli_val, &nli_f)) out->next_level_id = (int)nli_f;
    }

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
                    float spec_f = 0, shin_f = 8, fric_f = 0.30f;
                    const char *spv = level_boxes_find_key(mobj_start, mobj_end, "specular");
                    if (spv) level_boxes_parse_number(spv, &spec_f);
                    const char *shv = level_boxes_find_key(mobj_start, mobj_end, "shininess");
                    if (shv) level_boxes_parse_number(shv, &shin_f);
                    const char *frv = level_boxes_find_key(mobj_start, mobj_end, "friction");
                    if (frv) level_boxes_parse_number(frv, &fric_f);
                    mat->specular = spec_f;
                    mat->shininess = shin_f;
                    mat->friction = fric_f;
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
        mat->friction = 0.30f;
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

    // Doors (Story System Phase 1) -- parsed the same real, small-scanner way as spawners above.
    // Absent "doors" key is a real, honest "no scriptable doors in this level" state, not an
    // error -- door_count stays 0.
    out->door_count = 0;
    const char *door_arr_key = level_boxes_find_key(buf, end, "doors");
    if (door_arr_key) {
        const char *door_arr = level_boxes_skip_ws(door_arr_key);
        if (*door_arr == '[') {
            const char *door_arr_end = level_boxes_find_array_end(door_arr, end);
            if (door_arr_end) {
                const char *dcursor = door_arr + 1;
                while (dcursor < door_arr_end && out->door_count < LEVEL_BOXES_MAX_DOORS) {
                    dcursor = level_boxes_skip_ws(dcursor);
                    if (dcursor >= door_arr_end) break;
                    if (*dcursor == ',') { dcursor++; continue; }
                    if (*dcursor != '{') { dcursor++; continue; }
                    const char *dobj_start = dcursor;
                    const char *dobj_end = strchr(dobj_start, '}');
                    if (!dobj_end || dobj_end > door_arr_end) break;

                    LevelDoor *door = &out->doors[out->door_count];
                    memset(door, 0, sizeof(*door));
                    door->subscribe_button_id = -1; /* default: normal auto-proximity door */
                    const char *v3;
                    float box_index_f = -1.0f;
                    if ((v3 = level_boxes_find_key(dobj_start, dobj_end, "box_index"))) level_boxes_parse_number(v3, &box_index_f);
                    door->box_index = (int)box_index_f;
                    if ((v3 = level_boxes_find_key(dobj_start, dobj_end, "subscribe_button_id"))) {
                        float sub_f = -1.0f;
                        if (level_boxes_parse_number(v3, &sub_f)) door->subscribe_button_id = (int)sub_f;
                    }
                    if ((v3 = level_boxes_find_key(dobj_start, dobj_end, "script_path"))) {
                        level_boxes_parse_string(v3, door->script_path, sizeof(door->script_path));
                    }
                    if ((v3 = level_boxes_find_key(dobj_start, dobj_end, "script_url"))) {
                        level_boxes_parse_string(v3, door->script_url, sizeof(door->script_url));
                    }
                    // REAL, FOUND, PRE-EXISTING BUG (2026-09-17, founder real-time: "i dont know
                    // why this never moved forward i kept asking for doors please make doors
                    // actually work"): this used to ALSO require a real script_path/script_url
                    // before counting the door as existing at all -- so an unscripted door was
                    // silently dropped HERE, at parse time, before door_tick_builtin_proximity's
                    // own fallback in story_doors_init ever got a chance to run. A door only
                    // needs a real box_index to be a real door; whether it has a script is a
                    // separate, later concern (story_doors_init's own real fallback).
                    if (door->box_index >= 0 && door->box_index < count) {
                        out->door_count++;
                    }
                    dcursor = dobj_end + 1;
                }
            }
        }
    }

    // Buttons (S485, REFLUX pub/sub) -- same real, small-scanner convention as doors above.
    // Absent "buttons" key is a real, honest "no buttons authored for this level" state, not an
    // error.
    out->button_count = 0;
    const char *btn_arr_key = level_boxes_find_key(buf, end, "buttons");
    if (btn_arr_key) {
        const char *btn_arr = level_boxes_skip_ws(btn_arr_key);
        if (*btn_arr == '[') {
            const char *btn_arr_end = level_boxes_find_array_end(btn_arr, end);
            if (btn_arr_end) {
                const char *bcursor = btn_arr + 1;
                while (bcursor < btn_arr_end && out->button_count < LEVEL_BOXES_MAX_BUTTONS) {
                    bcursor = level_boxes_skip_ws(bcursor);
                    if (bcursor >= btn_arr_end) break;
                    if (*bcursor == ',') { bcursor++; continue; }
                    if (*bcursor != '{') { bcursor++; continue; }
                    const char *bobj_start = bcursor;
                    const char *bobj_end = strchr(bobj_start, '}');
                    if (!bobj_end || bobj_end > btn_arr_end) break;

                    LevelButton *btn = &out->buttons[out->button_count];
                    memset(btn, 0, sizeof(*btn));
                    const char *v4;
                    float bbox_f = -1.0f, bid_f = 0.0f;
                    if ((v4 = level_boxes_find_key(bobj_start, bobj_end, "box_index"))) level_boxes_parse_number(v4, &bbox_f);
                    if ((v4 = level_boxes_find_key(bobj_start, bobj_end, "button_id"))) level_boxes_parse_number(v4, &bid_f);
                    btn->box_index = (int)bbox_f;
                    btn->button_id = (int)bid_f;
                    if (btn->box_index >= 0 && btn->box_index < count) {
                        out->button_count++;
                    }
                    bcursor = bobj_end + 1;
                }
            }
        }
    }

    // Nav nodes (S461-01/S464) -- same real, small-scanner convention as doors above. Absent
    // "nav_nodes" key is a real, honest "no waypoint graph authored for this level" state, not
    // an error -- nav_node_count stays 0 and ai_nav_find_path/ai_nav_find_cover degrade
    // gracefully against an empty graph (see ai_nav.h's own doc comments).
    out->nav_node_count = 0;
    const char *nn_arr_key = level_boxes_find_key(buf, end, "nav_nodes");
    if (nn_arr_key) {
        const char *nn_arr = level_boxes_skip_ws(nn_arr_key);
        if (*nn_arr == '[') {
            const char *nn_arr_end = level_boxes_find_array_end(nn_arr, end);
            if (nn_arr_end) {
                const char *ncursor = nn_arr + 1;
                while (ncursor < nn_arr_end && out->nav_node_count < LEVEL_BOXES_MAX_NAV_NODES) {
                    ncursor = level_boxes_skip_ws(ncursor);
                    if (ncursor >= nn_arr_end) break;
                    if (*ncursor == ',') { ncursor++; continue; }
                    if (*ncursor != '{') { ncursor++; continue; }
                    const char *nobj_start = ncursor;
                    const char *nobj_end = strchr(nobj_start, '}');
                    if (!nobj_end || nobj_end > nn_arr_end) break;

                    LevelNavNode *nn = &out->nav_nodes[out->nav_node_count];
                    memset(nn, 0, sizeof(*nn));
                    const char *v4;
                    if ((v4 = level_boxes_find_key(nobj_start, nobj_end, "x"))) level_boxes_parse_number(v4, &nn->x);
                    if ((v4 = level_boxes_find_key(nobj_start, nobj_end, "y"))) level_boxes_parse_number(v4, &nn->y);
                    if ((v4 = level_boxes_find_key(nobj_start, nobj_end, "z"))) level_boxes_parse_number(v4, &nn->z);
                    if ((v4 = level_boxes_find_key(nobj_start, nobj_end, "is_cover"))) level_boxes_parse_bool(v4, &nn->is_cover);
                    if ((v4 = level_boxes_find_key(nobj_start, nobj_end, "cover_dir_x"))) level_boxes_parse_number(v4, &nn->cover_dir_x);
                    if ((v4 = level_boxes_find_key(nobj_start, nobj_end, "cover_dir_z"))) level_boxes_parse_number(v4, &nn->cover_dir_z);
                    if ((v4 = level_boxes_find_key(nobj_start, nobj_end, "neighbors"))) {
                        const char *narr = level_boxes_skip_ws(v4);
                        nn->neighbor_count = level_boxes_parse_int_array(narr, nobj_end, nn->neighbors, LEVEL_BOXES_MAX_NAV_NEIGHBORS);
                    }
                    out->nav_node_count++;
                    ncursor = nobj_end + 1;
                }
            }
        }
    }

    // Characters (S467, STORY_SYSTEM_NORTHSTAR.md Phase 2) -- same real, small-scanner
    // convention as spawners above. Absent "characters" key is a real, honest "no authored
    // story_ai NPCs for this level" state, not an error -- character_count stays 0.
    out->character_count = 0;
    const char *ch_arr_key = level_boxes_find_key(buf, end, "characters");
    if (ch_arr_key) {
        const char *ch_arr = level_boxes_skip_ws(ch_arr_key);
        if (*ch_arr == '[') {
            const char *ch_arr_end = level_boxes_find_array_end(ch_arr, end);
            if (ch_arr_end) {
                const char *ccursor = ch_arr + 1;
                while (ccursor < ch_arr_end && out->character_count < LEVEL_BOXES_MAX_CHARACTERS) {
                    ccursor = level_boxes_skip_ws(ccursor);
                    if (ccursor >= ch_arr_end) break;
                    if (*ccursor == ',') { ccursor++; continue; }
                    if (*ccursor != '{') { ccursor++; continue; }
                    const char *cobj_start = ccursor;
                    const char *cobj_end = strchr(cobj_start, '}');
                    if (!cobj_end || cobj_end > ch_arr_end) break;

                    LevelCharacter *ch = &out->characters[out->character_count];
                    memset(ch, 0, sizeof(*ch));
                    const char *v5;
                    float role_f = 0.0f;
                    if ((v5 = level_boxes_find_key(cobj_start, cobj_end, "role"))) level_boxes_parse_number(v5, &role_f);
                    ch->role = (int)role_f;
                    if ((v5 = level_boxes_find_key(cobj_start, cobj_end, "x"))) level_boxes_parse_number(v5, &ch->x);
                    if ((v5 = level_boxes_find_key(cobj_start, cobj_end, "y"))) level_boxes_parse_number(v5, &ch->y);
                    if ((v5 = level_boxes_find_key(cobj_start, cobj_end, "z"))) level_boxes_parse_number(v5, &ch->z);
                    out->character_count++;
                    ccursor = cobj_end + 1;
                }
            }
        }
    }

    // Level exits (S473, STORY_LEVEL_SEQUENCING_NORTHSTAR.md Phase 1) -- same real, small-scanner
    // convention as characters above. Absent "level_exits" key is a real, honest "no authored
    // exit points for this level" state, not an error -- level_exit_count stays 0.
    out->level_exit_count = 0;
    const char *le_arr_key = level_boxes_find_key(buf, end, "level_exits");
    if (le_arr_key) {
        const char *le_arr = level_boxes_skip_ws(le_arr_key);
        if (*le_arr == '[') {
            const char *le_arr_end = level_boxes_find_array_end(le_arr, end);
            if (le_arr_end) {
                const char *lecursor = le_arr + 1;
                while (lecursor < le_arr_end && out->level_exit_count < LEVEL_BOXES_MAX_LEVEL_EXITS) {
                    lecursor = level_boxes_skip_ws(lecursor);
                    if (lecursor >= le_arr_end) break;
                    if (*lecursor == ',') { lecursor++; continue; }
                    if (*lecursor != '{') { lecursor++; continue; }
                    const char *leobj_start = lecursor;
                    const char *leobj_end = strchr(leobj_start, '}');
                    if (!leobj_end || leobj_end > le_arr_end) break;

                    LevelExit *lex = &out->level_exits[out->level_exit_count];
                    memset(lex, 0, sizeof(*lex));
                    const char *v6;
                    if ((v6 = level_boxes_find_key(leobj_start, leobj_end, "x"))) level_boxes_parse_number(v6, &lex->x);
                    if ((v6 = level_boxes_find_key(leobj_start, leobj_end, "y"))) level_boxes_parse_number(v6, &lex->y);
                    if ((v6 = level_boxes_find_key(leobj_start, leobj_end, "z"))) level_boxes_parse_number(v6, &lex->z);
                    if ((v6 = level_boxes_find_key(leobj_start, leobj_end, "radius"))) level_boxes_parse_number(v6, &lex->radius);
                    out->level_exit_count++;
                    lecursor = leobj_end + 1;
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
    // is_story_start (S473, STORY_LEVEL_SEQUENCING_NORTHSTAR.md Phase 1, founder real-time: "we
    // dont need the text cutscene in the beginning we just need to spawn into the first map that
    // the story is") -- exactly one level is flagged as the real, global MODE_STORY entry level
    // at a time (internal/shankpit.LevelSummary's own real is_story_start field), same shape
    // is_default_queue above already established.
    int is_story_start;
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
                out[count].is_story_start = 0;
                const char *ss_val = level_boxes_find_key(obj_start, obj_end, "is_story_start");
                if (ss_val) level_boxes_parse_bool(ss_val, &out[count].is_story_start);
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
