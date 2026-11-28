#ifndef SHANKPIT_WMAP_LEVEL_H
#define SHANKPIT_WMAP_LEVEL_H
/* wmap_level.h -- the SHANKPIT side of the WORLDMAP contract (PARENA/stdlib/worldmap/CONTRACT.md).
 *
 * Reads a `.wmap` (a superset of shankpit-460's .map: box / spawn / poi, plus zone, building, npc, exit,
 * portal, crystal, bound) into the engine's own CustomLevelData, so every `--level <file>` path, the server
 * and the lobby client load it with no other change: level_boxes_load_from_file hands a *.wmap here.
 * GoblinFoxDragon is the server and owns the zones; this is how the client gets the same geometry, e.g. the
 * old GFD town (GoblinFoxDragon/data/maps/city.wmap, ported from its physics.h map_geo_city_base).
 *
 * Included by level_boxes.h AFTER CustomLevelData is defined; do not include it on its own.
 * One zone per file is loaded: the first `zone` section (a bare legacy .map is one implicit zone).
 * Telecrystals land in g_wmap_crystals (not in CustomLevelData: they are GFD's registry rows -- the server
 * decides whether a jump is honoured; the client only draws the ring and predicts the cast with the shared
 * wm_rules.prn timings). Box 0 is the floor slab, exactly as in the engine; boxes are coloured by role.
 */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WMAP_MAX_CRYSTALS 16
#define WMAP_PROMPT_LEN 48
typedef struct {
    char id[64];
    float x, z, r;
    int dest_zone;
    float dx, dz, yaw;
    int cost, total_ms, commit_ms;
    char prompt[WMAP_PROMPT_LEN];
} WmapCrystal;

static WmapCrystal g_wmap_crystals[WMAP_MAX_CRYSTALS];
static int g_wmap_crystal_count = 0;
static int g_wmap_zone_id = 0;
static float g_wmap_bound = 420.0f;

/* box colour by role: the floor and the perimeter shell are dark, thin boxes are road, the rest is building. */
static inline void wmap_box_colour(const LevelBox *b, int index, float *r, float *g, float *bl) {
    if (index == 0) { *r = 0.20f; *g = 0.22f; *bl = 0.20f; return; }       /* ground slab */
    if (b->h >= 50.0f) { *r = 0.16f; *g = 0.16f; *bl = 0.19f; return; }    /* perimeter shell */
    if (b->h <= 2.0f) { *r = 0.30f; *g = 0.30f; *bl = 0.32f; return; }     /* road / plaza */
    float t = (float)((index * 37) % 7) / 7.0f;                              /* buildings: warm, varied */
    *r = 0.55f + 0.20f * t; *g = 0.45f + 0.12f * t; *bl = 0.34f + 0.10f * t;
}

