/* horse_poop.h -- what comes out the other end, as a function table.
 *
 * The numbers live in PARENA/stdlib/shankpit/horse_poop_rules.prn, generated into
 * horse_poop_rules.c (`make regen-horse-poop-rules`) and installed by horse_poop_host.h. This header
 * is the table type plus the built-in defaults, the same shape horse_goals.h and horse_rules.h use,
 * so a translation unit that never installs the mod (a test, a training harness) still produces
 * piles exactly like the game does.
 *
 * Deliberately no RNG, clock or state: the host owns the countdown, the pile slots and the world.
 * A mod that wants to change how a farm behaves replaces the table; a mod that wants to react to a
 * pile polls REFLUX (REFLUX_ACTION_HORSE_POOPED) or reads local_state.piles. */
#ifndef HORSE_POOP_H
#define HORSE_POOP_H

typedef struct {
    int (*interval_ms)(int goal);     /* ms between piles under this goal; 0 = not this goal */
    int (*lifetime_ms)(void);         /* how long a pile stays in the world */
    int (*max_piles)(void);           /* how many the paddock holds; must match MAX_HORSE_PILES */
    int (*behind_milli)(void);        /* how far behind the tail it lands, milli of a unit */
    int (*spread_milli)(void);        /* sideways slop from the horse's own RNG, milli */
    int (*after_carrot_ms)(void);     /* a carrot eaten now comes out this many ms later */
} HorsePoop;

/* Built-in defaults -- byte-identical to horse_poop_rules.prn's own numbers, so a mod-less build
 * behaves the same way the shipped mod does. */
static int horse_poop_builtin_interval_ms(int goal) {
    if (goal < 1) return 5000;      /* graze */
    if (goal < 2) return 15000;     /* wander */
    return 0;                       /* idle / home / flee */
}
static int horse_poop_builtin_lifetime_ms(void) { return 45000; }
static int horse_poop_builtin_max_piles(void) { return 24; }
static int horse_poop_builtin_behind_milli(void) { return 1300; }
static int horse_poop_builtin_spread_milli(void) { return 400; }
static int horse_poop_builtin_after_carrot_ms(void) { return 2500; }

static const HorsePoop g_horse_poop_builtin = {
    horse_poop_builtin_interval_ms,
    horse_poop_builtin_lifetime_ms,
    horse_poop_builtin_max_piles,
    horse_poop_builtin_behind_milli,
    horse_poop_builtin_spread_milli,
    horse_poop_builtin_after_carrot_ms,
};

static const HorsePoop *g_horse_poop_active = 0;

static inline void phys_set_horse_poop(const HorsePoop *p) { g_horse_poop_active = p; }

/* Never NULL: an uninstalled host uses the built-in defaults, never a crash. */
static inline const HorsePoop *horse_poop(void) {
    return g_horse_poop_active ? g_horse_poop_active : &g_horse_poop_builtin;
}

#endif /* HORSE_POOP_H */
