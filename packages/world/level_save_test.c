/* level_save_test.c -- EDIT MAP's F6 saves a REAL level JSON with no IDUNA running.
 *
 * The point of the test: level_boxes_save_to_file must write every field level_boxes_parse_json
 * reads, or a level authored offline silently loses its doors/characters/zones/widgets the next
 * time it's loaded. Build a level with one of everything, save it, load it back, compare.
 *
 *   make test-level-save
 */
#include "edit_map.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define SAVE_PATH "/tmp/level_save_test.json"

static int feq(float a, float b) { return fabsf(a - b) < 1e-3f; }

static void name_set(char *dst, size_t cap, const char *src) { snprintf(dst, cap, "%s", src); }

int main(void) {
    static CustomLevelData a, b;
    memset(&a, 0, sizeof a);

    name_set(a.name, sizeof a.name, "offline-save-test");
    a.width = 120; a.height = 30; a.depth = 90;
    a.ground_plane_enabled = 0; a.ground_plane_squares = 4;
    a.enclosed = 1;
    a.next_level_id = 7;
    a.floor_tinted = 1;
    a.floor_r = 0.1f; a.floor_g = 0.2f; a.floor_b = 0.3f; a.floor_a = 0.55f;

    a.material_count = 2;
    name_set(a.materials[0].name, sizeof a.materials[0].name, "brick");
    name_set(a.materials[0].shader_name, sizeof a.materials[0].shader_name, "standard");
    a.materials[0].specular = 0.04f; a.materials[0].shininess = 6.0f;
    a.materials[0].friction = 0.30f; a.materials[0].density = 1.9f;
    name_set(a.materials[1].name, sizeof a.materials[1].name, "glass");
    name_set(a.materials[1].shader_name, sizeof a.materials[1].shader_name, "glass");
    a.materials[1].specular = 0.9f; a.materials[1].shininess = 64.0f;
    a.materials[1].friction = 0.12f; a.materials[1].density = 2.5f;

    /* a plain wall and a rotated ramp in the second material */
    a.count = 2;
    a.boxes[0].x = 1; a.boxes[0].y = 2; a.boxes[0].z = 3;
    a.boxes[0].w = 10; a.boxes[0].h = 4; a.boxes[0].d = 12;
    a.boxes[0].r = 0.5f; a.boxes[0].g = 0.6f; a.boxes[0].b = 0.7f;
    a.boxes[0].friction = 0.8f;
    a.boxes[0].material_idx = 0;
    a.boxes[1].x = -4; a.boxes[1].y = 1; a.boxes[1].z = 8;
    a.boxes[1].w = 6; a.boxes[1].h = 2; a.boxes[1].d = 6;
    a.boxes[1].r = 0.2f; a.boxes[1].g = 0.9f; a.boxes[1].b = 0.9f;
    a.boxes[1].friction = 0.6f;
    a.boxes[1].material_idx = 1;
    a.boxes[1].rot_y = 45.0f;
    a.boxes[1].ramp = 1;

    a.spawner_count = 2;
    a.spawners[0].id = 3; a.spawners[0].x = 0; a.spawners[0].y = 1; a.spawners[0].z = 2;
    a.spawners[0].yaw = 0; a.spawners[0].team = LEVEL_BOXES_SPAWNER_TEAM_FFA;
    a.spawners[1].id = 9; a.spawners[1].x = 20; a.spawners[1].y = 1; a.spawners[1].z = -20;
    a.spawners[1].yaw = 90; a.spawners[1].team = LEVEL_BOXES_SPAWNER_TEAM_RED;

    a.buggy_spawn_count = 1;
    a.buggy_spawns[0].x = 5; a.buggy_spawns[0].y = 1; a.buggy_spawns[0].z = 6; a.buggy_spawns[0].yaw = 180;

    a.lab_station_count = 2;
    a.lab_stations[0].kind = 3; a.lab_stations[0].x = -130; a.lab_stations[0].y = 2; a.lab_stations[0].z = -205;
    a.lab_stations[1].kind = 5; a.lab_stations[1].x = -84; a.lab_stations[1].y = 2; a.lab_stations[1].z = -185;

    /* one plain proximity door, one subscribed to a button with a Windows-style local script path */
    a.door_count = 2;
    a.doors[0].box_index = 0;
    a.doors[0].subscribe_button_id = -1;
    a.doors[1].box_index = 1;
    a.doors[1].subscribe_button_id = 4;
    name_set(a.doors[1].script_path, sizeof a.doors[1].script_path, "C:\\doors\\tick.so");

    a.button_count = 1;
    a.buttons[0].box_index = 1;
    a.buttons[0].button_id = 4;

    a.nav_node_count = 2;
    a.nav_nodes[0].x = 0; a.nav_nodes[0].y = 1; a.nav_nodes[0].z = 0;
    a.nav_nodes[0].is_cover = 1; a.nav_nodes[0].cover_dir_x = 0.7f; a.nav_nodes[0].cover_dir_z = -0.7f;
    a.nav_nodes[0].neighbor_count = 1; a.nav_nodes[0].neighbors[0] = 1;
    a.nav_nodes[1].x = 5; a.nav_nodes[1].y = 1; a.nav_nodes[1].z = 5;
    a.nav_nodes[1].neighbor_count = 1; a.nav_nodes[1].neighbors[0] = 0;

    a.character_count = 1;
    a.characters[0].role = 2; a.characters[0].kit = 1;
    a.characters[0].x = 12; a.characters[0].y = 1; a.characters[0].z = 12;

    a.level_exit_count = 1;
    a.level_exits[0].x = 40; a.level_exits[0].y = 1; a.level_exits[0].z = 40;
    a.level_exits[0].radius = 3; a.level_exits[0].target_spawner_id = 9;

    a.zone_count = 1;
    a.zones[0].x = -100; a.zones[0].y = 5; a.zones[0].z = -190;
    a.zones[0].radius = 25; a.zones[0].zone_type = 2;

    a.edu_widget_count = 1;
    name_set(a.edu_widgets[0].entity_id, sizeof a.edu_widgets[0].entity_id, "door_gate_alpha");
    a.edu_widgets[0].machine_type = 1;
    a.edu_widgets[0].initial_state = 1;
    a.edu_widgets[0].memory_address = 3;
    a.edu_widgets[0].x = 2; a.edu_widgets[0].y = 1; a.edu_widgets[0].z = 2;
    a.edu_widgets[0].radius = 0;
    a.edu_widgets[0].box_index = 1;

    a.brick_damage_count = 2;
    a.brick_damage[0].wall = 1; a.brick_damage[0].key = 12345u; a.brick_damage[0].hp = 2;
    a.brick_damage[1].wall = 0; a.brick_damage[1].key = 4000000000u; a.brick_damage[1].hp = 0;

    assert(level_boxes_save_to_file(SAVE_PATH, &a));
    assert(level_boxes_save_to_file("/nonexistent-dir/level.json", &a) == 0);

    memset(&b, 0, sizeof b);
    assert(level_boxes_load_from_file(SAVE_PATH, &b));

    /* header */
    assert(strcmp(a.name, b.name) == 0);
    assert(feq(a.width, b.width) && feq(a.height, b.height) && feq(a.depth, b.depth));
    assert(b.ground_plane_enabled == 0 && b.ground_plane_squares == 4);
    assert(b.enclosed == 1);
    assert(b.next_level_id == 7);
    assert(b.floor_tinted == 1 && feq(b.floor_r, a.floor_r) && feq(b.floor_g, a.floor_g) &&
           feq(b.floor_b, a.floor_b) && feq(b.floor_a, a.floor_a));

    /* materials + walls */
    assert(b.material_count == 2);
    assert(strcmp(b.materials[1].name, "glass") == 0);
    assert(feq(b.materials[1].friction, 0.12f) && feq(b.materials[1].density, 2.5f));
    assert(b.count == 2);
    assert(feq(b.boxes[0].x, 1) && feq(b.boxes[0].y, 2) && feq(b.boxes[0].z, 3));
    assert(feq(b.boxes[0].w, 10) && feq(b.boxes[0].h, 4) && feq(b.boxes[0].d, 12));
    assert(feq(b.boxes[0].r, 0.5f) && feq(b.boxes[0].g, 0.6f) && feq(b.boxes[0].b, 0.7f));
    assert(feq(b.boxes[0].friction, 0.8f));
    assert(strcmp(b.materials[b.boxes[0].material_idx].name, "brick") == 0);
    assert(strcmp(b.materials[b.boxes[1].material_idx].name, "glass") == 0);
    assert(feq(b.boxes[1].rot_y, 45.0f) && b.boxes[1].ramp == 1);

    /* spawners, buggy spawns, lab stations */
    assert(b.spawner_count == 2);
    assert(b.spawners[0].id == 3 && b.spawners[0].team == LEVEL_BOXES_SPAWNER_TEAM_FFA);
    assert(b.spawners[1].id == 9 && b.spawners[1].team == LEVEL_BOXES_SPAWNER_TEAM_RED);
    assert(feq(b.spawners[1].yaw, 90));
    assert(b.buggy_spawn_count == 1 && feq(b.buggy_spawns[0].yaw, 180));
    assert(b.lab_station_count == 2);
    assert(b.lab_stations[0].kind == 3 && b.lab_stations[1].kind == 5);

    /* doors + buttons (incl. the escaped Windows path) */
    assert(b.door_count == 2);
    assert(b.doors[0].box_index == 0 && b.doors[0].subscribe_button_id == -1);
    assert(b.doors[1].box_index == 1 && b.doors[1].subscribe_button_id == 4);
    assert(strcmp(b.doors[1].script_path, "C:\\doors\\tick.so") == 0);
    assert(b.button_count == 1 && b.buttons[0].button_id == 4);

    /* nav nodes, characters, exits, zones */
    assert(b.nav_node_count == 2);
    assert(b.nav_nodes[0].is_cover == 1 && b.nav_nodes[0].neighbor_count == 1 && b.nav_nodes[0].neighbors[0] == 1);
    assert(feq(b.nav_nodes[0].cover_dir_x, 0.7f) && feq(b.nav_nodes[0].cover_dir_z, -0.7f));
    assert(b.character_count == 1 && b.characters[0].role == 2 && b.characters[0].kit == 1);
    assert(b.level_exit_count == 1 && feq(b.level_exits[0].radius, 3) && b.level_exits[0].target_spawner_id == 9);
    assert(b.zone_count == 1 && b.zones[0].zone_type == 2 && feq(b.zones[0].radius, 25));

    /* edu widgets + brick damage (unsigned, 30-bit keys survive) */
    assert(b.edu_widget_count == 1);
    assert(strcmp(b.edu_widgets[0].entity_id, "door_gate_alpha") == 0);
    assert(b.edu_widgets[0].machine_type == 1 && b.edu_widgets[0].initial_state == 1);
    assert(b.edu_widgets[0].memory_address == 3 && b.edu_widgets[0].box_index == 1);
    assert(b.brick_damage_count == 2);
    assert(b.brick_damage[0].wall == 1 && b.brick_damage[0].hp == 2);
    assert(b.brick_damage[1].key == 4000000000u && b.brick_damage[1].hp == 0);

    /* saving the reloaded level again must produce the same document (a stable round-trip) */
    assert(level_boxes_save_to_file("/tmp/level_save_test_2.json", &b));
    FILE *f1 = fopen(SAVE_PATH, "rb"), *f2 = fopen("/tmp/level_save_test_2.json", "rb");
    assert(f1 && f2);
    char c1, c2;
    do {
        c1 = (char)fgetc(f1);
        c2 = (char)fgetc(f2);
        assert(c1 == c2);
    } while (c1 != EOF);
    fclose(f1); fclose(f2);

    /* EDIT MAP started on a .json that doesn't exist yet: a new empty level that saves + reloads */
    static CustomLevelData n, n2;
    assert(edit_map_init_new(&n, "brand-new") == &n);
    assert(strcmp(n.name, "brand-new") == 0 && n.count == 0 && n.spawner_count == 1);
    assert(n.ground_plane_enabled == 1 && n.ground_plane_squares == 16);
    assert(n.material_count == 1 && strcmp(n.materials[0].name, LEVEL_BOXES_DEFAULT_MATERIAL) == 0);
    assert(n.spawners[0].team == LEVEL_BOXES_SPAWNER_TEAM_FFA);
    assert(level_boxes_save_to_file("/tmp/level_save_test_new.json", &n));
    memset(&n2, 0, sizeof n2);
    assert(level_boxes_load_from_file("/tmp/level_save_test_new.json", &n2));
    assert(strcmp(n2.name, "brand-new") == 0 && n2.count == 0 && n2.spawner_count == 1);
    assert(n2.ground_plane_enabled == 1 && n2.material_count == 1);
    assert(edit_map_init_new(&n, NULL) == &n && strcmp(n.name, "untitled") == 0);
    assert(edit_map_init_new(NULL, "x") == NULL);

    puts("level_save ok");
    return 0;
}
