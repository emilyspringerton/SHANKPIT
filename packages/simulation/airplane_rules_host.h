/* airplane_rules_host.h -- installs the PARENA fixed-wing model into airplane_flight.h.
 * Rules live in PARENA/stdlib/shankpit/airplane_rules.prn, generated into airplane_rules.c (do not edit
 * it; `make regen-airplane-rules`). A host that flies one (apps/lobby, apps/server) includes this header,
 * links airplane_rules.c, and calls airplane_rules_install() once at startup. With nothing installed,
 * airplane_flight.h's own built-in defaults are used, so tests that just include it are unchanged. */
#ifndef SHANKPIT_AIRPLANE_RULES_HOST_H
#define SHANKPIT_AIRPLANE_RULES_HOST_H

#include "airplane_flight.h"

/* PARENA-generated (airplane_rules.c) */
int airplane_top_speed_milli(void);
int airplane_stall_milli(int flaps);
int airplane_liftoff_milli(void);
int airplane_thrust_milli(int throttle);
int airplane_drag_milli(int speed_milli, int flaps);
int airplane_lift_permille(int speed_milli, int flaps, int aoa_permille);
int airplane_hull(void);

static const AirplaneRules g_airplane_rules_parena = {
    airplane_top_speed_milli, airplane_stall_milli, airplane_liftoff_milli, airplane_thrust_milli,
    airplane_drag_milli, airplane_lift_permille, airplane_hull,
};

static inline void airplane_rules_install(void) { airplane_set_rules(&g_airplane_rules_parena); }

#endif /* SHANKPIT_AIRPLANE_RULES_HOST_H */
