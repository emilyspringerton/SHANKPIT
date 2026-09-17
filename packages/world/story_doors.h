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
// S459-82: a door's script now comes from either script_path (a local filesystem path -- real,
// still-supported dev/testing fallback) or script_url (a real, downloadable URL, e.g. IDUNA's
// own NOCK door-script repository at GET /api/v1/nock-door-scripts/:id/download --
// IDUNA/internal/nock/door_script_compile.go closes the original "via the nock tools" gap: a map
// designer writes PARENA in NOCK, IDUNA compiles it server-side, this file downloads and caches
// the result once at level load). Either way the file that actually gets dlopen'd is always a
// real local path -- script_url just gets fetched into STORY_DOOR_CACHE_DIR first.

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "level_boxes.h"
#include "../reflux/reflux_runtime.h"

// story_doors_cache_path -- S459-82: where a script_url-referenced .so is cached locally after
// download, keyed by box_index (one door per box_index in a given level, so this is a real,
// stable, collision-free cache key within that level's own lifetime -- a level reload simply
// re-downloads and overwrites, no cache invalidation logic needed for this v0 pass).
#define STORY_DOOR_CACHE_DIR "var/door-script-cache"
static inline void story_doors_cache_path(int box_index, char *out, size_t outsize) {
    snprintf(out, outsize, "%s/door_%d.so", STORY_DOOR_CACHE_DIR, box_index);
}

// door_tick's own real, fixed contract (docs/STORY_SYSTEM_NORTHSTAR.md Part 2):
//   (defn door-tick [(dist-to-player : F64) (state : F64)] : F64 ...)
// emitted by PARENA's C target as `double door_tick(double, double)` -- checked directly against
// a real compiled example, not assumed.
typedef double (*door_tick_fn)(double, double);

