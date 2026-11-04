/* horse_rules_host.h -- installs the PARENA horse rules into local_game.h's tölt tick.
 * The rules live in PARENA/stdlib/shankpit/horse_rules.prn, generated into horse_rules.c (do not edit
 * it; `make regen-horse-rules`). A host that simulates or rides horses (apps/server, apps/lobby)
 * includes this header, links horse_rules.c, and calls horse_rules_install() once at startup.
 * Translation units that only include local_game.h run the built-in defaults instead. */
#ifndef HORSE_RULES_HOST_H
#define HORSE_RULES_HOST_H

#include "horse_rules.h"

/* PARENA-generated (horse_rules.c) */
int horse_max_speed_milli(void);
int horse_tolt_drain_milli(int speed_milli);
int horse_stamina_regen_milli(void);
int horse_jump_cost_milli(void);
int horse_jump_speed_milli(void);
int horse_carrot_restore_milli(void);
int horse_fatigue_permille(int stamina_milli);
int horse_can_jump(int stamina_milli, int grounded);

static const HorseRules g_horse_rules_parena = {
    horse_max_speed_milli,
    horse_tolt_drain_milli,
    horse_stamina_regen_milli,
    horse_jump_cost_milli,
    horse_jump_speed_milli,
    horse_carrot_restore_milli,
    horse_fatigue_permille,
    horse_can_jump,
};

static inline void horse_rules_install(void) { phys_set_horse_rules(&g_horse_rules_parena); }

#endif /* HORSE_RULES_HOST_H */
