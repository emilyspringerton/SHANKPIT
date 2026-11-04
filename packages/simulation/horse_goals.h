/* horse_goals.h -- what a loose horse decides, as a function table.
 *
 * The real numbers live in PARENA/stdlib/shankpit/horse_goals.prn, generated into horse_goals.c
 * (`make regen-horse-goals`) and installed by horse_goals_host.h. local_game.h's own horse goal
 * tick includes THIS header only for the table type and the accessor, so a translation unit that
 * never installs the mod (most tests) decides with the built-in defaults below -- the same shape
 * horse_rules.h already uses for the ridden horse, and physics.h for the buggy.
 *
 * Deliberately no RNG, clock or state crosses this boundary: every function is pure and total, so
 * a goal decision can be tested without a world and replayed from a seed. */
#ifndef HORSE_GOALS_H
#define HORSE_GOALS_H

/* Goal ids -- protocol.h's HORSE_GOAL_* enum, kept as plain ints here so this header stays
 * independent of the wire header (the same reason physics.h keeps vehicle kinds as ints). */
#define HORSE_GOAL_ID_GRAZE  0
#define HORSE_GOAL_ID_WANDER 1
#define HORSE_GOAL_ID_IDLE   2
#define HORSE_GOAL_ID_HOME   3
#define HORSE_GOAL_ID_FLEE   4

typedef struct {
    int (*goal_pick)(int roll, int stamina_milli, int mood);  /* -> HORSE_GOAL_ID_* */
    int (*goal_duration_ms)(int goal);
    int (*goal_speed_milli)(int goal);          /* m/s x 1000, a walk */
    int (*goal_radius_milli)(int goal);         /* how far from the pad the target may be */
    int (*goal_reached_milli)(void);            /* close enough to call it reached */
    int (*graze_restore_milli)(int mood);       /* stamina milli per second while grazing */
    int (*startle_radius_milli)(void);          /* a player this close spooks a loose horse */
    int (*goal_react_base_ms)(int goal);        /* base for MISHRI's reaction delay */
    int (*goal_jitter_skill_permille)(void);    /* MISHRI aim-noise skill, permille */
} HorseGoals;

/* Built-in defaults -- byte-identical to horse_goals.prn's own numbers, so a test that never
   installs the mod exercises the same decisions the game ships. */
static int horse_goals_builtin_goal_pick(int roll, int stamina_milli, int mood) {
    if (stamina_milli < 300) return HORSE_GOAL_ID_GRAZE;
    if (mood > 5) return HORSE_GOAL_ID_HOME;
    if (mood > 3) return HORSE_GOAL_ID_IDLE;
    if (roll < 250) return HORSE_GOAL_ID_GRAZE;
    if (roll < 700) return HORSE_GOAL_ID_WANDER;
    return HORSE_GOAL_ID_IDLE;
}
static int horse_goals_builtin_goal_duration_ms(int goal) {
    if (goal < 1) return 14000;
    if (goal < 2) return 9000;
    if (goal < 3) return 6000;
    if (goal < 4) return 12000;
    return 4000;
}
static int horse_goals_builtin_goal_speed_milli(int goal) {
    if (goal < 1) return 1400;
    if (goal < 2) return 2400;
    if (goal < 3) return 0;
    if (goal < 4) return 3200;
    return 5200;
}
static int horse_goals_builtin_goal_radius_milli(int goal) {
    if (goal < 1) return 7000;
    if (goal < 2) return 15000;
    return 0;
}
static int horse_goals_builtin_goal_reached_milli(void) { return 1500; }
static int horse_goals_builtin_graze_restore_milli(int mood) { return mood > 5 ? 40 : 70; }
static int horse_goals_builtin_startle_radius_milli(void) { return 5000; }
static int horse_goals_builtin_goal_react_base_ms(int goal) {
    if (goal < 1) return 700;
    if (goal < 2) return 400;
    if (goal < 3) return 900;
    if (goal < 4) return 500;
    return 150;
}
static int horse_goals_builtin_goal_jitter_skill_permille(void) { return 350; }

static const HorseGoals g_horse_goals_builtin = {
    horse_goals_builtin_goal_pick,
    horse_goals_builtin_goal_duration_ms,
    horse_goals_builtin_goal_speed_milli,
    horse_goals_builtin_goal_radius_milli,
    horse_goals_builtin_goal_reached_milli,
    horse_goals_builtin_graze_restore_milli,
    horse_goals_builtin_startle_radius_milli,
    horse_goals_builtin_goal_react_base_ms,
    horse_goals_builtin_goal_jitter_skill_permille,
};

static const HorseGoals *g_horse_goals_active = 0;

static inline void phys_set_horse_goals(const HorseGoals *g) { g_horse_goals_active = g; }

/* Never NULL: an uninstalled host decides with the built-in defaults, never a crash. */
static inline const HorseGoals *horse_goals(void) {
    return g_horse_goals_active ? g_horse_goals_active : &g_horse_goals_builtin;
}

#endif /* HORSE_GOALS_H */
