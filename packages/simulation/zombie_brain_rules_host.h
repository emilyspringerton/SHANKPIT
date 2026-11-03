#ifndef ZOMBIE_BRAIN_RULES_HOST_H
#define ZOMBIE_BRAIN_RULES_HOST_H

/* zombie_brain_rules_host.h -- prototypes for the PARENA-generated per-class brain shaping
 * (PARENA/stdlib/shankpit/zombie_brain_rules.prn -> packages/simulation/zombie_brain_rules.c).
 *
 * Same convention as ai_brain_rules.c / zombies_pheromone_rules.c: the generated unit carries its own
 * prototypes and no shared header is maintained between them; this file only declares what THIS module
 * calls. Nothing here is hand-maintained logic. */

int zb_distance_bias(int cls);
int zb_health_bias(int cls);
int zb_turn_cap(int cls);
int zb_attack_threshold(int cls);
int zb_phase_milli(int cls, int level);

#endif
