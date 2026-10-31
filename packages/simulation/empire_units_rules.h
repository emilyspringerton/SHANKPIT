/* empire_units_rules.h -- the EMPIRE unit model's numbers, with built-in defaults.
 *
 * The numbers live in PARENA (PARENA/stdlib/shankpit/empire_rules.prn, generated into empire_rules.c by
 * scripts/gen_empire_rules.sh / `make regen-empire-rules`, installed through empire_rules_host.h). This
 * header carries matching C defaults so empire_units.h compiles and runs in any binary or test without
 * linking a generated file -- the same arrangement zone_rules.h/widget_rules.h keep, and
 * `make test-empire-units` asserts the two copies agree sample for sample.
 *
 * Three tables, deliberately separate:
 *   - ARCHETYPES: what a spawned thing IS. An archetype picks a brain role, a default skin, a faction,
 *     an order mask and a health pool. This is the level editor's own affordance ("spawn a soldier").
 *   - INHERITANCE: the generic numbers every HUMAN inherits, and the beast row a non-human uses instead.
 *     A zombie is not a human with different numbers; it is a different row.
 *   - ORDERS: the programmable half -- a mask of accepted orders and a per-archetype priority for each.
 */
#ifndef SHANKPIT_EMPIRE_UNITS_RULES_H
#define SHANKPIT_EMPIRE_UNITS_RULES_H

typedef struct {
    int (*archetype_count)(void);
    int (*archetype_species)(int arch);
    int (*archetype_role)(int arch);
    int (*archetype_kit)(int arch);
    int (*archetype_faction)(int arch);
    int (*archetype_orders)(int arch);
    int (*archetype_health)(int arch);
    int (*human_courage)(void);
    int (*human_aggression)(void);
    int (*human_loyalty)(void);
    int (*human_greed)(void);
    int (*human_curiosity)(void);
    int (*beast_courage)(void);
    int (*beast_aggression)(void);
    int (*beast_loyalty)(void);
    int (*beast_greed)(void);
    int (*beast_curiosity)(void);
    int (*order_follow)(void);
    int (*order_explore)(void);
    int (*order_engage)(void);
    int (*order_gather)(void);
    int (*order_claim)(void);
    int (*order_guard)(void);
    int (*order_priority)(int arch, int order);
    int (*claim_ticks)(void);
    int (*claim_radius)(void);
    int (*gather_radius)(void);
    int (*node_capacity)(void);
    int (*explore_radius)(void);
    int (*follow_distance)(void);
} EUUnitRules;

static inline int eu_clamp(int lo, int hi, int x) { return x < lo ? lo : (x > hi ? hi : x); }

static inline int eu_c_archetype_count(void) { return 6; }
static inline int eu_c_archetype_species(int arch) {
    arch = eu_clamp(0, 6, arch);
    if (arch == 0) return 0;
    return arch == 2 ? 2 : 1;
}
static inline int eu_c_archetype_role(int arch) {
    arch = eu_clamp(0, 6, arch);
    if (arch == 0) return 0;
    if (arch == 1) return 10; /* WANDERING_BOT */
    if (arch == 2) return 7;  /* RELENTLESS_PURSUER */
    if (arch == 3) return 4;  /* GUARD */
    if (arch == 4) return 3;  /* STORY_ALLY */
    if (arch == 5) return 4;  /* GUARD */
    return 10;                /* WORKER -> WANDERING_BOT */
}
static inline int eu_c_archetype_kit(int arch) {
    arch = eu_clamp(0, 6, arch);
    if (arch == 0) return 0;
    if (arch == 1) return 1; /* MANNEQUIN */
    if (arch == 2) return 0; /* AUTO (the zombie clip set is picked by the witness role, not by AIKit) */
    if (arch == 3) return 2; /* STAN */
    if (arch == 4) return 3; /* MIKE -- the founder's own ally */
    if (arch == 5) return 5; /* GEORGE */
    return 4;                /* LEELA */
}
static inline int eu_c_archetype_faction(int arch) {
    arch = eu_clamp(0, 6, arch);
    if (arch == 0) return 0;
    if (arch == 2 || arch == 3 || arch == 5) return 2; /* hostile */
    if (arch == 4) return 1;                           /* ally */
    return 0;                                          /* citizen/worker: neutral */
}
static inline int eu_c_archetype_orders(int arch) {
    arch = eu_clamp(0, 6, arch);
    if (arch == 0) return 0;
    if (arch == 1) return 42; /* EXPLORE|GATHER|GUARD */
    if (arch == 2) return 4;  /* ENGAGE */
    if (arch == 3) return 52; /* ENGAGE|CLAIM|GUARD */
    if (arch == 4) return 15; /* FOLLOW|EXPLORE|ENGAGE|GATHER */
    if (arch == 5) return 36; /* ENGAGE|GUARD */
    return 11;                /* FOLLOW|EXPLORE|GATHER */
}
static inline int eu_c_archetype_health(int arch) {
    arch = eu_clamp(0, 6, arch);
    if (arch == 0) return 100;
    if (arch == 1) return 60;
    if (arch == 2) return 40;
    if (arch == 3) return 100;
    if (arch == 4) return 120;
    if (arch == 5) return 100;
    return 70;
}

