/* env_parity_test.c -- the shared lighting and material fixtures, asserted from the C side.
 *
 * The files are PARENA/stdlib/shankpit/{lighting,material}_parity.txt, and NOOBTOPIA's tests/lighting_test.lua and
 * tests/material_test.lua assert the SAME two files. That is the whole point: the C emitted from those .prn files is
 * authoritative, and a Luau port that drifts is a red test in one repo instead of two worlds quietly disagreeing
 * about how dark a storm is or how reflective a mirror is.
 *
 * The numbers were produced by running these same functions; the fixture exists so that a change to either side
 * has to be a change on both sides.
 *
 * Run: make test-env-parity        (PARENA_DIR overrides where the fixtures are read from)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* PARENA-generated (packages/simulation/lighting_rules.c, material_rules.c). */
int env_occlusion(int, int, int, int, int);
int env_light_bleed(int, int);
int env_exposure(int);
int env_ambient_floor(int, int, int);
int env_emissive_scale(int);
int env_flash_boost(int);
int env_emitter_range_scale(int);
int env_emitter_spread_scale(int);

int mat_reflection_strength_permille(int);
int mat_kind_count(void);
int mat_scroll_speed_milli(int);
int mat_scroll_u_milli(int, int);
int mat_scroll_v_milli(int, int);
int mat_frame_index(int, int, int);
int mat_light_style_count(void);
int light_style_level_permille(int, int);
int mat_emissive_milli(int, int, int);
int tint_low_health_milli(int, int);
int tint_submerged_milli(int);
int tint_acid_milli(int);
int tint_shield_milli(int, int);
int palette_index_256(int);
int vlm_axis_index_milli(int, int, int, int);
int vlm_ambient_floor_milli(void);

static int g_checks = 0;
static int g_fails = 0;

static void expect(int got, int want, const char *what) {
    g_checks++;
    if (got != want) {
        g_fails++;
        printf("FAIL: %s (got %d want %d)\n", what, got, want);
    }
}

static void expect_true(int cond, const char *what) {
    g_checks++;
    if (!cond) { g_fails++; printf("FAIL: %s\n", what); }
}

static FILE *open_fixture(const char *name) {
    static char path[512];
    const char *dir = getenv("PARENA_DIR");
    if (dir && *dir) snprintf(path, sizeof path, "%s/stdlib/shankpit/%s", dir, name);
    else snprintf(path, sizeof path, "../PARENA/stdlib/shankpit/%s", name);
    FILE *f = fopen(path, "r");
    if (!f) printf("FAIL: cannot open %s (set PARENA_DIR to the PARENA checkout)\n", path);
    return f;
}

/* Pull every integer out of a line, ignoring a trailing comment. */
static int scan_ints(char *line, int *out, int max) {
    char *hash = strchr(line, '#');
    if (hash) *hash = '\0';
    int n = 0;
    char *p = line;
    while (n < max) {
        char *end = NULL;
        long v = strtol(p, &end, 10);
        if (end == p) break;
        out[n++] = (int)v;
        p = end;
    }
    return n;
}

static int lighting_suite(void) {
    FILE *f = open_fixture("lighting_parity.txt");
    if (!f) return 0;
    char line[512];
    int rows = 0;
    while (fgets(line, sizeof line, f)) {
        int v[16];
        if (scan_ints(line, v, 16) != 16) continue;
        rows++;
        int cover = v[0], rain = v[1], storm = v[2], fog = v[3], interior = v[4];
        int level = v[5], flash = v[6], vis = v[7];
        int o = env_occlusion(cover, rain, storm, fog, interior);
        int floor_light = env_ambient_floor(o, level, interior);
        int range = env_emitter_range_scale(vis);
        char tag[160];
        snprintf(tag, sizeof tag, "lighting row %d (%d/%d/%d/%d interior %d slider %d)",
                 rows, cover, rain, storm, fog, interior, level);
        expect(o, v[8], tag);
        expect(env_light_bleed(o, interior), v[9], "light bleed");
        expect(env_exposure(o), v[10], "exposure");
        expect(floor_light, v[11], "ambient floor");
        expect(env_emissive_scale(floor_light), v[12], "emissive scale (fed the floor)");
        expect(env_flash_boost(flash), v[13], "flash boost");
        expect(range, v[14], "emitter range");
        expect(env_emitter_spread_scale(range), v[15], "emitter spread");
    }
    fclose(f);
    expect_true(rows >= 12, "read at least 12 lighting parity rows");
    return rows;
}

static int material_suite(void) {
    FILE *f = open_fixture("material_parity.txt");
    if (!f) return 0;
    char line[512];
    int rows = 0;
    while (fgets(line, sizeof line, f)) {
        int v[9];
        int n = scan_ints(line, v, 9);
        if (n < 7) continue;
        rows++;
        int q = v[0], a = v[1], b = v[2], c = v[3], d = v[4];
        char tag[160];
        snprintf(tag, sizeof tag, "material row %d q=%d", rows, q);
        int got1 = -1, got2 = 0, have2 = 0;
        switch (q) {
        case 0: got1 = mat_reflection_strength_permille(a); break;
        case 1: got1 = light_style_level_permille(a, b); break;
        case 2: got1 = mat_emissive_milli(a, b, c); break;
        case 3: got1 = mat_frame_index(a, b, c); break;
        case 4: got1 = mat_scroll_u_milli(a, b); got2 = mat_scroll_v_milli(a, b); have2 = 1; break;
        case 5: got1 = tint_low_health_milli(a, b); break;
        case 6: got1 = tint_submerged_milli(a); break;
        case 7: got1 = tint_acid_milli(a); break;
        case 8: got1 = tint_shield_milli(a, b); break;
        case 9: got1 = palette_index_256(a); break;
        case 10: got1 = vlm_axis_index_milli(a, b, c, d); break;
        default: expect_true(0, "material row names a known question"); continue;
        }
        expect(got1, v[6], tag);
        if (have2) expect(got2, v[7], "scroll v");
    }
    fclose(f);
    expect_true(rows >= 20, "read at least 20 material parity rows");
    return rows;
}

int main(void) {
    int lrows = lighting_suite();
    int mrows = material_suite();

    /* The two constants the Luau host also pins, so neither side can quietly renumber them. */
    expect(mat_kind_count(), 5, "five material kinds");
    expect(mat_light_style_count(), 8, "eight light styles");
    expect(vlm_ambient_floor_milli(), 120, "the voxel lightmap floor is 120");

    if (g_fails) {
        printf("env_parity: %d of %d FAILED\n", g_fails, g_checks);
        return 1;
    }
    printf("env_parity OK (%d checks, %d lighting rows, %d material rows)\n", g_checks, lrows, mrows);
    return 0;
}
