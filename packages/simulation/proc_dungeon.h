/* proc_dungeon.h -- a deterministic procedural dungeon, emitted as a real SHANKPIT level.
 *
 * Founder: "GFD server ... like diablo where we can travel to a map and do it as a dungeon it can be
 * procedurally generated ... the first person to enter it it generates and then everyone else enters the
 * same one". So this is not an instanced generator: one seed produces one layout, the caller keeps that
 * seed for as long as the zone lives (packages/simulation/zone_registry.h), and every entrant rebuilds
 * the identical level from it.
 *
 * DETERMINISM, and why it is built the way it is: every number comes from integer arithmetic in
 * PARENA/stdlib/shankpit/zone_rules.prn plus a plain LCG seed, and every coordinate is an add or a
 * multiply of small integers. No rand(), no float trig, no time. That is what lets the test save two
 * levels from the same seed and compare them byte for byte -- and what stops two co-op clients (or a
 * server and a client) from walking different dungeons.
 *
 * SHAPE: a hub with 2..4 cardinal rooms joined by corridors, all axis-aligned. Diagonal corridors are the
 * one case that needs corner geometry, and a four-way hub already reads as a dungeon crossing -- so the
 * layout stays something a person can draw on paper, and every wall is a real OBB the engine already
 * collides with.
 *
 * The output is a plain CustomLevelData, which means:
 *   - it goes through the same level_boxes_apply_to_physics as any NOCK level,
 *   - it saves with level_boxes_save_to_file (so a zone can be a file, and a co-op friend can be told its
 *     path or its seed),
 *   - it inherits destructible brick, lighting and the widget circuit for free, because it authors the
 *     same schema rather than a parallel one.
 *
 * The gate is authored as a REAL circuit, not a scripted special case: lever (6) -> relay (7) ->
 * machine (1) with that machine pointing at the gate wall's own box index. The relay's 45-tick delay is
 * why the gate does not slam the moment you flip the lever.
 */
#ifndef SHANKPIT_PROC_DUNGEON_H
#define SHANKPIT_PROC_DUNGEON_H

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../world/level_boxes.h"
#include "zone_rules.h"

#define PROC_DUNGEON_MAX_ROOMS 8

typedef struct {
    int seed;
    int rooms;
    int difficulty;
    int gate_box;          /* index in lvl->boxes of the gate the circuit opens */
    int spawner_count;
    float center_x[PROC_DUNGEON_MAX_ROOMS];
    float center_z[PROC_DUNGEON_MAX_ROOMS];
    float width[PROC_DUNGEON_MAX_ROOMS];
    float depth[PROC_DUNGEON_MAX_ROOMS];
} ProcDungeonInfo;

/* Material table: index order matches zone-material-for's own numbering (0 concrete, 1 metal, 2 brick).
 * Friction is the live ground friction physics.h resolves per material, so a metal floor is slicker than
 * brick without a single line in the physics code. */
static inline void proc_dungeon_materials(CustomLevelData *lvl) {
    static const char *const names[3] = { "concrete", "metal", "brick" };
    static const float spec[3] = { 0.10f, 0.55f, 0.06f };
    static const float shin[3] = { 18.0f, 90.0f, 8.0f };
    static const float fric[3] = { 0.34f, 0.26f, 0.30f };
    static const float dens[3] = { 2.4f, 3.1f, 1.9f };
    lvl->material_count = 3;
    for (int i = 0; i < 3; i++) {
        LevelBoxMaterial *m = &lvl->materials[i];
        memset(m, 0, sizeof *m);
        snprintf(m->name, sizeof m->name, "%s", names[i]);
        snprintf(m->shader_name, sizeof m->shader_name, "standard");
        m->specular = spec[i];
        m->shininess = shin[i];
        m->friction = fric[i];
        m->density = dens[i];
    }
}