static inline int eu_c_human_courage(void) { return 600; }
static inline int eu_c_human_aggression(void) { return 500; }
static inline int eu_c_human_loyalty(void) { return 700; }
static inline int eu_c_human_greed(void) { return 500; }
static inline int eu_c_human_curiosity(void) { return 500; }
static inline int eu_c_beast_courage(void) { return 900; }
static inline int eu_c_beast_aggression(void) { return 900; }
static inline int eu_c_beast_loyalty(void) { return 0; }
static inline int eu_c_beast_greed(void) { return 0; }
static inline int eu_c_beast_curiosity(void) { return 300; }

static inline int eu_c_order_follow(void) { return 1; }
static inline int eu_c_order_explore(void) { return 2; }
static inline int eu_c_order_engage(void) { return 4; }
static inline int eu_c_order_gather(void) { return 8; }
static inline int eu_c_order_claim(void) { return 16; }
static inline int eu_c_order_guard(void) { return 32; }

static inline int eu_c_order_priority(int arch, int order) {
    arch = eu_clamp(0, 6, arch);
    order = eu_clamp(0, 5, order);
    /* EVERY archetype scores EVERY order, including ones its default mask omits: an authored mask is
       what makes "a citizen that fights" possible, so a zero here would swallow the author's intent. */
    static const int table[7][6] = {
        /* FOLLOW EXPLORE ENGAGE GATHER CLAIM GUARD */
        {   0,    0,     0,     0,    0,    0 }, /* none    */
        {   30,   60,    25,    50,   20,   40 }, /* citizen */
        {   5,    10,    100,   5,    5,    20 }, /* zombie  */
        {   20,   20,    85,    10,   60,   50 }, /* soldier */
        {   80,   30,    90,    40,   20,   30 }, /* ally    */
        {   10,   10,    70,    5,    20,   90 }, /* guard   */
        {   50,   40,    10,    80,   30,   20 }  /* worker  */
    };
    return table[arch][order];
}

static inline int eu_c_claim_ticks(void) { return 30000; }
static inline int eu_c_claim_radius(void) { return 6; }
static inline int eu_c_gather_radius(void) { return 5; }
static inline int eu_c_node_capacity(void) { return 25; }
static inline int eu_c_explore_radius(void) { return 60; }
static inline int eu_c_follow_distance(void) { return 20; }

static EUUnitRules g_eu_rules = {
    eu_c_archetype_count, eu_c_archetype_species, eu_c_archetype_role, eu_c_archetype_kit,
    eu_c_archetype_faction, eu_c_archetype_orders, eu_c_archetype_health,
    eu_c_human_courage, eu_c_human_aggression, eu_c_human_loyalty, eu_c_human_greed,
    eu_c_human_curiosity,
    eu_c_beast_courage, eu_c_beast_aggression, eu_c_beast_loyalty, eu_c_beast_greed,
    eu_c_beast_curiosity,
    eu_c_order_follow, eu_c_order_explore, eu_c_order_engage, eu_c_order_gather, eu_c_order_claim,
    eu_c_order_guard, eu_c_order_priority,
    eu_c_claim_ticks, eu_c_claim_radius, eu_c_gather_radius, eu_c_node_capacity,
    eu_c_explore_radius, eu_c_follow_distance
};

static inline void eu_rules_set(const EUUnitRules *r) { if (r) g_eu_rules = *r; }

#endif /* SHANKPIT_EMPIRE_UNITS_RULES_H */
