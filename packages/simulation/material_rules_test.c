/* material_rules_test.c -- `make test-material-rules`. Asserts the storytelling vocabulary the
 * renderer draws from: UV scrolling that wraps, frame-loop sheets that step and repeat, every light
 * style staying inside 0..1000 and actually doing its job (a strobe that goes dark, a fluorescent
 * that drops out rarely, a broken tube that is mostly off), the tints being monotonic in the danger
 * they represent, palette indices wrapping instead of going negative, and the probe-grid axis lookup
 * clamping at both ends. */
#include <stdio.h>
#include "material_rules_host.h"

static int fails = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { printf("FAIL %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } \
} while (0)

int main(void) {
    CHECK(mat_kind_count() == 5, "kinds %d", mat_kind_count());
    CHECK(mat_light_style_count() == 8, "styles %d", mat_light_style_count());

    /* ---- UV animation ---- */
    CHECK(mat_scroll_u_milli(0, 37) == 0 && mat_scroll_v_milli(0, 37) == 0, "a plain surface moved");
    for (int t = 0; t < 200; t++) {
        int u = mat_scroll_u_milli(1, t), v = mat_scroll_v_milli(1, t);
        CHECK(u >= 0 && u < 1000 && v >= 0 && v < 1000, "ooze offset %d/%d at tick %d", u, v, t);
    }
    CHECK(mat_scroll_u_milli(1, 0) != mat_scroll_u_milli(1, 3), "ooze does not move between ticks");
    CHECK(mat_scroll_u_milli(1, 60) == mat_scroll_u_milli(1, 0), "ooze does not wrap on the second");

    /* ---- frame-loop sheets ---- */
    {
        int seen[8] = {0};
        for (int t = 0; t < 60; t++) {
            int f = mat_frame_index(10, 8, t);
            CHECK(f >= 0 && f < 8, "frame %d at tick %d", f, t);
            seen[f] = 1;
        }
        int n = 0;
        for (int i = 0; i < 8; i++) n += seen[i];
        CHECK(n == 8, "a 10 fps / 8 frame sheet only reached %d frames in a second", n);
        CHECK(mat_frame_index(0, 8, 5) == 0 && mat_frame_index(10, 0, 5) == 0, "degenerate sheet index");
        CHECK(mat_frame_index(10, 8, 60) == mat_frame_index(10, 8, 0), "sheet does not loop on the second");
    }

    /* ---- light styles ---- */
    {
        int strobe_dark = 0, strobe_lit = 0, fluoro_drops = 0, broken_lit = 0, broken_dark = 0;
        for (int style = 0; style < mat_light_style_count(); style++) {
            for (int t = 0; t < 600; t++) {
                int lv = light_style_level_permille(style, t);
                CHECK(lv >= 0 && lv <= 1000, "style %d level %d at tick %d", style, lv, t);
                if (style == 1) { if (lv == 0) strobe_dark++; else strobe_lit++; }
                if (style == 2 && lv < 500) fluoro_drops++;
                if (style == 3) { if (lv > 500) broken_lit++; else broken_dark++; }
            }
        }
        CHECK(strobe_dark > 0 && strobe_lit > 0, "a strobe that never strobes");
        CHECK(fluoro_drops > 0 && fluoro_drops < 20, "fluorescent dropped out %d times in 10 s", fluoro_drops);
        CHECK(broken_dark > broken_lit * 2, "a broken tube is not mostly dark (%d lit / %d dark)",
              broken_lit, broken_dark);
        CHECK(light_style_level_permille(0, 0) == 1000 && light_style_level_permille(0, 599) == 1000,
              "a steady fixture is not steady");
    }
    CHECK(mat_emissive_milli(1, 0, 100) >= 350, "a lit terminal vanished in a dark room");
    CHECK(mat_emissive_milli(1, 0, 100) <= 1000, "emissive above full");
    CHECK(mat_emissive_milli(0, 0, 1000) >= 120, "a bright room's fixture went out entirely");

    /* ---- tints ---- */
    CHECK(tint_low_health_milli(300, 300) == 0, "full health is tinted");
    CHECK(tint_low_health_milli(0, 300) == 1000, "zero health is not fully tinted");
    CHECK(tint_low_health_milli(60, 300) > tint_low_health_milli(150, 300), "tint not monotonic in damage");
    CHECK(tint_low_health_milli(0, 0) == 0, "zero max health divided by zero");
    CHECK(tint_submerged_milli(0) == 0 && tint_submerged_milli(4000) == 1000, "submerged ramp");
    CHECK(tint_submerged_milli(9000) == 1000, "submerged not clamped");
    CHECK(tint_acid_milli(1000) == 900, "acid tint %d", tint_acid_milli(1000));
    CHECK(tint_shield_milli(100, 100) == 700, "shield tint %d", tint_shield_milli(100, 100));
    CHECK(tint_shield_milli(10, 0) == 0, "shield tint with no shield");

    /* ---- palette LUT ---- */
    CHECK(palette_index_256(-1) == 255, "index -1 wrapped to %d", palette_index_256(-1));
    CHECK(palette_index_256(256) == 0, "index 256 wrapped to %d", palette_index_256(256));
    CHECK(palette_index_256(300) == 44, "index 300 wrapped to %d", palette_index_256(300));

    /* ---- voxel probe grid ---- */
    CHECK(vlm_axis_index_milli(0, 1000, 4000, 32) == 0, "below the volume is not cell 0");
    CHECK(vlm_axis_index_milli(9000, 1000, 4000, 32) == 31, "above the volume is not the last cell");
    CHECK(vlm_axis_index_milli(3000, 1000, 4000, 32) == 15, "midpoint cell %d",
          vlm_axis_index_milli(3000, 1000, 4000, 32));
    CHECK(vlm_axis_index_milli(3000, 1000, 0, 32) == 0, "zero extent did not fall back");
    CHECK(vlm_ambient_floor_milli() > 0 && vlm_ambient_floor_milli() < 300,
          "outside-volume floor %d is not a dark corner", vlm_ambient_floor_milli());

    if (fails) { printf("material_rules: %d FAILED\n", fails); return 1; }
    printf("material_rules: all checks passed\n");
    return 0;
}
