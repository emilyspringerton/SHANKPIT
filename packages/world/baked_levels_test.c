/* baked_levels_test.c -- card #479/#480: levels are baked into the binary and served from there unless
 * LIVE LEVEL DOWNLOAD is on. With the setting off no network is touched, so this runs offline.
 *
 *   make test-baked-levels
 */
#include "level_boxes.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    assert(level_boxes_live_download == 0);          /* default: use the compiled-in levels */

    static LevelRegistryEntry main_list[LEVEL_REGISTRY_MAX_ENTRIES];
    int n = level_boxes_fetch_registry_list(main_list, LEVEL_REGISTRY_MAX_ENTRIES);
    assert(n >= 10 && n == (int)(sizeof(BAKED_LEVEL_EXPORTS) / sizeof(BAKED_LEVEL_EXPORTS[0])) - 1 /* minus the zombies default */);

    /* every listed level exports, parses, carries geometry and remembers its registry id */
    static CustomLevelData lvl;
    int default_queue = 0, named_nextown = 0;
    for (int i = 0; i < n; i++) {
        memset(&lvl, 0, sizeof lvl);
        assert(level_boxes_fetch_export(main_list[i].id, &lvl));
        assert(lvl.source_id == main_list[i].id);
        assert(lvl.count >= 1);
        assert(lvl.count == main_list[i].wall_count || main_list[i].wall_count == 0 || lvl.count >= main_list[i].wall_count / 2);
        default_queue += main_list[i].is_default_queue;
        if (strcmp(main_list[i].name, "nextown") == 0) named_nextown = 1;
    }
    assert(default_queue == 1);                      /* exactly one level is flagged the QUEUE default */
    assert(named_nextown);                           /* ZOMBIES' fallback level is baked too */

    /* the ZOMBIES collection's default level is baked and exports */
    static LevelRegistryEntry z[8];
    int zn = level_boxes_fetch_registry_list_in("zombies", z, 8);
    assert(zn == 1 && z[0].is_zombie_default);
    assert(level_boxes_fetch_export(z[0].id, &lvl) && lvl.count >= 1);

    /* an id that was never baked fails cleanly and leaves *out untouched */
    memset(&lvl, 0x5a, sizeof lvl);
    assert(!level_boxes_fetch_export(999999, &lvl));
    assert(((unsigned char *)&lvl)[0] == 0x5a);
    assert(level_boxes_fetch_registry_list_in("nonsense", z, 8) == 0);

    printf("baked_levels_test: %d main levels + %d zombies default, all export; default queue level flagged\n", n, zn);
    return 0;
}
