/* crystal_widgets_test.c -- telecrystals as widgets: city.wmap -> TELECRYSTAL widgets -> cast; level JSON round trip.   make test-crystal-widgets */
#include "crystal_widgets.h"
#include "wmap_level.h"
#include <stdio.h>
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

int main(void) {
    static CustomLevelData lvl, rt; static CrystalWidgets cw, cw2; int j = -1;

    /* the real map: its 3 crystals arrive as machine_type 9 widgets */
    CHECK(level_boxes_load_from_file("maps/gfd_city.wmap", &lvl));
    int nw = 0; for (int i = 0; i < lvl.edu_widget_count; i++) nw += lvl.edu_widgets[i].machine_type == 9;
    CHECK(nw == 3);
    crystal_widgets_load(&cw, &lvl);
    CHECK(cw.n == 3 && !strcmp(cw.id[0], "TOWN_TO_MINES") && cw.dest_zone[0] == 2 && cw.cost[0] == 1000);
    CHECK(cw.cast[0].x == 270 && cw.cast[0].z == 60 && cw.cast[0].r == 14 && cw.cast[0].commit_ms == 600 && cw.cast[0].total_ms == 1000);
    CHECK(!strcmp(cw.prompt[0], "G: TELEPORT MINES"));

    /* the cast over the widget table: near mines crystal, G, jump at 600 ms with that crystal's destination */
    CHECK(crystal_widgets_prompt(&cw, 0, 0) == 2 || crystal_widgets_prompt(&cw, 0, 0) == -1);   /* (0,0) is the Handington-return crystal's ring */
    CHECK(crystal_widgets_prompt(&cw, 270, 60) == 0 && crystal_widgets_prompt(&cw, 900, 900) == -1);
    CHECK(crystal_widgets_tick(&cw, 270, 60, 1, 1000, &j) == TC_STARTED);
    CHECK(crystal_widgets_prompt(&cw, 270, 60) == 0);
    CHECK(crystal_widgets_tick(&cw, 270, 60, 0, 1599, &j) == TC_NONE);
    CHECK(crystal_widgets_tick(&cw, 270, 60, 0, 1600, &j) == TC_JUMP && j == 0 && cw.dest_zone[j] == 2 && cw.cost[j] == 1000);

    /* a hand-authored widget (not from .wmap) survives the level JSON round trip, with defaults and clamps */
    static const char *J = "{\"name\":\"c\",\"walls\":[{\"x\":0,\"y\":0,\"z\":0,\"sx\":4,\"sy\":1,\"sz\":4,\"r\":1,\"g\":1,\"b\":1}],"
        "\"edu_widgets\":[{\"entity_id\":\"gate_crystal\",\"machine_type\":9,\"memory_address\":-1,\"x\":3,\"y\":0,\"z\":4,\"radius\":6,\"dest_zone\":7,\"dest_spawner\":12,\"cost\":50,\"cast_total_ms\":900,\"cast_commit_ms\":500,\"prompt\":\"G: GO\"},"
        "{\"entity_id\":\"bare\",\"machine_type\":9,\"x\":0,\"y\":0,\"z\":0,\"radius\":0},"
        "{\"entity_id\":\"lever1\",\"machine_type\":6,\"memory_address\":10,\"x\":1,\"y\":0,\"z\":1,\"radius\":3}]}";
    CHECK(level_boxes_parse_json(J, &lvl));
    CHECK(lvl.edu_widget_count == 3);
    crystal_widgets_load(&cw, &lvl);
    CHECK(cw.n == 2);                                                            /* the lever is not a crystal */
    CHECK(cw.dest_zone[0] == 7 && cw.dest_spawner[0] == 12 && cw.cost[0] == 50 && cw.cast[0].commit_ms == 500 && !strcmp(cw.prompt[0], "G: GO"));
    CHECK(cw.cast[1].total_ms == 1000 && cw.cast[1].commit_ms == 600 && cw.cast[1].r == 10.0f && !strcmp(cw.prompt[1], "G: TELEPORT"));   /* defaults */
    CHECK(level_boxes_save_to_file("/tmp/crystal_rt.json", &lvl) && level_boxes_load_from_file("/tmp/crystal_rt.json", &rt));
    crystal_widgets_load(&cw2, &rt);
    CHECK(cw2.n == 2 && cw2.dest_zone[0] == 7 && cw2.dest_spawner[0] == 12 && cw2.cost[0] == 50 && !strcmp(cw2.prompt[0], "G: GO") && !strcmp(cw2.id[0], "gate_crystal"));
    CHECK(rt.edu_widgets[2].machine_type == 6);                                   /* other kinds untouched */

    if (g_fail) { printf("test-crystal-widgets: %d FAILED\n", g_fail); return 1; }
    printf("test-crystal-widgets: ok\n");
    return 0;
}
