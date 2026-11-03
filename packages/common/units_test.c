/* units_test.c -- the generated unit layer, checked against the factors a human would look up.
 * make test-units. A tolerance of 1e-9 relative is used where a value is exact by definition and
 * 1e-6 absolute on volumes, which are the products of three converted lengths. */
#include "units_rules.h"
#include <math.h>
#include <stdio.h>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("units: FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
static int near(double a, double b, double eps) { return fabs(a - b) <= eps; }

int main(void) {
    int u;

    /* ---- the definitions themselves ---- */
    CHECK(near(units_length_mm(1.0, 3), 25.4, 1e-12), "1 inch is %f mm", units_length_mm(1.0, 3));
    CHECK(near(units_length_mm(1.0, 4), 304.8, 1e-12), "1 foot is %f mm", units_length_mm(1.0, 4));
    CHECK(near(units_length_mm(1.0, 5), 914.4, 1e-12), "1 yard is %f mm", units_length_mm(1.0, 5));
    CHECK(near(units_length_mm(2.54, 1), 25.4, 1e-12), "2.54 cm is %f mm", units_length_mm(2.54, 1));
    CHECK(near(units_mass_mg(1.0, 3), 453592.37, 1e-6), "1 lb is %f mg", units_mass_mg(1.0, 3));
    CHECK(near(units_mass_mg(1.0, 2), 28349.523125, 1e-6), "1 oz is %f mg", units_mass_mg(1.0, 2));
    CHECK(near(units_mass_kg(1.0, 4), 907.18474, 1e-9), "1 short ton is %f kg", units_mass_kg(1.0, 4));
    CHECK(near(units_volume_m3(1.0, 5), 0.003785411784, 1e-15), "1 US gallon is %f m3", units_volume_m3(1.0, 5));
    CHECK(near(units_volume_m3(1.0, 3), 0.000016387064, 1e-18), "1 cu in is %f m3", units_volume_m3(1.0, 3));

    /* ---- metric <-> imperial through the one conversion call ---- */
    CHECK(near(units_convert(1.0, 0, 3, 1), 2.54, 1e-12), "1 in in cm is %f", units_convert(1.0, 0, 3, 1));
    CHECK(near(units_convert(1.0, 0, 2, 4), 3.28083989501312, 1e-9), "1 m in ft is %f", units_convert(1.0, 0, 2, 4));
    CHECK(near(units_convert(1.0, 1, 3, 1), 0.45359237, 1e-12), "1 lb in kg is %f", units_convert(1.0, 1, 3, 1));
    CHECK(near(units_convert(2.0, 1, 1, 3), 4.40924524369755, 1e-9), "2 kg in lb is %f", units_convert(2.0, 1, 1, 3));
    CHECK(near(units_convert(1.0, 2, 5, 1), 3.785411784, 1e-12), "1 gal in L is %f", units_convert(1.0, 2, 5, 1));
    CHECK(near(units_convert(1.0, 2, 4, 1), 28.316846592, 1e-12), "1 cu ft in L is %f", units_convert(1.0, 2, 4, 1));

    /* ---- every code round-trips through the canonical unit ---- */
    for (u = 0; u <= 5; u++) {
        double x = 7.25;
        CHECK(near(units_convert(units_convert(x, 0, u, 0), 0, 0, u), x, 1e-9), "length code %d did not round-trip", u);
        CHECK(near(units_convert(units_convert(x, 2, u, 0), 2, 0, u), x, 1e-9), "volume code %d did not round-trip", u);
    }
    for (u = 0; u <= 4; u++) {
        double x = 2.5;
        CHECK(near(units_convert(units_convert(x, 1, u, 0), 1, 0, u), x, 1e-9), "mass code %d did not round-trip", u);
    }

    /* ---- codes are checked, not trusted ---- */
    CHECK(units_code_valid(0, 5) == 1 && units_code_valid(0, 6) == 0, "length code validity is wrong");
    CHECK(units_code_valid(1, 4) == 1 && units_code_valid(1, 5) == 0, "mass code validity is wrong");
    CHECK(units_code_valid(2, 5) == 1 && units_code_valid(2, -1) == 0, "volume code validity is wrong");
    CHECK(units_code_valid(3, 0) == 0, "an unknown family was accepted");
    CHECK(units_convert(1.0, 0, 6, 0) == 0.0 && units_convert(1.0, 0, 0, 9) == 0.0, "a bad code converted anyway");
    CHECK(units_box_volume_m3(1.0, 1.0, 1.0, 6) == 0.0, "a box with a bad unit produced a volume");

    /* ---- the compound affordances ---- */
    CHECK(near(units_box_volume_m3(2.0, 1.0, 0.5, 4), 0.028316846592, 1e-12), "2x1x0.5 ft is %f m3",
          units_box_volume_m3(2.0, 1.0, 0.5, 4));
    CHECK(near(units_box_volume_m3(100.0, 50.0, 20.0, 1), 0.1, 1e-12), "100x50x20 cm is %f m3",
          units_box_volume_m3(100.0, 50.0, 20.0, 1));
    CHECK(near(units_box_mass_kg(1.0, 2400.0), 2400.0, 1e-12), "1 m3 of concrete is not 2400 kg");
    CHECK(near(units_box_mass_kg(units_box_volume_m3(2.0, 1.0, 0.5, 4), 2400.0), 67.9604318208, 1e-9),
          "2x1x0.5 ft of concrete is %f kg", units_box_mass_kg(units_box_volume_m3(2.0, 1.0, 0.5, 4), 2400.0));

    /* ---- snapping, both grids, and the no-snap profile ---- */
    CHECK(units_snap(0.61, 0) == 0.61, "profile 0 moved a coordinate");
    CHECK(near(units_snap(0.61, 1), 0.8, 1e-12), "0.61 m on the 0.4 grid is %f", units_snap(0.61, 1));
    CHECK(near(units_snap(0.3, 3), 0.4064, 1e-12), "0.3 m on the 16-inch grid is %f", units_snap(0.3, 3));
    CHECK(near(units_snap(-0.5, 4), -0.4064, 1e-12), "negative snaps went the wrong way: %f", units_snap(-0.5, 4));
    CHECK(near(units_snap(0.0, 2), 0.0, 1e-12), "zero moved off zero");

    /* ---- authoring entry points: F32 in and out, the exact F64 core underneath ---- */
    CHECK(near((double)units_author_length_mm(12.0f, 4), 3657.6, 1e-2), "12 ft as F32 is %f mm",
          (double)units_author_length_mm(12.0f, 4));
    CHECK(near((double)units_author_length_m(12.0f, 4), 3.6576, 1e-5), "12 ft as F32 is %f m",
          (double)units_author_length_m(12.0f, 4));
    CHECK(near((double)units_author_box_m3(2.0f, 1.0f, 0.5f, 4), 0.028316846592, 1e-7),
          "2x1x0.5 ft as F32 is %f m3", (double)units_author_box_m3(2.0f, 1.0f, 0.5f, 4));
    CHECK(near((double)units_author_mass_kg(3.0f, 3), 1.36077711, 1e-6), "3 lb as F32 is %f kg",
          (double)units_author_mass_kg(3.0f, 3));
    CHECK(near((double)units_author_snap(0.61f, 1), 0.8, 1e-6), "an F32 snap did not land on the grid");
    CHECK(units_author_length_mm(1.0f, 9) == 0.0f, "a bad authoring code did not return 0");

    if (fails) { printf("units: %d FAILED\n", fails); return 1; }
    printf("units: all checks passed\n");
    return 0;
}
