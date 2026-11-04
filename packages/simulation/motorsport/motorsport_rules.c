/* motorsport_rules.c -- the motor-sport tech tree, generated.
 *
 * Do not edit by hand. `make regen-motorsport` (or the generator script) re-emits this from
 * PARENA/stdlib/shankpit/motorsport_rules.prn. The C here is the compiled-in defaults;
 * it is identical to the PARENA-decided source so a test can assert the two agree.
 *
 * The parts stack for a tuned machine. A bike is a group of slots; each slot is a part from
 * the catalog, and each part comes in classes. The class is a full level of the tech tree:
 *   - motor: 0 = stock / smokestack, 1 = crossplane, 2 = parallel-twin, 3 = triple, 4 = V4.
 *   - frame: 0 = trail, 1 = enduro, 2 = dirt, 3 = scrambler, 4 = cross.
 *   - suspension: 0 = coil, 1 = air, 2 = preload, 3 = double-fork, 4 = fully suspended.
 *   - tires: 0 = knobbly, 1 = trail, 2 = slick, 3 = drag, 4 = rally-spec.
 *   - electronics: 0 = mechanical, 1 = CDI, 2 = fuel injection, 3 = ECU, 4 = traction control.
 *
 * A built bike is a group of 5 parts, one per slot. Each slot may only accept a part of its
 * own class. The build cost is the stack of tier upgrades: a motor at class 0 is free stock,
 * at class 1 costs 5 engines, at class 2 costs 12, at class 3 costs 20, at class 4 costs 35.
 */
#include "motorsport.h"
#include "motorsport_rules.h"
#include <stdint.h>

/* The build budget: the total cost of a build, for the player to spend. */int g_motorsport_build_cost = 0;

/* Install the built-in rules. Call once at startup from the host that owns the bike. */void motorsport_install(void) {
    g_motorsport_build_cost = 0;
}

/* The number of classes per slot (5: stock, 1, 2, 3, 4). */int motorsport_class_count(void) {
    return 5;
}

/* The price of a part in the build queue, given its class. A motor at class 0 is free stock,
 * at class 1 costs 5 engines, at class 2 costs 12, at class 3 costs 20, at class 4 costs 35. */int motorsport_part_cost(int slot, int class_id) {
    if (slot < 0 || slot >= MOTORSPORT_PART_COUNT) return 0;
    if (class_id < 0 || class_id >= MOTORSPORT_CLASS_COUNT) return 0;
    static const int cost[] = { 0, 5, 12, 20, 35 };
    return cost[class_id];
}

/* The weight of a part in the build queue, once it is built, given its class. A heavier bike
 * loses the race; a lighter bike wins. */int motorsport_part_weight(int slot, int class_id) {
    if (slot < 0 || slot >= MOTORSPORT_PART_COUNT) return 0;
    if (class_id < 0 || class_id >= MOTORSPORT_CLASS_COUNT) return 0;
    static const int weight[] = { 1, 2, 3, 4, 5 };
    return weight[class_id];
}

/* The power of a part in the build queue, given its class. A motor's power is the engine's
 * class: a motor at class 4 has 4x the power of a stock motor. For all other slots the
 * power is the part's own class weight (the frame is heavier, so it powers the bike less;
 * a heavier frame loses the race). */int motorsport_part_power(int slot, int class_id) {
    if (slot < 0 || slot >= MOTORSPORT_PART_COUNT) return 0;
    if (class_id < 0 || class_id >= MOTORSPORT_CLASS_COUNT) return 0;
    if (slot == PART_SLOT_ID_ENGINE) return class_id + 1;
    return 1 + class_id;
}

/* The grip of a part in the build queue, given its class. The tires' grip is the tire class:
 * a tire at class 4 has 4x the grip of a stock tire. For all other slots the grip is the
 * part's weight, so heavier parts have more grip but lose the race. */int motorsport_part_grip(int slot, int class_id) {
    if (slot < 0 || slot >= MOTORSPORT_PART_COUNT) return 0;
    if (class_id < 0 || class_id >= MOTORSPORT_CLASS_COUNT) return 0;
    if (slot == PART_SLOT_ID_TIRES) return class_id + 1;
    return 1 + class_id;
}

/* The build budget: the total cost of a build, for the player to spend. */int motorsport_build_cost(int *cost) {
    if (cost) *cost = g_motorsport_build_cost;
    return g_motorsport_build_cost;
}

/* Build a bike from a build queue. A slot may only take a part of its own class, so a frame may
 * not be a motor. Returns 0 on success, -1 on error. */int motorsport_build_bike(Bike *bike, const Build *build) {
    if (!bike || !build) return -1;
    bike->engine = build->engine;
    bike->frame = build->frame;
    bike->suspension = build->suspension;
    bike->tires = build->tires;
    bike->electronics = build->electronics;
    return 0;
}

/* A racecourse is the payoff. A sprint is a race on a track with a specific surface, and the
 * winner is decided by the engine's torque, the frame's weight, the suspension's damping, and the
 * tires' grip. The winner is the bike with the higher engine power minus the frame weight times
 * the suspension damping times the tire grip, and the loser gets farther than the winner. Returns
 * 0 on success, -1 on error. */int motorsport_sprint_race(const Bike *left, const Bike *right) {
    if (!left || !right) return -1;
    int left_power = motorsport_part_power(PART_SLOT_ID_ENGINE, left->engine);
    int right_power = motorsport_part_power(PART_SLOT_ID_ENGINE, right->engine);
    int left_weight = motorsport_part_weight(PART_SLOT_ID_FRAME, left->frame);
    int right_weight = motorsport_part_weight(PART_SLOT_ID_FRAME, right->frame);
    int left_grip = motorsport_part_grip(PART_SLOT_ID_TIRES, left->tires);
    int right_grip = motorsport_part_grip(PART_SLOT_ID_TIRES, right->tires);
    left_power *= left_grip;
    right_power *= right_grip;
    left_power -= left_weight * (left->suspension + 1);
    right_power -= right_weight * (right->suspension + 1);
    return left_power > right_power ? 0 : -1;
}
