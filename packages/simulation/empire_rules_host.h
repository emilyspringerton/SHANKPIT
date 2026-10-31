/* empire_rules_host.h -- the EMPIRE influence economy and sphere geometry, from PARENA.
 *
 * The rules live in PARENA/stdlib/shankpit/empire_rules.prn, generated into empire_rules.c (do not
 * edit it; `make regen-empire-rules`). A host that plays EMPIRE (apps/lobby) includes this header
 * and links empire_rules.c. These are pure queries -- there is no rules table to install, because
 * nothing here is tunable at runtime: influence is economy, not feel. */
#ifndef SHANKPIT_EMPIRE_RULES_HOST_H
#define SHANKPIT_EMPIRE_RULES_HOST_H

/* PARENA-generated (empire_rules.c) */
int empire_influence_start(void);
int empire_influence_max(void);
int empire_influence_per_second(void);
int empire_troop_kind_count(void);
int empire_troop_cost(int kind);
int empire_clone_cost_milli(int kind);
int empire_clone_cost(int kind);
int empire_squad_cap(void);
int empire_spawn_radius_milli(void);
int empire_can_spawn(int influence, int kind, int live);
int empire_clone_can(int influence, int kind, int live, int veterans);
int empire_influence_from_district(int state, int base);
int empire_influence_per_banked_unit(void);
int empire_bank_credit(int carrying, int influence, int cap);
int empire_bleedout_ms(void);
int empire_revive_radius_milli(void);
int empire_revive_hold_ms(void);
int empire_revive_health_milli(void);
int empire_downable(int archetype);
int empire_bled_out(int downed_ms);
int empire_revive_holds(int held_ms);
int empire_district_state(int your_pressure_milli, int their_pressure_milli);
int empire_district_threshold_milli(void);
int empire_sphere_rings(void);
int empire_districts_in_ring(int ring);
int empire_sphere_radius_milli(void);
int empire_district_radius_milli(void);
int empire_nearby_districts(void);

#endif /* SHANKPIT_EMPIRE_RULES_HOST_H */
