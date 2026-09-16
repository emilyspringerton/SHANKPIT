#ifndef SHANKPIT_STORY_DOORS_H
#define SHANKPIT_STORY_DOORS_H

// story_doors.h -- Story System Phase 1 (docs/STORY_SYSTEM_NORTHSTAR.md Part 2), the first real
// scriptable map object kind, end to end: a door's own behavior is a real, compiled PARENA
// script (see that doc's "door-tick" contract), dlopen'd at level load and evaluated once per
// server tick per placed door instance, exactly the way PAPERCRAFT's own level_mod.prn precedent
// links compiled PARENA into a hand-written C host -- the one real difference being dlopen
// (runtime-loadable, per this repo's own existing apps/dynmod_poc proof of concept) instead of
// build-time static linking, since a door's script is per-LEVEL content, not a repo-wide mod.
//
// Real, current v0 scope, named honestly: script_path is a local filesystem path to an
// already-compiled .so (compiled by hand via `parena build ... -o door_tick.c` + `gcc -shared
// -fPIC`, same two real steps IDUNA's own internal/nock/procgen.go already automates for the
// Java-target texture pipeline). A NOCK authoring UI + IDUNA-hosted script compile/storage
// (mirroring procgen.go's own real pipeline, targeting C instead of Java) is real, scoped,
// deferred future work -- not built in this pass.

#include <dlfcn.h>
#include <string.h>

#include "level_boxes.h"

#define STORY_DOOR_OPEN_THRESHOLD 0.5

// door_tick's own real, fixed contract (docs/STORY_SYSTEM_NORTHSTAR.md Part 2):
//   (defn door-tick [(dist-to-player : F64) (state : F64)] : F64 ...)
// emitted by PARENA's C target as `double door_tick(double, double)` -- checked directly against
// a real compiled example, not assumed.
typedef double (*door_tick_fn)(double, double);

typedef struct {
    int box_index;
    void *dl_handle;
    door_tick_fn tick;
    double state; // 0.0 = closed, 1.0 = open (a script MAY return intermediate values for a
                  // future opening/closing animation state; this v0 pass only acts on the
                  // >= STORY_DOOR_OPEN_THRESHOLD collision toggle, per phys_set_custom_level_box_y)
} DoorRuntime;

#define STORY_DOORS_MAX LEVEL_BOXES_MAX_DOORS
static DoorRuntime g_story_doors[STORY_DOORS_MAX];
static int g_story_door_count = 0;

// story_doors_shutdown -- real cleanup, called before loading a new level's own doors (or at
// process exit) so a stale dlopen handle from a PREVIOUS level's door script is never left
// resident once that level is no longer active.
static inline void story_doors_shutdown(void) {
    for (int i = 0; i < g_story_door_count; i++) {
        if (g_story_doors[i].dl_handle) dlclose(g_story_doors[i].dl_handle);
    }
    memset(g_story_doors, 0, sizeof(g_story_doors));
    g_story_door_count = 0;
}

// story_doors_init -- real dlopen/dlsym per door, called once right after
// server_apply_custom_level's own phys_set_custom_level* calls (so g_custom_level_box_authored_y
// is already populated). A door whose script fails to load or resolve is skipped with a real,
// visible stderr line -- never a silent no-op and never a crash (an author's own bad script
// mustn't take the whole server down).
static inline void story_doors_init(const CustomLevelData *lvl) {
    story_doors_shutdown();
    for (int i = 0; i < lvl->door_count && g_story_door_count < STORY_DOORS_MAX; i++) {
        const LevelDoor *ld = &lvl->doors[i];
        void *h = dlopen(ld->script_path, RTLD_NOW);
        if (!h) {
            fprintf(stderr, "[story-doors] dlopen(%s) failed: %s\n", ld->script_path, dlerror());
            continue;
        }
        dlerror(); // clear any pending error before dlsym, matching dlsym's own documented usage
        door_tick_fn fn = (door_tick_fn)dlsym(h, "door_tick");
        const char *err = dlerror();
        if (err || !fn) {
            fprintf(stderr, "[story-doors] dlsym(door_tick) in %s failed: %s\n", ld->script_path, err ? err : "symbol not found");
            dlclose(h);
            continue;
        }
        DoorRuntime *dr = &g_story_doors[g_story_door_count++];
        dr->box_index = ld->box_index;
        dr->dl_handle = h;
        dr->tick = fn;
        dr->state = 0.0; // real, honest default: every door starts CLOSED
        printf("[story-doors] loaded door script %s for box_index=%d\n", ld->script_path, ld->box_index);
    }
}

// story_doors_tick -- real per-tick evaluation, called once per server tick (main.c's own main
// loop, after update_projectiles, before the snapshot broadcast -- so a door's own new state is
// already reflected in the tick's outgoing snapshot's own collision-relevant geometry, even
// though door state itself isn't yet part of the wire protocol -- see the NORTHSTAR doc's own
// "what this does not cover" for that real, separate, deferred piece). dist-to-player is the
// real minimum distance from any active player to the door's own authored box position.
static inline void story_doors_tick(const PlayerState *players, int max_clients) {
    for (int i = 0; i < g_story_door_count; i++) {
        DoorRuntime *dr = &g_story_doors[i];
        float bx, by, bz;
        if (!phys_custom_level_box_pos(dr->box_index, &bx, &by, &bz)) continue;

        double min_dist = 1e9;
        for (int pi = 0; pi < max_clients; pi++) {
            const PlayerState *p = &players[pi];
            if (!p->active || p->state == STATE_DEAD) continue;
            double dx = p->x - bx, dy = p->y - by, dz = p->z - bz;
            double d = sqrt(dx * dx + dy * dy + dz * dz);
            if (d < min_dist) min_dist = d;
        }

        int was_open = dr->state >= STORY_DOOR_OPEN_THRESHOLD;
        dr->state = dr->tick(min_dist, dr->state);
        int is_open = dr->state >= STORY_DOOR_OPEN_THRESHOLD;
        if (is_open != was_open) {
            phys_set_custom_level_box_y(dr->box_index, is_open);
        }
    }
}

#endif // SHANKPIT_STORY_DOORS_H