typedef struct {
    int box_index;
    void *dl_handle;
    door_tick_fn tick; // NULL for a button-subscribing door (subscribe_button_id >= 0) -- that
                        // case never calls a dist-based tick function at all, see story_doors_tick.
    double state; // 0.0 = closed, 1.0 = open (a script MAY return intermediate values for a
                  // future opening/closing animation state; this v0 pass only acts on the
                  // >= STORY_DOOR_OPEN_THRESHOLD collision toggle, per phys_set_custom_level_box_y)
    // subscribe_button_id/reflux_cursor -- S485, REFLUX pub/sub. -1 = normal door (dist-driven,
    // via `tick` above, completely unchanged). >= 0 = this door ignores distance-to-player
    // entirely; story_doors_tick instead polls the shared REFLUX log starting at reflux_cursor
    // for REFLUX_ACTION_BUTTON_PRESSED actions whose own `a` payload matches this id, and toggles
    // `state` once per match (real open<->closed TOGGLE semantics, not proximity hysteresis --
    // founder real-time: "a door that is open and closable gives you more agency").
    int subscribe_button_id;
    int reflux_cursor;
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
        const char *open_path = ld->script_path;
        char cache_path[LEVEL_BOXES_SCRIPT_PATH_LEN + 32];

        // S485, REFLUX pub/sub: a button-subscribing door skips proximity/script logic entirely
        // -- it's driven purely by REFLUX_ACTION_BUTTON_PRESSED matches, see story_doors_tick.
        // reflux_cursor starts at the log's CURRENT size (not 0) so a button pressed before this
        // level loaded (a stale action already in the shared log from a previous level) never
        // retroactively fires a door that just spawned in closed.
        if (ld->subscribe_button_id >= 0) {
            DoorRuntime *dr = &g_story_doors[g_story_door_count++];
            dr->box_index = ld->box_index;
            dr->dl_handle = NULL;
            dr->tick = NULL;
            dr->state = 0.0;
            dr->subscribe_button_id = ld->subscribe_button_id;
            dr->reflux_cursor = reflux_host_log_size();
            printf("[story-doors] box_index=%d subscribes to button_id=%d via REFLUX\n", ld->box_index, ld->subscribe_button_id);
            continue;
        }

        // Real, working default (found live, 2026-09-17): no script_path AND no script_url means
        // this door genuinely has no custom PARENA script attached -- a real, common, EXPECTED
        // case, not a malformed export. Register it with door_tick_builtin_proximity right away
        // and move on, rather than falling through into the dlopen path below (which used to
        // always get attempted against an empty path and fail). See door_tick_builtin_proximity's
        // own doc comment in level_boxes.h for the full story.
        if (ld->script_path[0] == '\0' && ld->script_url[0] == '\0') {
            DoorRuntime *dr = &g_story_doors[g_story_door_count++];
            dr->box_index = ld->box_index;
            dr->dl_handle = NULL;
            dr->tick = door_tick_builtin_proximity;
            dr->state = 0.0;
            dr->subscribe_button_id = -1;
            printf("[story-doors] no script attached for box_index=%d -- using builtin proximity default\n", ld->box_index);
            continue;
        }

        // S459-82: script_url takes precedence when both are set (script_path stays as a real,
        // honest local-dev/testing fallback, same as it's always been). Download once here, at
        // level load -- not per-tick -- and cache locally so dlopen always has a real file path.
        if (open_path[0] == '\0' && ld->script_url[0] != '\0') {
            // real, deliberate shell-out, matching this file's own established curl-via-popen
            // convention rather than adding a portable mkdir-with-parents helper for one real
            // caller; return value genuinely doesn't matter here -- the fopen("wb") right below
            // is the real, authoritative check for "does this directory actually exist/is it
            // writable," not this call's own exit code.
            if (system("mkdir -p " STORY_DOOR_CACHE_DIR) != 0) { /* checked via fopen below */ }
            char *data = NULL;
            long n = level_boxes_fetch_url(ld->script_url, &data);
            if (n <= 0) {
                fprintf(stderr, "[story-doors] fetching %s failed\n", ld->script_url);
                continue;
            }
            story_doors_cache_path(ld->box_index, cache_path, sizeof(cache_path));
            FILE *f = fopen(cache_path, "wb");
            if (!f) {
                fprintf(stderr, "[story-doors] writing cache file %s failed\n", cache_path);
                free(data);
                continue;
            }
            fwrite(data, 1, (size_t)n, f);
            fclose(f);
            free(data);
            open_path = cache_path;
            printf("[story-doors] downloaded %s -> %s (%ld bytes)\n", ld->script_url, cache_path, n);
        }

        void *h = dlopen(open_path, RTLD_NOW);
        if (!h) {
            fprintf(stderr, "[story-doors] dlopen(%s) failed: %s\n", open_path, dlerror());
            continue;
        }
        dlerror(); // clear any pending error before dlsym, matching dlsym's own documented usage
        door_tick_fn fn = (door_tick_fn)dlsym(h, "door_tick");
        const char *err = dlerror();
        if (err || !fn) {
            fprintf(stderr, "[story-doors] dlsym(door_tick) in %s failed: %s\n", open_path, err ? err : "symbol not found");
            dlclose(h);
            continue;
        }
        DoorRuntime *dr = &g_story_doors[g_story_door_count++];
        dr->box_index = ld->box_index;
        dr->dl_handle = h;
        dr->tick = fn;
        dr->state = 0.0; // real, honest default: every door starts CLOSED
        dr->subscribe_button_id = -1;
        printf("[story-doors] loaded door script %s for box_index=%d\n", open_path, ld->box_index);
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

        // S485, REFLUX pub/sub: a button-subscribing door ignores distance-to-player entirely --
        // poll the shared log from this door's own last-seen cursor, toggle once per matching
        // REFLUX_ACTION_BUTTON_PRESSED (real open<->closed TOGGLE, not proximity hysteresis).
        // Multiple matches arriving in the same tick (unlikely but real -- e.g. two players
        // pressing linked buttons the same tick) toggle multiple times, same as multiple real
        // presses across separate ticks would.
        if (dr->subscribe_button_id >= 0) {
            int log_size = reflux_host_log_size();
            int was_open = dr->state >= STORY_DOOR_OPEN_THRESHOLD;
            for (int li = dr->reflux_cursor; li < log_size; li++) {
                if (reflux_host_action_type_at(li) == REFLUX_ACTION_BUTTON_PRESSED
                    && reflux_host_action_a_at(li) == dr->subscribe_button_id) {
                    dr->state = (dr->state >= STORY_DOOR_OPEN_THRESHOLD) ? 0.0 : 1.0;
                }
            }
            dr->reflux_cursor = log_size;
            int is_open = dr->state >= STORY_DOOR_OPEN_THRESHOLD;
            if (is_open != was_open) {
                phys_set_custom_level_box_y(dr->box_index, is_open);
            }
            continue;
        }

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
