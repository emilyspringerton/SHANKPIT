/* buggy_rules_host.h -- installs the PARENA buggy handling rules (#468) into physics.h.
 * The rules live in PARENA/stdlib/shankpit/buggy_rules.prn, generated into buggy_rules.c (do not edit it;
 * `make regen-buggy-rules`). A host that drives or simulates buggies (apps/server, apps/lobby) includes
 * this header, links buggy_rules.c, and calls buggy_rules_install() once at startup. Translation units
 * that only include physics.h (most tests) run the built-in constants instead. */
#ifndef BUGGY_RULES_HOST_H
#define BUGGY_RULES_HOST_H

#include "physics.h"

/* PARENA-generated (buggy_rules.c) */
int buggy_top_speed_milli(void);
int buggy_reverse_top_speed_milli(void);
int buggy_drive_force_permille(int);
int buggy_turn_rate_milli(int);
int buggy_steer_authority_permille(int);
int buggy_lateral_grip_permille(int);

static const PhysBuggyRules g_buggy_rules_parena = {
    buggy_top_speed_milli,
    buggy_reverse_top_speed_milli,
    buggy_drive_force_permille,
    buggy_turn_rate_milli,
    buggy_steer_authority_permille,
    buggy_lateral_grip_permille,
};

static inline void buggy_rules_install(void) { phys_set_buggy_rules(&g_buggy_rules_parena); }

#endif