static inline int proc_dungeon_add_box(CustomLevelData *lvl, float cx, float cz, float w, float h, float d,
                                       int mat) {
    LevelBox *b;
    if (!lvl || lvl->count >= LEVEL_BOXES_MAX) return -1;
    if (w <= 0.0f || d <= 0.0f || h <= 0.0f) return -1;
    b = &lvl->boxes[lvl->count];
    memset(b, 0, sizeof *b);
    b->x = cx;
    b->y = h * 0.5f;   /* LevelBox.y is the CENTRE, so a wall of height h stands on the floor */
    b->z = cz;
    b->w = w;
    b->h = h;
    b->d = d;
    b->material_idx = (mat >= 0 && mat < lvl->material_count) ? mat : 0;
    b->friction = lvl->materials[b->material_idx].friction;
    if (b->material_idx == 1) { b->r = 0.34f; b->g = 0.40f; b->b = 0.46f; }
    else if (b->material_idx == 2) { b->r = 0.58f; b->g = 0.31f; b->b = 0.26f; }
    else { b->r = 0.46f; b->g = 0.46f; b->b = 0.50f; }
    return lvl->count++;
}

/* A wall running along X (constant Z), and one running along Z (constant X). */
static inline void proc_dungeon_wall_x(CustomLevelData *lvl, float z, float x0, float x1, float t, float h,
                                       int mat) {
    if (x1 - x0 < 0.01f) return;
    proc_dungeon_add_box(lvl, (x0 + x1) * 0.5f, z, x1 - x0, h, t, mat);
}
static inline void proc_dungeon_wall_z(CustomLevelData *lvl, float x, float z0, float z1, float t, float h,
                                       int mat) {
    if (z1 - z0 < 0.01f) return;
    proc_dungeon_add_box(lvl, x, (z0 + z1) * 0.5f, t, h, z1 - z0, mat);
}

/* One room. side: 0 north (-z), 1 south (+z), 2 west (-x), 3 east (+x), -1 = sealed. The doorway is a
 * real gap of door_w centred on that wall, because a wall with a hole in it is two walls and that is
 * exactly how the engine already builds one. */
static inline void proc_dungeon_room(CustomLevelData *lvl, float cx, float cz, float w, float d, float h,
                                     float t, int mat, int side, float door_w) {
    float hw = w * 0.5f, hd = d * 0.5f;
    float g = door_w * 0.5f;

    /* north / south */
    if (side == 0) {
        proc_dungeon_wall_x(lvl, cz - hd, cx - hw, cx - g, t, h, mat);
        proc_dungeon_wall_x(lvl, cz - hd, cx + g, cx + hw, t, h, mat);
    } else {
        proc_dungeon_wall_x(lvl, cz - hd, cx - hw, cx + hw, t, h, mat);
    }
    if (side == 1) {
        proc_dungeon_wall_x(lvl, cz + hd, cx - hw, cx - g, t, h, mat);
        proc_dungeon_wall_x(lvl, cz + hd, cx + g, cx + hw, t, h, mat);
    } else {
        proc_dungeon_wall_x(lvl, cz + hd, cx - hw, cx + hw, t, h, mat);
    }
    /* west / east */
    if (side == 2) {
        proc_dungeon_wall_z(lvl, cx - hw, cz - hd, cz - g, t, h, mat);
        proc_dungeon_wall_z(lvl, cx - hw, cz + g, cz + hd, t, h, mat);
    } else {
        proc_dungeon_wall_z(lvl, cx - hw, cz - hd, cz + hd, t, h, mat);
    }
    if (side == 3) {
        proc_dungeon_wall_z(lvl, cx + hw, cz - hd, cz - g, t, h, mat);
        proc_dungeon_wall_z(lvl, cx + hw, cz + g, cz + hd, t, h, mat);
    } else {
        proc_dungeon_wall_z(lvl, cx + hw, cz - hd, cz + hd, t, h, mat);
    }
}

