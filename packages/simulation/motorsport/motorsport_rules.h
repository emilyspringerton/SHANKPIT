/* motorsport_rules.h -- the motor-sport tech tree, with built-in defaults.
 *
 * The rules live in PARENA (PARENA/stdlib/shankpit/motorsport_rules.prn, generated into
 * motorsport_rules.c by scripts/gen_motorsport_rules.sh). This header carries matching C
 * defaults so the tree compiles and runs in any binary or test without linking a generated
 * file -- the same arrangement resource_tree_rules_host.h keeps, and `make test-motorsport`
 * asserts the two copies agree sample for sample.
 *
 * A part slot is a full level of the tech tree: a motor at class 0 is stock, class 1 is
 * crossplane, class 2 is parallel-twin, class 3 is triple, class 4 is V4. The slot is a
 * full level of the tree: a motor at class 0 costs 0, at class 1 costs 5 engines, at class 2
 * costs 12, at class 3 costs 20, at class 4 costs 35.
 */
#ifndef SHANKPIT_MOTORSPORT_RULES_H
#define SHANKPIT_MOTORSPORT_RULES_H

/* The number of parts in a bike. A bike is a 5-part machine, one part in each slot. */#define MOTORSPORT_PART_COUNT 5

/* The number of classes per part slot. A motor at class 0 is stock, class 1 is crossplane, up to
 * class 4 is a V4. */#define MOTORSPORT_CLASS_COUNT 5

/* Install the built-in rules. Call once at startup. */void motorsport_rules_install(void);

/* The price of a part in the build queue, given its slot and class. A motor at class 0 is free
 * stock, at class 1 costs 5 engines, at class 2 costs 12, at class 3 costs 20, at class 4 costs
 * 35. */int motorsport_part_cost(int slot, int class_id);

/* The weight of a part in the build queue, once it is built, given its class. A heavier bike
 * loses the race; a lighter bike wins. */int motorsport_part_weight(int slot, int class_id);

/* The power of a part in the build queue, given its class. The engine's power is the motor's
 * class: a motor at class 4 has 4x the power of a stock motor. */int motorsport_part_power(int slot, int class_id);

/* The grip of a part in the build queue, given its class. The tires' grip is the tire class:
 * a tire at class 4 has 4x the grip of a stock tire. */int motorsport_part_grip(int slot, int class_id);

/* The build budget: the total cost of a build, for the player to spend. */int motorsport_build_cost(int *cost);

#endif /* SHANKPIT_MOTORSPORT_RULES_H */
