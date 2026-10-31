/* drone_rules_host.h -- installs the PARENA quadcopter models into drone_flight.h.
 * The rules live in PARENA/stdlib/shankpit/drone_rules.prn, generated into drone_rules.c (do not edit
 * it; `make regen-drone-rules`). A host that simulates drones (apps/lobby, apps/server) includes this
 * header, links drone_rules.c, and calls drone_rules_install() once at startup. With nothing installed
 * drone_flight.h's own built-in defaults are used, so tests that just include it are unchanged. */
#ifndef SHANKPIT_DRONE_RULES_HOST_H
#define SHANKPIT_DRONE_RULES_HOST_H

#include "drone_flight.h"

/* PARENA-generated (drone_rules.c) */
int drone_mass_grams(int kind);
int drone_response_permille(int kind);
int drone_top_speed_milli(int kind);
int drone_climb_milli(int kind);
int drone_yaw_rate_milli(int kind);
int drone_bounce_permille(int kind);
int drone_hull(int kind);
int drone_drift_permille(int kind, int storm);
int drone_shard_kills(int kind, int mass_grams, int rel_speed_milli);
int drone_gimbal_yaw_milli(int kind);
int drone_gimbal_pitch_milli(int kind);

static const DroneRules g_drone_rules_parena = {
    drone_mass_grams, drone_response_permille, drone_top_speed_milli, drone_climb_milli,
    drone_yaw_rate_milli, drone_drift_permille, drone_bounce_permille, drone_hull,
    drone_shard_kills, drone_gimbal_yaw_milli, drone_gimbal_pitch_milli,
};

static inline void drone_rules_install(void) { drone_set_rules(&g_drone_rules_parena); }

#endif /* SHANKPIT_DRONE_RULES_HOST_H */
