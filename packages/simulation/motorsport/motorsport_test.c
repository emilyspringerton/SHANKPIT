/* motorsport_test.c -- the motor-sport tech tree, headless.
 *
 * What this proves: the parts stack for a tuned machine, the tech tree, and the sprints.
 * A bike is a group of 5 parts, one per slot. The tech tree is the stack of tier upgrades:
 * a motor at class 0 is free stock, at class 1 costs 5 engines, at class 2 costs 12, at
 * class 3 costs 20, at class 4 costs 35.
 */
#include <stdio.h>
#include <string.h>

#include "motorsport.h"

static int g_fail = 0;
static int g_checks = 0;

#define CHECK(c, msg) do { \
    g_checks++; \
    if (!(c)) { g_fail++; printf("FAIL %s:%d  %s (%s)\n", __FILE__, __LINE__, #c, msg); } \
} while (0)

static void test_part_price(void) {
    /* motor class prices */
    CHECK(motorsport_part_cost(PART_SLOT_ID_ENGINE, 0) == 0, "engine stock is free");
    CHECK(motorsport_part_cost(PART_SLOT_ID_ENGINE, 1) == 5, "engine class 1 costs 5");
    CHECK(motorsport_part_cost(PART_SLOT_ID_ENGINE, 2) == 12, "engine class 2 costs 12");
    CHECK(motorsport_part_cost(PART_SLOT_ID_ENGINE, 3) == 20, "engine class 3 costs 20");
    CHECK(motorsport_part_cost(PART_SLOT_ID_ENGINE, 4) == 35, "engine class 4 costs 35");
}

static void test_part_weight(void) {
    CHECK(motorsport_part_weight(PART_SLOT_ID_FRAME, 0) == 1, "trail frame is lightest");
    CHECK(motorsport_part_weight(PART_SLOT_ID_FRAME, 4) == 5, "cross frame is heaviest");
}

static void test_part_power(void) {
    CHECK(motorsport_part_power(PART_SLOT_ID_ENGINE, 0) == 1, "stock motor has 1 power");
    CHECK(motorsport_part_power(PART_SLOT_ID_ENGINE, 4) == 5, "V4 motor has 5 power");
    CHECK(motorsport_part_power(PART_SLOT_ID_ELECTRONICS, 0) == 1, "mechanical electronics has 1 power");
}

static void test_part_grip(void) {
    CHECK(motorsport_part_grip(PART_SLOT_ID_TIRES, 0) == 1, "knobbly tires have 1 grip");
    CHECK(motorsport_part_grip(PART_SLOT_ID_TIRES, 4) == 5, "rally-spec tires have 5 grip");
}

static void test_build_budget(void) {
    int cost = 0;
    CHECK(motorsport_build_cost(&cost) == 0, "build budget returns 0 when nothing built");
}

static void test_build_bike(void) {
    Build b = { .engine = 2, .frame = 1, .suspension = 1, .tires = 2, .electronics = 1 };
    Bike bike;
    CHECK(motorsport_build_bike(&bike, &b) == 0, "build a bike from a build queue");
    CHECK(bike.engine == 2, "engine class 2");
    CHECK(bike.frame == 1, "frame class 1");
    CHECK(bike.tires == 2, "tire class 2");
}

static void test_sprint_race(void) {
    Bike a = { .engine = 4, .frame = 1, .suspension = 2, .tires = 2, .electronics = 1 };
    Bike b = { .engine = 0, .frame = 1, .suspension = 2, .tires = 2, .electronics = 1 };
    CHECK(motorsport_sprint_race(&a, &b) == 0, "a sprint race is decided between two bikes");
}

int main(void) {
    motorsport_install();

    printf("== part prices ==\n");
    test_part_price();
    printf("== part weight ==\n");
    test_part_weight();
    printf("== part power ==\n");
    test_part_power();
    printf("== part grip ==\n");
    test_part_grip();
    printf("== build budget ==\n");
    test_build_budget();
    printf("== build a bike ==\n");
    test_build_bike();
    printf("== sprint race ==\n");
    test_sprint_race();

    printf("\nmotorsport: %d passed, %d failed\n", g_checks - g_fail, g_fail);
    return g_fail ? 1 : 0;
}
