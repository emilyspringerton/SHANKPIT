/* tools/leveltool.c -- offline level JSON utility for the IDUNA-free authoring flow.
 *
 * EDIT MAP's F6 writes real level JSON (packages/world/level_boxes.h's level_boxes_save_to_file).
 * This is the headless companion: inspect a map, create an empty one, re-save one, and -- the
 * useful one -- `check`, which loads a file, saves it, loads it again and proves nothing the loader
 * parses was dropped. That's the same guarantee make test-level-save asserts, runnable against any
 * map you just built (or every checked-in export) without launching the lobby.
 *
 *   make leveltool
 *   ./bin/leveltool info  var/maps/mymap.json
 *   ./bin/leveltool map   var/maps/mymap.json
 *   ./bin/leveltool check var/maps/mymap.json
 *   ./bin/leveltool save  var/maps/in.json var/maps/out.json
 *   ./bin/leveltool new   mymap var/maps/mymap.json
 *
 * POSIX only (temp files live in $TMPDIR or /tmp) -- it is a dev tool, never linked into the game.
 */
#include "level_boxes.h"
#include "edit_map.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *tmp_dir(void) {
    const char *d = getenv("TMPDIR");
    return (d && d[0]) ? d : "/tmp";
}

static void usage(void) {
    fprintf(stderr,
            "usage: leveltool <command> [args]\n"
            "  info  <level.json>            print a summary of a level file\n"
            "  map   <level.json>            print a top-down ASCII view of the level\n"
            "  check <level.json>            prove a save/load round-trip drops nothing\n"
            "  save  <in.json> <out.json>    load a level and write it back out\n"
            "  new   <name> <out.json>       write a new empty level (EDIT MAP's new-level defaults)\n");
}

static int load_or_die(const char *path, CustomLevelData *lvl) {
    memset(lvl, 0, sizeof(*lvl));
    if (!level_boxes_load_from_file(path, lvl)) {
        fprintf(stderr, "leveltool: cannot load level JSON: %s\n", path);
        return 0;
    }
    return 1;
}

static void print_summary(const char *path, const CustomLevelData *l) {
    printf("%s\n", path);
    printf("  name %-24s %g x %g x %g   ground plane %s (%d squares)%s\n",
           l->name[0] ? l->name : "(unnamed)", (double)l->width, (double)l->height, (double)l->depth,
           l->ground_plane_enabled ? "on" : "off", l->ground_plane_squares,
           l->enclosed ? "   enclosed" : "");
    printf("  walls %d   materials %d   spawners %d   doors %d   buttons %d\n",
           l->count, l->material_count, l->spawner_count, l->door_count, l->button_count);
    printf("  buggy spawns %d   lab stations %d   nav nodes %d   characters %d\n",
           l->buggy_spawn_count, l->lab_station_count, l->nav_node_count, l->character_count);
    printf("  exits %d   zones %d   edu widgets %d   brick damage cells %d%s\n",
           l->level_exit_count, l->zone_count, l->edu_widget_count, l->brick_damage_count,
           l->next_level_id ? "" : "   (no next level)");
    if (l->next_level_id) printf("  next level id %d\n", l->next_level_id);
    if (l->floor_tinted)
        printf("  floor tint  r %g  g %g  b %g  a %g\n",
               (double)l->floor_r, (double)l->floor_g, (double)l->floor_b, (double)l->floor_a);
}

/* Every array count plus the header fields that must survive. Deliberately structural (not
 * per-wall floats): packages/world/level_save_test.c already asserts the field-by-field detail. */
static int same_shape(const CustomLevelData *a, const CustomLevelData *b) {
    return strcmp(a->name, b->name) == 0 && a->count == b->count &&
           a->material_count == b->material_count && a->spawner_count == b->spawner_count &&
           a->door_count == b->door_count && a->button_count == b->button_count &&
           a->buggy_spawn_count == b->buggy_spawn_count && a->lab_station_count == b->lab_station_count &&
           a->nav_node_count == b->nav_node_count && a->character_count == b->character_count &&
           a->level_exit_count == b->level_exit_count && a->zone_count == b->zone_count &&
           a->edu_widget_count == b->edu_widget_count && a->brick_damage_count == b->brick_damage_count &&
           a->next_level_id == b->next_level_id && a->enclosed == b->enclosed &&
           a->ground_plane_enabled == b->ground_plane_enabled &&
           a->ground_plane_squares == b->ground_plane_squares && a->floor_tinted == b->floor_tinted;
}

