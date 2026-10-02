#ifndef EDIT_MAP_H
#define EDIT_MAP_H
/* edit_map.h -- EDIT MAP mode's pure edit logic (kanban #517/#530 EDIT-2).
 *
 * Edits never rewrite the source level JSON (that format carries doors/characters/widgets/etc.
 * a C round-trip would silently drop). They are an ordered OP LOG: add box / delete box / move
 * spawner. The log is applied to a loaded CustomLevelData, saved as var/maps/<name>.edits.json,
 * and (EDIT-3) streamed as events into an IDUNA edit session, where the admin-gated NOCK editor
 * does the real save. Header-only, no SDL/GL, unit-tested in apps/tests/test_edit_map.c. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "level_boxes.h"

#define EDIT_MAP_MAX_OPS 512

typedef enum { EDIT_OP_ADD_BOX = 1, EDIT_OP_DEL_BOX = 2, EDIT_OP_MOVE_SPAWNER = 3 } EditOpKind;

typedef struct {
    int kind;
    int index;                 /* DEL_BOX: box index at the time of the op; MOVE_SPAWNER: spawner index */
    float x, y, z;             /* ADD_BOX centre / MOVE_SPAWNER position */
    float w, h, d;             /* ADD_BOX extents */
    float yaw;                 /* MOVE_SPAWNER */
} EditOp;

typedef struct { EditOp ops[EDIT_MAP_MAX_OPS]; int n; } EditLog;

/* Apply one op to lvl. Returns 1 if it changed the level, 0 if rejected (full, out of range). */
static inline int edit_map_apply(CustomLevelData *lvl, const EditOp *op) {
    switch (op->kind) {
    case EDIT_OP_ADD_BOX: {
        if (lvl->count >= LEVEL_BOXES_MAX) return 0;
        LevelBox *b = &lvl->boxes[lvl->count++];
        memset(b, 0, sizeof(*b));
        b->x = op->x; b->y = op->y; b->z = op->z;
        b->w = op->w; b->h = op->h; b->d = op->d;
        b->r = 0.6f; b->g = 0.6f; b->b = 0.65f; b->friction = 0.8f;
        return 1;
    }
    case EDIT_OP_DEL_BOX:
        if (op->index < 0 || op->index >= lvl->count) return 0;
        memmove(&lvl->boxes[op->index], &lvl->boxes[op->index + 1],
                sizeof(LevelBox) * (size_t)(lvl->count - op->index - 1));
        lvl->count--;
        return 1;
    case EDIT_OP_MOVE_SPAWNER:
        if (op->index < 0 || op->index >= lvl->spawner_count) return 0;
        lvl->spawners[op->index].x = op->x; lvl->spawners[op->index].y = op->y;
        lvl->spawners[op->index].z = op->z; lvl->spawners[op->index].yaw = op->yaw;
        return 1;
    }
    return 0;
}

/* Apply + record. The log only keeps ops that actually changed the level. */
static inline int edit_map_do(EditLog *log, CustomLevelData *lvl, const EditOp *op) {
    if (log->n >= EDIT_MAP_MAX_OPS) return 0;
    if (!edit_map_apply(lvl, op)) return 0;
    log->ops[log->n++] = *op;
    return 1;
}

/* Index of the box whose AABB contains (px,py,pz) grown by eps, or -1. Used to resolve a
 * crosshair hit point (nudged slightly into the surface) back to a box. Last match wins so a
 * box added later (drawn on top) is picked first. */
static inline int edit_map_box_at(const CustomLevelData *lvl, float px, float py, float pz, float eps) {
    for (int i = lvl->count - 1; i >= 0; i--) {
        const LevelBox *b = &lvl->boxes[i];
        if (fabsf(px - b->x) <= b->w * 0.5f + eps && fabsf(py - b->y) <= b->h * 0.5f + eps &&
            fabsf(pz - b->z) <= b->d * 0.5f + eps) return i;
    }
    return -1;
}

static inline int edit_map_save(const EditLog *log, const char *level_name, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fprintf(f, "{\"version\":1,\"level\":\"%s\",\"ops\":[", level_name);
    for (int i = 0; i < log->n; i++) {
        const EditOp *o = &log->ops[i];
        fprintf(f, "%s{\"kind\":%d,\"index\":%d,\"x\":%.4f,\"y\":%.4f,\"z\":%.4f,\"w\":%.4f,\"h\":%.4f,\"d\":%.4f,\"yaw\":%.4f}",
                i ? "," : "", o->kind, o->index, o->x, o->y, o->z, o->w, o->h, o->d, o->yaw);
    }
    fprintf(f, "]}\n");
    return fclose(f) == 0;
}

/* Replay a saved edits file onto lvl. Returns ops applied, -1 if the file is missing. */
static inline int edit_map_load_apply(EditLog *log, CustomLevelData *lvl, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    static char buf[1 << 17];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    int applied = 0;
    const char *p = strstr(buf, "\"ops\"");
    while (p && (p = strchr(p, '{')) && p[1] == '"' && strncmp(p + 2, "kind", 4) == 0) {
        EditOp o; memset(&o, 0, sizeof o);
        if (sscanf(p, "{\"kind\":%d,\"index\":%d,\"x\":%f,\"y\":%f,\"z\":%f,\"w\":%f,\"h\":%f,\"d\":%f,\"yaw\":%f}",
                   &o.kind, &o.index, &o.x, &o.y, &o.z, &o.w, &o.h, &o.d, &o.yaw) != 9) break;
        if (edit_map_do(log, lvl, &o)) applied++;
        p = strchr(p, '}');
        if (!p) break;
        p++;
    }
    return applied;
}
#endif
