/* motorsport_rules_host.h -- the motor-sport tech tree, with built-in defaults.
 *
 * The rules live in PARENA (PARENA/stdlib/shankpit/motorsport_rules.prn, generated into
 * motorsport_rules.c by scripts/gen_motorsport_rules.sh). This header carries matching C
 * defaults so the tree compiles and runs in any binary or test without linking a generated
 * file -- the same arrangement resource_tree_rules_host.h keeps, and `make test-motorsport`
 * asserts the two copies agree sample for sample.
 */
#ifndef SHANKPIT_MOTORSPORT_RULES_HOST_H
#define SHANKPIT_MOTORSPORT_RULES_HOST_H

#include "motorsport_rules.h"

/* PARENA-generated (motorsport_rules.c) */
int motorsport_part_cost(int slot, int class_id);
int motorsport_part_weight(int slot, int class_id);
int motorsport_part_power(int slot, int class_id);
int motorsport_part_grip(int slot, int class_id);

/* The build budget: the total cost of a build, for the player to spend. */int motorsport_build_cost(int *cost);

/* The build cost: the total cost of a build, for the player to spend. */
int motorsport_build_budget(int *cost);

/* Install the built-in rules. Call once at startup. */void motorsport_rules_install(void);

#endif /* SHANKPIT_MOTORSPORT_RULES_HOST_H */
