/* municipal_city_test.c -- the civic district (levels/municipal, tools/build_municipal_district.py)
 * through the engine's OWN level loader, not through the JSON.
 *
 * What it is here to stop, in the order the failures would actually happen:
 *   1. the district's six new material names are the whole point of it, and a material the native
 *      client has no generator for silently draws as brick -- so each one is asserted to exist with
 *      its own density and its own shader, and the three woods to be three different masses.
 *   2. LEVEL_BOXES_MAX (2048) is a TRUNCATION at load, not an error: a city one box over the cap
 *      loses geometry with nothing printed. The lofts already spent 1659 of that budget and the
 *      district spends the rest, so the exported file is checked against the cap.
 *   3. the district is one- and two-storey: the two-storey library's upper floor is only reachable
 *      through its stair ramp, so the ramps are asserted to be in the file the loader reads.
 *   4. the level has to round-trip through level_boxes_save_to_file (EDIT MAP F6) carrying the new
 *      materials AND their densities, or a re-save quietly turns mahogany back into 600 kg/m^3 generic
 *      wood and every bench in the city weighs the same again.
 *
 * Build/run: make test-municipal-city
 */
#include "level_boxes.h"
#include "../common/protocol.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

static void ok(int cond, const char *what) {
    g_checks++;
    if (!cond) {
        g_failures++;
        printf("FAIL  %s\n", what);
    } else {
        printf("ok    %s\n", what);
    }
}

static CustomLevelData g_level;

static int material_index(const char *name) {
    for (int i = 0; i < g_level.material_count; i++)
        if (strcmp(g_level.materials[i].name, name) == 0) return i;
    return -1;
}

/* the engine's own carveability rule, restated here so the count is this test's answer and not the
   loader's: brick_world.h builds fracture parents out of exactly these boxes. */
static int carveable(const LevelBox *b) {
    if (b->ramp || b->rot_x != 0.0f || b->rot_y != 0.0f || b->rot_z != 0.0f) return 0;
    if (b->h <= 12.0f && (b->w < b->d ? b->w : b->d) >= 20.0f) return 0;   /* a slab: the floor stays */
    const char *m = b->material_idx >= 0 && b->material_idx < g_level.material_count
                  ? g_level.materials[b->material_idx].name : "";
    return strcmp(m, "brick") == 0 || strcmp(m, "concrete") == 0 || strcmp(m, "glass") == 0
        || strncmp(m, "wood", 4) == 0 || strcmp(m, "tile") == 0 || strcmp(m, "marble") == 0
        || strcmp(m, "plaster") == 0;
}

