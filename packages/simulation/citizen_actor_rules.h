/* citizen_actor_rules.h -- how a citizen in MODE_ZOMBIES chooses what to do with its body, as a
 * function table.
 *
 * The rules live in PARENA/stdlib/shankpit/citizen_actor_rules.prn, generated into
 * citizen_actor_rules.c (`make regen-citizen-actor-rules`) and installed by
 * citizen_actor_rules_host.h. This header carries the same numbers and the same two-opinion
 * decision (hand-written heuristics blended with a hand-written fixed-point policy) as plain C
 * defaults, so a translation unit that never installs the mod -- a test, a training harness --
 * decides exactly like the game does. The same arrangement horse_poop.h / slide_rules.h use.
 *
 * Deliberately no RNG, clock or state: the host owns the cooldowns and the body. A mod that wants
 * different citizens replaces this table; a mod that wants to react to one polls REFLUX
 * (REFLUX_ACTION_CITIZEN_*). */
#ifndef CITIZEN_ACTOR_RULES_H
#define CITIZEN_ACTOR_RULES_H

typedef struct {
    int (*jump_vy_milli)(void);          /* jump impulse, milli of a unit/tick */
    int (*jump_cooldown_ms)(void);       /* how often a body may jump at all */
    int (*crouch_speed_milli)(void);     /* the crouch-walk cap the shared physics applies */
    int (*slide_speed_milli)(void);      /* above this a crouch is the engine's own slide */
    int (*slide_min_ms)(void);
    int (*slide_max_ms)(void);
    int (*slide_jump_boost_milli)(void); /* extra vy for leaving a slide with a jump */
    int (*slide_jump_window_ms)(void);
    int (*wield_range_milli)(void);      /* a citizen draws a weapon inside this range */
    int (*fire_range_milli)(void);
    int (*fire_cooldown_ms)(void);
    int (*claw_reach_milli)(void);       /* a zombie's swing already reaches inside this */
    int (*dodge_ms)(void);               /* a dodge commits for this long */
    int (*cover_health_pm)(void);        /* hurt enough to stop trading shots */
    int (*cover_ms)(void);
    int (*cover_backpedal_milli)(void);  /* how fast cover gives ground */
    int (*retreat_health_pm)(void);      /* badly hurt: stop fighting, leave */
    int (*retreat_ms)(void);
    int (*heuristic_score)(int action, int dist_milli, int threat, int health_pm, int speed_milli, int ready);
    int (*policy_aggression)(int proximity, int hurt, int threat, int ready, int speed, int ground);
    int (*blended_score)(int action, int dist_milli, int threat, int health_pm, int speed_milli,
                         int ready, int aggression);
    int (*pick)(int dist_milli, int threat, int health_pm, int speed_milli, int ready, int aggression);
} CitizenActorRules;

/* ---- built-in defaults: numerically identical to citizen_actor_rules.prn --------------------- */
static int citizen_builtin_clamp(int lo, int hi, int x) { return x < lo ? lo : (x > hi ? hi : x); }

static int citizen_builtin_jump_vy_milli(void) { return 950; }
static int citizen_builtin_jump_cooldown_ms(void) { return 900; }
static int citizen_builtin_crouch_speed_milli(void) { return 350; }
static int citizen_builtin_slide_speed_milli(void) { return 750; }
static int citizen_builtin_slide_min_ms(void) { return 350; }
static int citizen_builtin_slide_max_ms(void) { return 1400; }
static int citizen_builtin_slide_jump_boost_milli(void) { return 200; }
static int citizen_builtin_slide_jump_window_ms(void) { return 250; }
static int citizen_builtin_wield_range_milli(void) { return 12000; }
static int citizen_builtin_fire_range_milli(void) { return 14000; }
static int citizen_builtin_fire_cooldown_ms(void) { return 260; }
static int citizen_builtin_claw_reach_milli(void) { return 2600; }
static int citizen_builtin_dodge_ms(void) { return 220; }
static int citizen_builtin_cover_health_pm(void) { return 400; }
static int citizen_builtin_cover_ms(void) { return 700; }
static int citizen_builtin_cover_backpedal_milli(void) { return 450; }
static int citizen_builtin_retreat_health_pm(void) { return 250; }
static int citizen_builtin_retreat_ms(void) { return 900; }

