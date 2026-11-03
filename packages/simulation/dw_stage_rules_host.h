#ifndef DW_STAGE_RULES_HOST_H
#define DW_STAGE_RULES_HOST_H

/* dw_stage_rules_host.h -- prototypes for the PARENA-generated bracket arithmetic
 * (PARENA/stdlib/deadweight/stage.prn -> packages/simulation/dw_stage_rules.c).
 *
 * Same no-shared-header-between-generated-units convention as dw_rules_host.h, dw_citizen_host.h and
 * dw_rating_host.h. Nothing in this file is hand-maintained logic, and NOOBTOPIA emits the same four
 * functions to Luau from the same source -- two stages in two worlds must pair a field identically. */

int stage_slots(int n);
int stage_rounds(int slots);
int stage_opponent(int slots, int i);
int stage_higher_seed(int rank_a, int rank_b);

#endif
