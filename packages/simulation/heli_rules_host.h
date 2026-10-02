/* heli_rules_host.h -- installs the PARENA helicopter flight model (#542) into net_sim.h.
 * The rules live in PARENA/stdlib/shankpit/heli_rules.prn, generated into heli_rules.c (do not edit it;
 * `make regen-heli-rules`). A host that simulates helicopters (apps/server, apps/lobby) includes this
 * header, links heli_rules.c, and calls heli_rules_install() once at startup. */
#ifndef HELI_RULES_HOST_H
#define HELI_RULES_HOST_H

#include "net_sim.h"

/* PARENA-generated (heli_rules.c) */
int heli_hover_collective_permille(void);
int heli_collective_step_permille(int, int, int);
int heli_lift_permille(int);
int heli_cyclic_target_milli(int);
int heli_attitude_step_milli(int, int);
int heli_yaw_target_milli(int);
int heli_yaw_step_milli(int, int);
int heli_drag_permille(int);
int heli_vertical_damp_permille(void);
int heli_max_hspeed_milli(void);
int heli_max_vspeed_up_milli(void);
int heli_max_vspeed_down_milli(void);

static const HeliRules g_heli_rules_parena = {
    heli_hover_collective_permille, heli_collective_step_permille, heli_lift_permille,
    heli_cyclic_target_milli, heli_attitude_step_milli, heli_yaw_target_milli, heli_yaw_step_milli,
    heli_drag_permille, heli_vertical_damp_permille, heli_max_hspeed_milli,
    heli_max_vspeed_up_milli, heli_max_vspeed_down_milli,
};

static inline void heli_rules_install(void) { heli_set_rules(&g_heli_rules_parena); }

#endif