static int same_file(const char *pa, const char *pb) {
    FILE *fa = fopen(pa, "rb"), *fb = fopen(pb, "rb");
    if (!fa || !fb) { if (fa) fclose(fa); if (fb) fclose(fb); return 0; }
    int same = 1, ca, cb;
    do {
        ca = fgetc(fa);
        cb = fgetc(fb);
        if (ca != cb) { same = 0; break; }
    } while (ca != EOF);
    fclose(fa); fclose(fb);
    return same;
}

static int cmd_check(const char *path) {
    static CustomLevelData a, b;
    if (!load_or_die(path, &a)) return 1;

    char pass1[512], pass2[512];
    snprintf(pass1, sizeof pass1, "%s/leveltool_%ld_a.json", tmp_dir(), (long)getpid());
    snprintf(pass2, sizeof pass2, "%s/leveltool_%ld_b.json", tmp_dir(), (long)getpid());

    if (!level_boxes_save_to_file(pass1, &a)) {
        fprintf(stderr, "leveltool: cannot write %s\n", pass1);
        return 1;
    }
    if (!load_or_die(pass1, &b)) return 1;
    if (!same_shape(&a, &b)) {
        fprintf(stderr,
                "leveltool: FAIL %s -- round-trip changed the level "
                "(walls %d->%d, doors %d->%d, buttons %d->%d, characters %d->%d, exits %d->%d, "
                "zones %d->%d, edu widgets %d->%d, brick cells %d->%d, stations %d->%d, "
                "buggy spawns %d->%d, nav nodes %d->%d, materials %d->%d, spawners %d->%d)\n",
                path, a.count, b.count, a.door_count, b.door_count, a.button_count, b.button_count,
                a.character_count, b.character_count, a.level_exit_count, b.level_exit_count,
                a.zone_count, b.zone_count, a.edu_widget_count, b.edu_widget_count,
                a.brick_damage_count, b.brick_damage_count, a.lab_station_count, b.lab_station_count,
                a.buggy_spawn_count, b.buggy_spawn_count, a.nav_node_count, b.nav_node_count,
                a.material_count, b.material_count, a.spawner_count, b.spawner_count);
        remove(pass1);
        return 1;
    }
    if (!level_boxes_save_to_file(pass2, &b) || !same_file(pass1, pass2)) {
        fprintf(stderr, "leveltool: FAIL %s -- writer is not stable (two saves differ)\n", path);
        remove(pass1);
        remove(pass2);
        return 1;
    }
    remove(pass1);
    remove(pass2);
    printf("ok  %-48s %d walls, %d doors, %d characters, %d exits, %d zones\n",
           path, b.count, b.door_count, b.character_count, b.level_exit_count, b.zone_count);
    return 0;
}

/* ---- top-down preview ----------------------------------------------------------------
 * A map saved with F6 can't be looked at on a box with no display, which makes "did my geometry
 * land where I meant" unanswerable. This projects the X/Z footprint into a fixed ASCII grid: walls
 * as '#', then spawners/characters/exits/zones/doors/buttons on top of them. Deliberately rough
 * (one cell per (span / 75) units, Y ignored) -- enough to see a layout, not a screenshot. */
#define MAP_MAX_W 76
#define MAP_MAX_H 36

static void map_plot(char grid[MAP_MAX_H][MAP_MAX_W], int w, int h, double x, double z,
                     double minx, double minz, double cell, char ch) {
    if (cell <= 0) return;
    int cx = (int)((x - minx) / cell + 0.5);
    int cz = (int)((z - minz) / cell + 0.5);
    if (cx < 0 || cx >= w || cz < 0 || cz >= h) return;
    grid[cz][cx] = ch;
}

