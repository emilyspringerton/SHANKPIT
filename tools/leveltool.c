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

int main(int argc, char **argv) {
    if (argc < 3) { usage(); return 2; }
    const char *cmd = argv[1];

    if (strcmp(cmd, "info") == 0) {
        static CustomLevelData lvl;
        if (!load_or_die(argv[2], &lvl)) return 1;
        print_summary(argv[2], &lvl);
        return 0;
    }
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