/* Parses buf (NUL-terminated .wmap text) into *out. Returns 1 on success. */
static inline int wmap_level_parse(const char *buf, CustomLevelData *out) {
    memset(out, 0, sizeof(*out));
    g_wmap_crystal_count = 0; g_wmap_zone_id = 0; g_wmap_bound = 420.0f;
    snprintf(out->name, sizeof(out->name), "wmap");
    LevelBoxMaterial *mat = &out->materials[0];
    snprintf(mat->name, sizeof(mat->name), "%s", LEVEL_BOXES_DEFAULT_MATERIAL);
    snprintf(mat->shader_name, sizeof(mat->shader_name), "standard");
    mat->specular = 0.04f; mat->shininess = 6.0f; mat->friction = 0.30f;
    out->material_count = 1;
    out->ground_plane_enabled = 0;   /* the floor slab (box 0) is the ground */
    out->ground_plane_squares = 1;

    int zones_seen = 0, in_zone = 1;       /* in_zone: lines count (implicit zone, or the first declared one) */
    const char *p = buf;
    while (*p) {
        char line[512]; size_t n = 0;
        while (*p && *p != '\n' && n < sizeof(line) - 1) line[n++] = *p++;
        while (*p && *p != '\n') p++;       /* over-long line: drop the tail */
        if (*p == '\n') p++;
        line[n] = '\0';
        char *hash = strchr(line, '#'); if (hash) *hash = '\0';
        char kw[24]; int off = 0;
        if (sscanf(line, "%23s%n", kw, &off) != 1) continue;
        const char *a = line + off;
        float v[12];
        if (!strcmp(kw, "worldmap")) {
            int ver = 0; if (sscanf(a, "%d", &ver) != 1 || ver != 1) return 0;
        } else if (!strcmp(kw, "zone")) {
            int id = 0; char kind[16] = "", name[64] = "";
            if (sscanf(a, "%d %15s %63s", &id, kind, name) < 2) return 0;
            zones_seen++;
            in_zone = (zones_seen == 1);
            if (in_zone) { g_wmap_zone_id = id; snprintf(out->name, sizeof(out->name), "%s", name); }
        } else if (!in_zone) {
            continue;                         /* entries of a later zone: not this file's level */
        } else if (!strcmp(kw, "bound")) {
            if (sscanf(a, "%f", &v[0]) == 1) g_wmap_bound = v[0];
        } else if (!strcmp(kw, "box")) {
            if (sscanf(a, "%f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return 0;
            if (out->count >= LEVEL_BOXES_MAX) return 0;
            LevelBox *b = &out->boxes[out->count];
            b->x = v[0]; b->y = v[1]; b->z = v[2]; b->w = v[3]; b->h = v[4]; b->d = v[5];
            b->friction = 0.30f; b->material_idx = 0;
            wmap_box_colour(b, out->count, &b->r, &b->g, &b->b);
            out->count++;
        } else if (!strcmp(kw, "spawn")) {
            if (sscanf(a, "%f %f %f", &v[0], &v[1], &v[2]) != 3) return 0;
            if (out->spawner_count < LEVEL_BOXES_MAX_SPAWNERS) {
                LevelSpawner *s = &out->spawners[out->spawner_count];
                s->x = v[0]; s->y = v[1]; s->z = v[2]; s->yaw = 0; s->team = LEVEL_BOXES_SPAWNER_TEAM_FFA;
                s->id = ++out->spawner_count;
            }
        } else if (!strcmp(kw, "crystal")) {
            WmapCrystal c; memset(&c, 0, sizeof c);
            int used = 0;
            if (sscanf(a, "%63s %f %f %f %d %f %f %f %d %d %d %n", c.id, &c.x, &c.z, &c.r, &c.dest_zone, &c.dx, &c.dz,
                       &c.yaw, &c.cost, &c.total_ms, &c.commit_ms, &used) < 11) return 0;
            snprintf(c.prompt, sizeof c.prompt, "%s", a + used);
            size_t pl = strlen(c.prompt);
            while (pl && isspace((unsigned char)c.prompt[pl - 1])) c.prompt[--pl] = '\0';
            if (g_wmap_crystal_count < WMAP_MAX_CRYSTALS) g_wmap_crystals[g_wmap_crystal_count++] = c;
            /* ...and the same crystal as a TELECRYSTAL widget (machine_type 9), so the widget system -- not a special case -- runs it. */
            if (out->edu_widget_count < LEVEL_BOXES_MAX_EDU_WIDGETS) {
                LevelEduWidget *w = &out->edu_widgets[out->edu_widget_count++];
                memset(w, 0, sizeof *w);
                snprintf(w->entity_id, sizeof w->entity_id, "%s", c.id[0] ? c.id + (strncmp(c.id, "TELECRYSTAL_ID_", 15) ? 0 : 15) : "crystal");
                w->machine_type = 9; w->memory_address = -1; w->x = c.x; w->y = 0; w->z = c.z; w->radius = c.r;
                w->box_index = w->input_pin = w->gate_pin = -1;
                w->dest_zone = c.dest_zone; w->cost = c.cost; w->cast_total_ms = c.total_ms; w->cast_commit_ms = c.commit_ms;
                snprintf(w->prompt, sizeof w->prompt, "%s", c.prompt);
            }
        }
        /* poi / building / npc / exit / portal: GFD's server data; the client draws them from boxes. */
    }
    if (out->count < 1) return 0;
    return 1;
}

static inline int wmap_level_load(const char *path, CustomLevelData *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 1 << 20) { fclose(f); return 0; }
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) { fclose(f); return 0; }
    size_t got = fread(buf, 1, (size_t)size, f); fclose(f); buf[got] = '\0';
    static CustomLevelData tmp;   /* CustomLevelData is large; keep it off the stack */
    int ok = wmap_level_parse(buf, &tmp);
    free(buf);
    if (!ok) return 0;
    *out = tmp;
    return 1;
}

static inline int wmap_path_is_wmap(const char *path) {
    size_t n = strlen(path);
    return n > 5 && !strcmp(path + n - 5, ".wmap");
}
#endif
