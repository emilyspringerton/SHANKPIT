/* wmap_level_test.c -- the .wmap reader: GFD's city.wmap loads as an ordinary level.   make test-wmap */
#include "level_boxes.h"
#include <assert.h>
#include <stdio.h>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

int main(void) {
    static CustomLevelData lvl;
    /* the real thing: GoblinFoxDragon's town, via the ordinary level loader (dispatches on .wmap) */
    CHECK(level_boxes_load_from_file("maps/gfd_city.wmap", &lvl));
    CHECK(lvl.count == 54);
    CHECK(lvl.boxes[0].w == 2520.0f && lvl.boxes[0].y == -6.0f);         /* box 0 = the floor slab */
    CHECK(lvl.boxes[5].x == 180.0f && lvl.boxes[5].d == 820.0f);          /* Grand Avenue spine */
    CHECK(lvl.spawner_count == 5);
    CHECK(lvl.spawners[0].x == 180.0f && lvl.spawners[0].y == 8.0f && lvl.spawners[0].z == 176.0f);
    CHECK(lvl.material_count == 1 && lvl.ground_plane_enabled == 0);
    CHECK(g_wmap_zone_id == 0 && g_wmap_bound == 1900.0f);
    CHECK(g_wmap_crystal_count == 3);
    CHECK(!strcmp(g_wmap_crystals[0].id, "TELECRYSTAL_ID_TOWN_TO_MINES"));
    CHECK(g_wmap_crystals[0].x == 270.0f && g_wmap_crystals[0].z == 60.0f && g_wmap_crystals[0].r == 14.0f);
    CHECK(g_wmap_crystals[0].dest_zone == 2 && g_wmap_crystals[0].cost == 1000);
    CHECK(g_wmap_crystals[0].total_ms == 1000 && g_wmap_crystals[0].commit_ms == 600);
    CHECK(!strcmp(g_wmap_crystals[0].prompt, "G: TELEPORT MINES"));
    CHECK(!strcmp(g_wmap_crystals[2].prompt, "G: RETURN NEW HANDINGTON"));

    /* a legacy shankpit-460 .map-style body is a valid worldmap too (implicit zone) */
    CHECK(wmap_level_parse("box 0 -2 0 800 4 800\nbox 0 3 -330 64 6 44\nspawn 0 6.5 -330\npoi rocket_launcher 2 13 -4\n", &lvl));
    CHECK(lvl.count == 2 && lvl.spawner_count == 1);
    /* only the first zone loads */
    CHECK(wmap_level_parse("worldmap 1\nzone 4 town A\nbox 0 0 0 1 1 1\nzone 5 field B\nbox 9 9 9 9 9 9\n", &lvl));
    CHECK(lvl.count == 1 && g_wmap_zone_id == 4);
    /* rejects: bad version, bad box, no boxes */
    CHECK(!wmap_level_parse("worldmap 2\nbox 0 0 0 1 1 1\n", &lvl));
    CHECK(!wmap_level_parse("box 1 2 3\n", &lvl));
    CHECK(!wmap_level_parse("# nothing\n", &lvl));

    if (g_fail) { printf("test-wmap: %d FAILED\n", g_fail); return 1; }
    printf("test-wmap: ok\n");
    return 0;
}