static int cmd_map(const char *path) {
    static CustomLevelData l;
    if (!load_or_die(path, &l)) return 1;

    double minx = 1e30, maxx = -1e30, minz = 1e30, maxz = -1e30;
    for (int i = 0; i < l.count; i++) {
        const LevelBox *b = &l.boxes[i];
        double hx = (double)b->w * 0.5, hz = (double)b->d * 0.5;
        if ((double)b->x - hx < minx) minx = (double)b->x - hx;
        if ((double)b->x + hx > maxx) maxx = (double)b->x + hx;
        if ((double)b->z - hz < minz) minz = (double)b->z - hz;
        if ((double)b->z + hz > maxz) maxz = (double)b->z + hz;
    }
    if (l.count == 0) { minx = -20; maxx = 20; minz = -20; maxz = 20; }
    double spanx = maxx - minx, spanz = maxz - minz;
    if (spanx < 1) spanx = 1;
    if (spanz < 1) spanz = 1;
    double cell = spanx / (MAP_MAX_W - 1);
    if (spanz / (MAP_MAX_H - 1) > cell) cell = spanz / (MAP_MAX_H - 1);
    int w = (int)(spanx / cell) + 1, h = (int)(spanz / cell) + 1;
    if (w > MAP_MAX_W) w = MAP_MAX_W;
    if (h > MAP_MAX_H) h = MAP_MAX_H;
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    static char grid[MAP_MAX_H][MAP_MAX_W];
    memset(grid, ' ', sizeof grid);
    for (int i = 0; i < l.count; i++) {
        const LevelBox *b = &l.boxes[i];
        double hx = (double)b->w * 0.5, hz = (double)b->d * 0.5;
        map_plot(grid, w, h, (double)b->x, (double)b->z, minx, minz, cell, '#');
        map_plot(grid, w, h, (double)b->x - hx, (double)b->z - hz, minx, minz, cell, '#');
        map_plot(grid, w, h, (double)b->x + hx, (double)b->z + hz, minx, minz, cell, '#');
        map_plot(grid, w, h, (double)b->x - hx, (double)b->z + hz, minx, minz, cell, '#');
        map_plot(grid, w, h, (double)b->x + hx, (double)b->z - hz, minx, minz, cell, '#');
    }
    for (int i = 0; i < l.zone_count; i++)
        map_plot(grid, w, h, (double)l.zones[i].x, (double)l.zones[i].z, minx, minz, cell, 'z');
    for (int i = 0; i < l.level_exit_count; i++)
        map_plot(grid, w, h, (double)l.level_exits[i].x, (double)l.level_exits[i].z, minx, minz, cell, 'E');
    for (int i = 0; i < l.character_count; i++)
        map_plot(grid, w, h, (double)l.characters[i].x, (double)l.characters[i].z, minx, minz, cell, 'C');
    for (int i = 0; i < l.spawner_count; i++)
        map_plot(grid, w, h, (double)l.spawners[i].x, (double)l.spawners[i].z, minx, minz, cell, 'S');
    for (int i = 0; i < l.door_count; i++) {
        int bi = l.doors[i].box_index;
        if (bi >= 0 && bi < l.count)
            map_plot(grid, w, h, (double)l.boxes[bi].x, (double)l.boxes[bi].z, minx, minz, cell, 'D');
    }
    for (int i = 0; i < l.button_count; i++) {
        int bi = l.buttons[i].box_index;
        if (bi >= 0 && bi < l.count)
            map_plot(grid, w, h, (double)l.boxes[bi].x, (double)l.boxes[bi].z, minx, minz, cell, 'B');
    }

    printf("%s -- top-down, -Z is up/north, Y ignored\n", path);
    printf("  x %.1f..%.1f   z %.1f..%.1f   %dx%d cells, %.2f units/cell\n",
           minx, maxx, minz, maxz, w, h, cell);
    putchar('+');
    for (int c = 0; c < w; c++) putchar('-');
    printf("+\n");
    for (int r = 0; r < h; r++) {
        putchar('|');
        fwrite(grid[r], 1, (size_t)w, stdout);
        printf("|\n");
    }
    putchar('+');
    for (int c = 0; c < w; c++) putchar('-');
    printf("+\n");
    printf("  '#' wall  'S' spawner  'C' character  'E' exit  'z' zone  'D' door  'B' button\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) { usage(); return 2; }
    const char *cmd = argv[1];

    if (strcmp(cmd, "info") == 0) {
        static CustomLevelData lvl;
        if (!load_or_die(argv[2], &lvl)) return 1;
        print_summary(argv[2], &lvl);
        return 0;
    }
    if (strcmp(cmd, "map") == 0) return cmd_map(argv[2]);
    if (strcmp(cmd, "check") == 0) return cmd_check(argv[2]);

    if (strcmp(cmd, "save") == 0) {
        if (argc < 4) { usage(); return 2; }
        static CustomLevelData lvl;
        if (!load_or_die(argv[2], &lvl)) return 1;
        if (!level_boxes_save_to_file(argv[3], &lvl)) {
            fprintf(stderr, "leveltool: cannot write %s\n", argv[3]);
            return 1;
        }
        printf("wrote %s (%d walls)\n", argv[3], lvl.count);
        return 0;
    }
    if (strcmp(cmd, "new") == 0) {
        if (argc < 4) { usage(); return 2; }
        static CustomLevelData lvl;
        edit_map_init_new(&lvl, argv[2]);
        if (!level_boxes_save_to_file(argv[3], &lvl)) {
            fprintf(stderr, "leveltool: cannot write %s\n", argv[3]);
            return 1;
        }
        printf("wrote %s (new level '%s', %g x %g x %g, one spawner)\n",
               argv[3], lvl.name, (double)lvl.width, (double)lvl.height, (double)lvl.depth);
        return 0;
    }
    usage();
    return 2;
}
