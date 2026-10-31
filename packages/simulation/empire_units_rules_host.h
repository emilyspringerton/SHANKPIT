/* empire_rules_host.h -- installs PARENA's EMPIRE rules into empire_units_rules.h.
 * The rules live in PARENA/stdlib/shankpit/empire_rules.prn, generated into empire_units_rules.c (do not edit
 * it; `make regen-empire-rules`). A host that spawns units (apps/lobby, apps/server) includes this header,
 * links empire_units_rules.c, and calls eu_rules_install() once at startup. With nothing installed
 * empire_units_rules.h's own defaults are used, so tests keep their link lines. */
#ifndef SHANKPIT_EMPIRE_UNITS_RULES_HOST_H
#define SHANKPIT_EMPIRE_UNITS_RULES_HOST_H

#include "empire_units_rules.h"

/* PARENA-generated (empire_units_rules.c) */
int eu_archetype_count(void);
int eu_archetype_species(int arch);
int eu_archetype_role(int arch);
int eu_archetype_kit(int arch);
int eu_archetype_faction(int arch);
int eu_archetype_orders(int arch);
int eu_archetype_health(int arch);
int eu_human_courage(void);
int eu_human_aggression(void);
int eu_human_loyalty(void);
int eu_human_greed(void);
int eu_human_curiosity(void);
int eu_beast_courage(void);
int eu_beast_aggression(void);
int eu_beast_loyalty(void);
int eu_beast_greed(void);
int eu_beast_curiosity(void);
int eu_order_follow(void);
int eu_order_explore(void);
int eu_order_engage(void);
int eu_order_gather(void);
int eu_order_claim(void);
int eu_order_guard(void);
int eu_order_priority(int arch, int order);
int eu_claim_ticks(void);
int eu_claim_radius(void);
int eu_gather_radius(void);
int eu_node_capacity(void);
int eu_explore_radius(void);
int eu_follow_distance(void);

static const EUUnitRules g_eu_rules_parena = {
    eu_archetype_count, eu_archetype_species, eu_archetype_role, eu_archetype_kit,
    eu_archetype_faction, eu_archetype_orders, eu_archetype_health,
    eu_human_courage, eu_human_aggression, eu_human_loyalty, eu_human_greed, eu_human_curiosity,
    eu_beast_courage, eu_beast_aggression, eu_beast_loyalty, eu_beast_greed, eu_beast_curiosity,
    eu_order_follow, eu_order_explore, eu_order_engage, eu_order_gather, eu_order_claim,
    eu_order_guard, eu_order_priority,
    eu_claim_ticks, eu_claim_radius, eu_gather_radius, eu_node_capacity,
    eu_explore_radius, eu_follow_distance
};

static inline void eu_rules_install(void) { eu_rules_set(&g_eu_rules_parena); }

#endif /* SHANKPIT_EMPIRE_UNITS_RULES_HOST_H */
