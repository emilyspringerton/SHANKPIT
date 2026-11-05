#ifndef SHAMBLER_MOD_H
#define SHAMBLER_MOD_H

/* shambler_mod.h -- the C names for packages/sim/shambler_mod.prn, generated into
 * packages/simulation/shambler_mod.c (`make regen-shambler-mod`).
 *
 * A mod is a set of pure scalar hooks (packages/sim/mod_abi.prn); this header is only the bridge
 * that lets a C host name them. Nothing here decides anything -- the numbers are the .prn's. */

int shambler_on_tick(int hp, int hp_max);
int shambler_on_hit_dealt(int target_shield);
int shambler_on_hit_taken(int hp, int hp_max);
int shambler_on_ko(void);

/* The regen curve: what tick `tick_index` is worth to a shambler. Flat 1 in the .prn today. */
int shambler_on_shield_regen(int tick_index);

#endif /* SHAMBLER_MOD_H */
