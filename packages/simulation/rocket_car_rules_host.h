/* rocket_car_rules_host.h -- installs the PARENA rocket-car model into physics.h.
 * The rules live in PARENA/stdlib/shankpit/rocket_car_rules.prn (ported from Bedrock Racers'
 * br_phys.prn air-tank economy), generated into rocket_car_rules.c (do not edit it;
 * `make regen-rocket-car-rules`). A host that can drive one (apps/lobby, apps/server) includes this
 * header, links rocket_car_rules.c, and calls rocket_car_rules_install() once at startup. With
 * nothing installed, physics.h's own built-in rocket defaults are used, so every existing test that
 * just includes physics.h keeps working unchanged. */
#ifndef SHANKPIT_ROCKET_CAR_RULES_HOST_H
#define SHANKPIT_ROCKET_CAR_RULES_HOST_H

#include "physics.h"

/* PARENA-generated (rocket_car_rules.c) */
int rocket_top_speed_milli(void);
int rocket_reverse_top_speed_milli(void);
int rocket_drive_permille(void);
int rocket_tank_max(void);
int rocket_lift_spend(void);
int rocket_boost_spend(void);
int rocket_refill_spend(void);
int rocket_surface_refill_spend(void);
int rocket_hover_height_milli(void);
int rocket_boost_permille(int tank);
int rocket_hover_accel_milli(int err_milli);
int rocket_tank_step(int tank, int spend);
int rocket_tank_fill(int tank, int gain);

static const PhysRocketRules g_phys_rocket_rules_parena = {
    rocket_top_speed_milli, rocket_reverse_top_speed_milli, rocket_drive_permille,
    rocket_tank_max, rocket_lift_spend, rocket_boost_spend, rocket_refill_spend,
    rocket_surface_refill_spend, rocket_hover_height_milli, rocket_boost_permille,
    rocket_hover_accel_milli, rocket_tank_step, rocket_tank_fill,
};

static inline void rocket_car_rules_install(void) { phys_set_rocket_rules(&g_phys_rocket_rules_parena); }

#endif /* SHANKPIT_ROCKET_CAR_RULES_HOST_H */
