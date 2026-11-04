/* citizen_actor_rules_host.h -- installs PARENA's citizen actor rules into citizen_actor_rules.h.
 * The rules live in PARENA/stdlib/shankpit/citizen_actor_rules.prn, generated into
 * citizen_actor_rules.c (do not edit it; `make regen-citizen-actor-rules`). A host that simulates
 * citizens (apps/lobby, apps/server) includes this header, links citizen_actor_rules.c and calls
 * citizen_actor_rules_install() once at startup. With nothing installed, citizen_actor_rules.h's
 * built-in defaults are used, so a test that never installs the mod is unchanged. */
#ifndef CITIZEN_ACTOR_RULES_HOST_H
#define CITIZEN_ACTOR_RULES_HOST_H

#include "citizen_actor_rules.h"

/* PARENA-generated (citizen_actor_rules.c) */
int citizen_jump_vy_milli(void);
int citizen_jump_cooldown_ms(void);
int citizen_crouch_speed_milli(void);
int citizen_slide_speed_milli(void);
int citizen_slide_min_ms(void);
int citizen_slide_max_ms(void);
int citizen_slide_jump_boost_milli(void);
int citizen_slide_jump_window_ms(void);
int citizen_wield_range_milli(void);
int citizen_fire_range_milli(void);
int citizen_fire_cooldown_ms(void);
int citizen_heuristic_score(int action, int dist_milli, int threat, int health_pm, int speed_milli, int ready);
int citizen_policy_aggression(int proximity, int hurt, int threat, int ready, int speed, int ground);
int citizen_blended_score(int action, int dist_milli, int threat, int health_pm, int speed_milli,
                          int ready, int aggression);
int citizen_pick(int dist_milli, int threat, int health_pm, int speed_milli, int ready, int aggression);

static const CitizenActorRules g_citizen_actor_rules_parena = {
    citizen_jump_vy_milli,
    citizen_jump_cooldown_ms,
    citizen_crouch_speed_milli,
    citizen_slide_speed_milli,
    citizen_slide_min_ms,
    citizen_slide_max_ms,
    citizen_slide_jump_boost_milli,
    citizen_slide_jump_window_ms,
    citizen_wield_range_milli,
    citizen_fire_range_milli,
    citizen_fire_cooldown_ms,
    citizen_heuristic_score,
    citizen_policy_aggression,
    citizen_blended_score,
    citizen_pick,
};

static inline void citizen_actor_rules_install(void) { citizen_actor_set_rules(&g_citizen_actor_rules_parena); }

#endif /* CITIZEN_ACTOR_RULES_HOST_H */
