/* garage_rules_test.c -- `make test-garage-rules`.
 *
 * The garage's two decisions are hand-checked here, and the generated PARENA table is compared against
 * garage.h's built-in defaults for EVERY (origin, objective) pair: if those two drift, the game and this
 * test are selling different cars.
 */
#include <stdio.h>

#include "garage.h"
#include "garage_rules_host.h"

static int fails = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { printf("FAIL %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } \
} while (0)

int main(void) {
    /* the hand-derived table, before any rules are installed (the built-in defaults) */
    CHECK(garage_accepts_for(GARAGE_ORIGIN_CIVILIAN, GARAGE_OBJECTIVE_ANY) == 1, "a bay with no job takes anything");
    CHECK(garage_accepts_for(GARAGE_ORIGIN_MILITARY, GARAGE_OBJECTIVE_ANY) == 1, "ANY should take a military hulk");
    CHECK(garage_accepts_for(GARAGE_ORIGIN_CIVILIAN, GARAGE_OBJECTIVE_REBUILD) == 1, "REBUILD takes a civilian car");
    CHECK(garage_accepts_for(GARAGE_ORIGIN_STOLEN, GARAGE_OBJECTIVE_REBUILD) == 0, "REBUILD must refuse a stolen car");
    CHECK(garage_accepts_for(GARAGE_ORIGIN_STOLEN, GARAGE_OBJECTIVE_DISAPPEAR) == 1, "DISAPPEAR takes the stolen one");
    CHECK(garage_accepts_for(GARAGE_ORIGIN_CIVILIAN, GARAGE_OBJECTIVE_DISAPPEAR) == 0, "DISAPPEAR refuses a clean car");
    CHECK(garage_accepts_for(GARAGE_ORIGIN_STOLEN, GARAGE_OBJECTIVE_FENCE) == 1, "FENCE takes a stolen car");
    CHECK(garage_accepts_for(GARAGE_ORIGIN_MILITARY, GARAGE_OBJECTIVE_FENCE) == 1, "FENCE takes a military hulk");
    CHECK(garage_accepts_for(GARAGE_ORIGIN_CIVILIAN, GARAGE_OBJECTIVE_FENCE) == 0, "FENCE refuses a civilian car");
    CHECK(garage_accepts_for(GARAGE_ORIGIN_CIVILIAN, 99) == 0, "an unknown objective is refused");
    CHECK(garage_accepts_for(-1, GARAGE_OBJECTIVE_ANY) == 1, "ANY is total: an unknown origin still gets in");

    CHECK(garage_pays_for(GARAGE_ORIGIN_CIVILIAN) == 1000, "civilian pays full");
    CHECK(garage_pays_for(GARAGE_ORIGIN_STOLEN) == 700, "stolen pays less (no papers)");
    CHECK(garage_pays_for(GARAGE_ORIGIN_MILITARY) == 1300, "military pays more (the parts)");
    CHECK(garage_pays_for(7) == 0, "an undefined origin pays nothing");
    CHECK(garage_value(300, GARAGE_ORIGIN_CIVILIAN) == 300, "a full civilian car is worth its hull");
    CHECK(garage_value(300, GARAGE_ORIGIN_MILITARY) == 390, "military 1300 permille of 300");
    CHECK(garage_value(200, GARAGE_ORIGIN_STOLEN) == 140, "stolen 700 permille of 200");
    CHECK(garage_value(-5, GARAGE_ORIGIN_CIVILIAN) == 0, "negative hull is worth nothing");

    /* now the real table, and EVERY pair against the defaults */
    garage_rules_install();
    for (int origin = -1; origin <= GARAGE_ORIGIN_COUNT + 1; origin++)
        for (int objective = -1; objective <= GARAGE_OBJECTIVE_COUNT + 1; objective++)
            CHECK(garage_accepts_for(origin, objective) == garage_def_accepts(origin, objective),
                  "PARENA accepts(%d,%d) = %d, header says %d", origin, objective,
                  garage_accepts_for(origin, objective), garage_def_accepts(origin, objective));
    for (int origin = -1; origin <= GARAGE_ORIGIN_COUNT + 1; origin++)
        CHECK(garage_pays_for(origin) == garage_def_pays(origin),
              "PARENA pays(%d) = %d, header says %d", origin, garage_pays_for(origin), garage_def_pays(origin));

    if (fails) { printf("garage: %d FAILED\n", fails); return 1; }
    printf("garage: all checks passed\n");
    return 0;
}
