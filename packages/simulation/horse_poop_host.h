/* horse_poop_host.h -- installs the PARENA horse poop rules into local_game.h's horse tick. The
 * rules live in PARENA/stdlib/shankpit/horse_poop_rules.prn, generated into horse_poop_rules.c (do
 * not edit it; `make regen-horse-poop-rules`). A host that simulates horses (apps/server, apps/lobby)
 * includes this header, links horse_poop_rules.c, and calls horse_poop_install() once at startup.
 * Translation units that only include local_game.h use horse_poop.h's built-in defaults instead. */
#ifndef HORSE_POOP_HOST_H
#define HORSE_POOP_HOST_H

#include "horse_poop.h"

/* PARENA-generated (horse_poop_rules.c) */
int horse_poop_interval_ms(int goal);
int horse_poop_lifetime_ms(void);
int horse_poop_max_piles(void);
int horse_poop_behind_milli(void);
int horse_poop_spread_milli(void);
int horse_poop_after_carrot_ms(void);

static const HorsePoop g_horse_poop_parena = {
    horse_poop_interval_ms,
    horse_poop_lifetime_ms,
    horse_poop_max_piles,
    horse_poop_behind_milli,
    horse_poop_spread_milli,
    horse_poop_after_carrot_ms,
};

static inline void horse_poop_install(void) { phys_set_horse_poop(&g_horse_poop_parena); }

#endif /* HORSE_POOP_HOST_H */
