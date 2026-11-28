/* level_mesh_schema_test.c -- per-box mesh / normal_map / lod_bias: parse, defaults, hostile ids, save round trip.   make test-level-mesh */
#include "level_boxes.h"
#include <stdio.h>
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static const char *J =
 "{\"name\":\"m\",\"walls\":["
 "{\"x\":0,\"y\":1,\"z\":0,\"sx\":2,\"sy\":2,\"sz\":2,\"r\":1,\"g\":1,\"b\":1},"
 "{\"x\":5,\"y\":1,\"z\":0,\"sx\":2,\"sy\":2,\"sz\":2,\"r\":1,\"g\":1,\"b\":1,\"mesh\":\"crystal_a\",\"normal_map\":\"3f9a0c.tga\",\"lod_bias\":1},"
 "{\"x\":9,\"y\":1,\"z\":0,\"sx\":2,\"sy\":2,\"sz\":2,\"r\":1,\"g\":1,\"b\":1,\"mesh\":\"../../etc/passwd\",\"normal_map\":\"a b\",\"lod_bias\":99},"
 "{\"x\":9,\"y\":1,\"z\":4,\"sx\":2,\"sy\":2,\"sz\":2,\"r\":1,\"g\":1,\"b\":1,\"mesh\":\".hidden\"}"
 "]}";

int main(void) {
    static CustomLevelData a, b;
    CHECK(level_boxes_parse_json(J, &a));
    CHECK(a.count == 4);
    CHECK(a.boxes[0].mesh[0] == 0 && a.boxes[0].normal_map[0] == 0 && a.boxes[0].lod_bias == 0);   /* old levels: plain box */
    CHECK(!strcmp(a.boxes[1].mesh, "crystal_a") && !strcmp(a.boxes[1].normal_map, "3f9a0c.tga") && a.boxes[1].lod_bias == 1);
    CHECK(a.boxes[2].mesh[0] == 0 && a.boxes[2].normal_map[0] == 0);                                 /* hostile ids read as "" */
    CHECK(a.boxes[2].lod_bias == 3);                                                                   /* clamped */
    CHECK(a.boxes[3].mesh[0] == 0);
    /* collision data is untouched by the skin */
    CHECK(a.boxes[1].x == 5 && a.boxes[1].w == 2 && a.boxes[1].h == 2);
    /* save -> load round trip keeps the override */
    CHECK(level_boxes_save_to_file("/tmp/level_mesh_rt.json", &a));
    CHECK(level_boxes_load_from_file("/tmp/level_mesh_rt.json", &b));
    CHECK(b.count == 4 && !strcmp(b.boxes[1].mesh, "crystal_a") && !strcmp(b.boxes[1].normal_map, "3f9a0c.tga") && b.boxes[1].lod_bias == 1);
    CHECK(b.boxes[0].mesh[0] == 0);
    if (g_fail) { printf("test-level-mesh: %d FAILED\n", g_fail); return 1; }
    printf("test-level-mesh: ok\n");
    return 0;
}