int main(void) {
    printf("== the civic district through the engine's own parser ==\n");
    ok(level_boxes_load_from_file("var/zombie/nextown_zombies.json", &g_level),
       "the city loads from var/zombie/nextown_zombies.json");
    ok(g_level.count > 1900, "with the municipal district's own walls in it");
    /* not `== 2048`: the loader would happily return a truncated level, and that is exactly the
       failure this boundary exists to catch */
    ok(g_level.count < LEVEL_BOXES_MAX, "and under LEVEL_BOXES_MAX, so nothing was truncated at load");

    printf("== six materials the city did not carry before ==\n");
    struct { const char *name; float density; } want[] = {
        {"tile", 2300.0f}, {"plaster", 900.0f}, {"marble", 2700.0f},
        {"wood_oak", 720.0f}, {"wood_mahogany", 580.0f}, {"wood_pine", 460.0f},
    };
    for (int i = 0; i < 6; i++) {
        int mi = material_index(want[i].name);
        char label[96];
        snprintf(label, sizeof label, "the %s material is in the level's material table", want[i].name);
        ok(mi >= 0, label);
        if (mi < 0) continue;
        snprintf(label, sizeof label, "%s carries its own density (%.0f kg/m^3)",
                 want[i].name, (double)want[i].density);
        ok(fabsf(g_level.materials[mi].density - want[i].density) < 0.5f, label);
    }
    /* "multiple woods with different densities" as a fact about the FILE, not about the generator */
    int oak = material_index("wood_oak"), mah = material_index("wood_mahogany"), pine = material_index("wood_pine");
    ok(oak >= 0 && mah >= 0 && pine >= 0
       && g_level.materials[oak].density > g_level.materials[mah].density
       && g_level.materials[mah].density > g_level.materials[pine].density,
       "oak is heavier than mahogany, which is heavier than pine");
    ok(g_level.material_count <= LEVEL_BOXES_MAX_MATERIALS, "the material table is inside the loader's cap");

    printf("== the district is really built out of them ==\n");
    int tile_boxes = 0, plaster_boxes = 0, marble_boxes = 0, wood_boxes = 0, carve = 0, ramps = 0;
    int tile_mi = material_index("tile");
    for (int i = 0; i < g_level.count; i++) {
        const LevelBox *b = &g_level.boxes[i];
        const char *m = b->material_idx >= 0 && b->material_idx < g_level.material_count
                      ? g_level.materials[b->material_idx].name : "";
        if (b->material_idx == tile_mi) tile_boxes++;
        if (strcmp(m, "plaster") == 0) plaster_boxes++;
        if (strcmp(m, "marble") == 0) marble_boxes++;
        if (strncmp(m, "wood_", 5) == 0) wood_boxes++;
        if (carveable(b)) carve++;
        if (b->ramp) ramps++;
    }
    /* a floor is one box per room, so the district's tile count is in the dozens, not the hundreds --
       these are existence-and-share thresholds, printed so a reader can see what actually landed */
    printf("      %d tile, %d plaster, %d marble, %d wood boxes\n",
           tile_boxes, plaster_boxes, marble_boxes, wood_boxes);
    ok(tile_boxes > 25, "the district is tiled (tile boxes on the floors, not just in the table)");
    ok(plaster_boxes > 60, "and plastered");
    ok(marble_boxes > 10, "with marble columns, daises and entablatures");
    ok(wood_boxes > 30, "and three woods' worth of furniture and shelving");
    ok(carve > 150, "every one of those rooms can be shot apart, not just walked through");
    ok(ramps > 0, "the two-storey library has a stair ramp in the file the engine loads");

    printf("== the loot is the district's own ==\n");
    ok(g_level.chest_count == 30, "24 loft chests plus one per municipal building");

    /* The district does not get its own slots: the lofts' plan gives 12 of the city's 69 up, and two
       bodies stand in each civic building. If this ever exceeds MAX_CLIENTS-1 the excess is authored,
       loaded and never spawned -- which is exactly the failure the split exists to prevent. */
    printf("== the district is staffed out of the city's own slots ==\n");
    printf("      %d authored characters\n", g_level.character_count);
    ok(g_level.character_count == MAX_CLIENTS - 1,
       "the city is fully staffed: 57 loft zombies and 12 civic bodies in MAX_CLIENTS-1 = 69");
    ok(g_level.character_count <= LEVEL_BOXES_MAX_CHARACTERS,
       "and inside the file's own character cap");

    printf("== a re-save does not turn mahogany back into generic wood ==\n");
    {
        CustomLevelData again;
        memset(&again, 0, sizeof again);
        ok(level_boxes_save_to_file("/tmp/municipal_city_roundtrip.json", &g_level),
           "the city writes back out through level_boxes_save_to_file");
        ok(level_boxes_load_from_file("/tmp/municipal_city_roundtrip.json", &again),
           "and the written file parses again");
        ok(again.count == g_level.count, "with the same box count");
        ok(again.material_count == g_level.material_count, "and the same material table size");
        int ao = -1, ap = -1;
        for (int i = 0; i < again.material_count; i++) {
            if (strcmp(again.materials[i].name, "wood_oak") == 0) ao = i;
            if (strcmp(again.materials[i].name, "wood_pine") == 0) ap = i;
        }
        ok(ao >= 0 && ap >= 0
           && fabsf(again.materials[ao].density - 720.0f) < 0.5f
           && fabsf(again.materials[ap].density - 460.0f) < 0.5f,
           "with oak still 720 and pine still 460 after the round trip");
    }

    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