static int citizen_builtin_heuristic_score(int action, int dist, int threat, int health, int speed, int ready) {
    /* one scorer per action, in the mod's own order; see citizen_actor_rules.prn */
    if (action == 0) return threat ? 0 : 420;                                     /* idle */
    if (action == 1) return threat ? (dist > 12000 ? 500 : 200) : 900;            /* walk */
    if (action == 2) return threat ? 380 + citizen_builtin_clamp(0, 20000, dist) / 40 : 260; /* run */
    if (action == 3) {                                                            /* jump */
        if (!ready) return 0;
        if (threat && dist < 7000) return 600 + (health < 400 ? 260 : 0);
        return threat ? 120 : 60;
    }
    if (action == 4) return (threat == 0 && speed < 200) ? 240 : 0;               /* crouch */
    if (action == 5) {                                                            /* slide */
        if (!(threat && speed >= 750)) return 0;
        return dist < 6000 ? 900 : 660;
    }
    if (action == 6)                                                              /* slide jump */
        return (ready && threat && speed >= 750 && dist < 5000) ? 980 : 0;
    if (action == 7) {                                                            /* shoot */
        if (!(threat && dist >= 2500 && dist < 14000)) return 0;
        return 500 + ((14000 - dist) * 400) / 14000;
    }
    if (action == 8) return (threat && health < 500 && dist > 6000) ? 300 : 0;    /* reload */
    if (action == 9) {                                                            /* dodge */
        if (!(threat && dist < 2600)) return 0;
        return speed < 750 ? 1500 : 700;
    }
    if (action == 10)                                                             /* take cover */
        return (threat && health < 400 && dist < 6000) ? 900 : 0;
    if (action == 11) return (threat && health < 250) ? 1400 : 0;                 /* retreat */
    return 0;
}

static int citizen_builtin_policy_aggression(int proximity, int hurt, int threat, int ready, int speed, int ground) {
    return citizen_builtin_clamp(0, 1000,
        (citizen_builtin_clamp(0, 1000, proximity) * 320 +
         citizen_builtin_clamp(0, 1000, hurt) * 260 +
         (threat ? 1000 : 0) * 180 +
         (ready ? 1000 : 0) * 140 +
         citizen_builtin_clamp(0, 1000, speed) * 60 +
         (ground ? 1000 : 0) * 40) / 1000);
}

static int citizen_builtin_bias(int action) {
    switch (action) {
        case 0: return -200;
        case 1: return -120;
        case 2: return 80;
        case 3: return 200;
        case 4: return 60;
        case 5: return 180;
        case 6: return 220;
        case 7: return 160;
        case 8: return 60;
        case 9: return 200;
        case 10: return 120;
        case 11: return 180;
        default: return 0;
    }
}

static int citizen_builtin_blended_score(int action, int dist, int threat, int health, int speed,
                                         int ready, int aggression) {
    return citizen_builtin_clamp(0, 2000,
        citizen_builtin_heuristic_score(action, dist, threat, health, speed, ready) +
        ((aggression - 500) * citizen_builtin_bias(action)) / 1000);
}

static int citizen_builtin_pick(int dist, int threat, int health, int speed, int ready, int aggression) {
    int best = -1, best_action = 0;
    for (int action = 0; action < 12; action++) {
        int score = citizen_builtin_blended_score(action, dist, threat, health, speed, ready, aggression);
        if (score > best) { best = score; best_action = action; }
    }
    return best_action;
}

static const CitizenActorRules g_citizen_actor_builtin = {
    citizen_builtin_jump_vy_milli,
    citizen_builtin_jump_cooldown_ms,
    citizen_builtin_crouch_speed_milli,
    citizen_builtin_slide_speed_milli,
    citizen_builtin_slide_min_ms,
    citizen_builtin_slide_max_ms,
    citizen_builtin_slide_jump_boost_milli,
    citizen_builtin_slide_jump_window_ms,
    citizen_builtin_wield_range_milli,
    citizen_builtin_fire_range_milli,
    citizen_builtin_fire_cooldown_ms,
    citizen_builtin_claw_reach_milli,
    citizen_builtin_dodge_ms,
    citizen_builtin_cover_health_pm,
    citizen_builtin_cover_ms,
    citizen_builtin_cover_backpedal_milli,
    citizen_builtin_retreat_health_pm,
    citizen_builtin_retreat_ms,
    citizen_builtin_heuristic_score,
    citizen_builtin_policy_aggression,
    citizen_builtin_blended_score,
    citizen_builtin_pick,
};

static const CitizenActorRules *g_citizen_actor_rules_active = 0;

static inline void citizen_actor_set_rules(const CitizenActorRules *r) { g_citizen_actor_rules_active = r; }

/* Never NULL: an uninstalled host uses the built-in defaults, never a crash. */
static inline const CitizenActorRules *citizen_actor_rules(void) {
    return g_citizen_actor_rules_active ? g_citizen_actor_rules_active : &g_citizen_actor_builtin;
}

#endif /* CITIZEN_ACTOR_RULES_H */