/* The hub has a doorway on every side that has a room, so the crossing is walkable in all directions. */
static inline int proc_dungeon_side_has_room(const ProcDungeonInfo *info, int side) {
    for (int i = 1; i < info->rooms; i++) {
        int dir = (i - 1) % 4;
        if (dir == side) return 1;
    }
    return 0;
}

/* Builds the layout into *out. Returns the box count (0 only if the rules produced nothing usable). */
static inline int proc_dungeon_generate(CustomLevelData *out, const char *zone_id, unsigned seed) {
    static ProcDungeonInfo info;
    CustomLevelData *lvl = out;
    float h, t, door_w, ring;
    int mat_hub, gate_side = 0;

    if (!lvl) return 0;
    memset(lvl, 0, sizeof *lvl);
    memset(&info, 0, sizeof info);
    info.seed = (int)(seed & 0x7FFFFFFFu);
    info.rooms = g_zone_rules.room_count(info.seed);
    if (info.rooms < 2) info.rooms = 2;
    if (info.rooms > PROC_DUNGEON_MAX_ROOMS) info.rooms = PROC_DUNGEON_MAX_ROOMS;
    info.difficulty = g_zone_rules.difficulty(info.seed);
    h = (float)g_zone_rules.wall_height(info.seed);
    t = (float)g_zone_rules.wall_thickness();
    door_w = (float)g_zone_rules.door_width();
    ring = (float)g_zone_rules.ring_distance();

    snprintf(lvl->name, sizeof lvl->name, "PROC %s %08x", (zone_id && zone_id[0]) ? zone_id : "zone", seed);
    proc_dungeon_materials(lvl);
    lvl->ground_plane_enabled = 1;
    lvl->ground_plane_squares = 96;
    lvl->enclosed = 1;
    lvl->width = (ring + 40.0f) * 2.0f;
    lvl->height = h;
    lvl->depth = lvl->width;

    /* hub */
    {
        float w = (float)g_zone_rules.room_width(info.seed, 0);
        float d = (float)g_zone_rules.room_depth(info.seed, 0);
        int side;
        mat_hub = g_zone_rules.material_for(info.seed, 0);
        info.center_x[0] = 0.0f;
        info.center_z[0] = 0.0f;
        info.width[0] = w;
        info.depth[0] = d;
        /* Build the hub by hand so each side can carry a doorway; the generic room builder takes one. */
        for (side = 0; side < 4; side++) {
            int has = proc_dungeon_side_has_room(&info, side);
            proc_dungeon_room(lvl, 0.0f, 0.0f, w, d, h, t, mat_hub, has ? side : -1, door_w);
        }
    }

    /* rooms + corridors */
    for (int i = 1; i < info.rooms; i++) {
        int dir = (i - 1) % 4;
        float w = (float)g_zone_rules.room_width(info.seed, i);
        float d = (float)g_zone_rules.room_depth(info.seed, i);
        int mat = g_zone_rules.material_for(info.seed, i);
        float cx = (float)g_zone_rules.ring_dir_x(dir) * ring;
        float cz = (float)g_zone_rules.ring_dir_z(dir) * ring;
        float hw_hub = info.width[0] * 0.5f, hd_hub = info.depth[0] * 0.5f;
        float hw = w * 0.5f, hd = d * 0.5f;
        int doorway_side = (dir + 2) % 4;   /* the side facing back to the hub */

        info.center_x[i] = cx;
        info.center_z[i] = cz;
        info.width[i] = w;
        info.depth[i] = d;
        proc_dungeon_room(lvl, cx, cz, w, d, h, t, mat, doorway_side, door_w);

        /* corridor: two parallel walls, inner width == the doorway it joins */
        if (dir == 0 || dir == 2) {
            float x0 = dir == 0 ? hw_hub : cx + hw;
            float x1 = dir == 0 ? cx - hw : -hw_hub;
            proc_dungeon_wall_x(lvl, (door_w + t) * 0.5f, x0, x1, t, h, mat);
            proc_dungeon_wall_x(lvl, -(door_w + t) * 0.5f, x0, x1, t, h, mat);
        } else {
            float z0 = dir == 1 ? hd_hub : cz + hd;
            float z1 = dir == 1 ? cz - hd : -hd_hub;
            proc_dungeon_wall_z(lvl, (door_w + t) * 0.5f, z0, z1, t, h, mat);
            proc_dungeon_wall_z(lvl, -(door_w + t) * 0.5f, z0, z1, t, h, mat);
        }
    }

    /* The gate: a real wall panel across the hub's north doorway, wired to a lever through a relay. It is
       added last so its box index is stable and trivially inspectable in a saved level. */
    gate_side = 0;
    (void)gate_side;
    {
        float hd_hub = info.depth[0] * 0.5f;
        int mat = g_zone_rules.material_for(info.seed, 7);
        info.gate_box = proc_dungeon_add_box(lvl, 0.0f, -hd_hub + t * 0.5f, door_w, h, t, mat);
    }

    /* spawners: one at the hub, one per room, FFA */
    {
        int want = g_zone_rules.spawn_count(info.seed);
        int sp = 0;
        for (int i = 0; i < info.rooms && sp < want && lvl->spawner_count < LEVEL_BOXES_MAX_SPAWNERS; i++, sp++) {
            LevelSpawner *ps = &lvl->spawners[lvl->spawner_count];
            memset(ps, 0, sizeof *ps);
            ps->x = info.center_x[i];
            ps->y = 0.0f;
            ps->z = info.center_z[i];
            ps->yaw = (float)(i * 90);
            ps->team = LEVEL_BOXES_SPAWNER_TEAM_FFA;
            ps->id = lvl->spawner_count;
            lvl->spawner_count++;
            info.spawner_count++;
        }
    }

    /* The gate circuit. Pins point at LOWER indices (the circuit's own rule), and the machine points at
       the gate box, so the door is a widget of widgets rather than a bespoke door type. */
    if (info.gate_box >= 0) {
        struct { const char *id; int type; int reg; float x, z; int input; int box; int delay; } w[3] = {
            { "proc_gate_lever", 6, 40, 0.0f, 6.0f, -1, -1, 0 },
            { "proc_gate_relay", 7, 41, 0.0f, 6.0f, 0, -1, 45 },
            { "proc_gate_motor", 1, 42, 0.0f, 0.0f, 1, -1, 0 },
        };
        for (int i = 0; i < 3 && lvl->edu_widget_count < LEVEL_BOXES_MAX_EDU_WIDGETS; i++) {
            LevelEduWidget *ew = &lvl->edu_widgets[lvl->edu_widget_count];
            memset(ew, 0, sizeof *ew);
            snprintf(ew->entity_id, sizeof ew->entity_id, "%s", w[i].id);
            ew->machine_type = w[i].type;
            ew->initial_state = 0;
            ew->memory_address = w[i].reg;
            ew->x = w[i].x;
            ew->y = 0.0f;
            ew->z = w[i].z;
            ew->radius = 3.0f;
            ew->box_index = -1;
            ew->input_pin = w[i].input;
            ew->gate_pin = -1;
            ew->delay_ticks = w[i].delay;
            lvl->edu_widget_count++;
        }
        /* the machine drives the gate box (index is valid: the box was just added) */
        lvl->edu_widgets[lvl->edu_widget_count - 1].box_index = info.gate_box;
    }

    return lvl->count;
}

/* Saves the generated level as the real JSON a NOCK level uses. Returns 1 on success. */
static inline int proc_dungeon_save(const char *path, const CustomLevelData *lvl) {
    if (!path || !lvl) return 0;
    return level_boxes_save_to_file(path, lvl);
}

#endif /* SHANKPIT_PROC_DUNGEON_H */
