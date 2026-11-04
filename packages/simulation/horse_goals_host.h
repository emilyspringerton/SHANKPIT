/* horse_goals_host.h -- installs the PARENA horse goal decisions into local_game.h's loose-horse
 * tick. The decisions live in PARENA/stdlib/shankpit/horse_goals.prn, generated into
 * horse_goals.c (do not edit it; `make regen-horse-goals`). A host that simulates horses
 * (apps/server, apps/lobby) includes this header, links horse_goals.c, and calls
 * horse_goals_install() once at startup. Translation units that only include local_game.h decide
 * with horse_goals.h's built-in defaults instead. */
#ifndef HORSE_GOALS_HOST_H
#define HORSE_GOALS_HOST_H

#include "horse_goals.h"

/* PARENA-generated (horse_goals.c) */
int horse_goal_pick(int roll, int stamina_milli, int mood);
int horse_goal_duration_ms(int goal);
int horse_goal_speed_milli(int goal);
int horse_goal_radius_milli(int goal);
int horse_goal_reached_milli(void);
int horse_graze_restore_milli(int mood);
int horse_startle_radius_milli(void);
int horse_goal_react_base_ms(int goal);
int horse_goal_jitter_skill_permille(void);

static const HorseGoals g_horse_goals_parena = {
    horse_goal_pick,
    horse_goal_duration_ms,
    horse_goal_speed_milli,
    horse_goal_radius_milli,
    horse_goal_reached_milli,
    horse_graze_restore_milli,
    horse_startle_radius_milli,
    horse_goal_react_base_ms,
    horse_goal_jitter_skill_permille,
};

static inline void horse_goals_install(void) { phys_set_horse_goals(&g_horse_goals_parena); }

#endif /* HORSE_GOALS_HOST_H */
