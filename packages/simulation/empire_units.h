/* empire_units.h -- the EMPIRE unit model: what a spawned thing is, what it inherits, what it will do.
 *
 * Founder: "we need to be able to spawn in allies like mike etc and they need their own ais etc for
 * exploring the environment engaging enemies following allies taking over territory gathering resources
 * etc ... the ais also need some generic stuff that all humans will inherit but not all ais will be human
 * ... this is almost like a level building affordance i should be able to spawn in citizens zombies
 * soldiers etc and i should be able to have different ais".
 *
 * This is the layer between the level editor and story_ai.c's brain, and it exists so those two do not
 * grow a table of special cases each:
 *
 *   archetype  -- citizen / zombie / soldier / ally / guard / worker. Picking one picks a brain role
 *                 (AIRole), a default skin (AIKit) and a faction. That is the editor affordance.
 *   species    -- HUMAN or BEAST. Every human inherits one baseline row (courage, loyalty, greed,
 *                 curiosity); a non-human inherits the beast row. A zombie is not a human with different
 *                 numbers, which is why this is an inheritance switch and not a column.
 *   orders     -- the programmable half. A bitmask of what this unit accepts plus the archetype's own
 *                 priority for each, both authored (level JSON / NOCK) and both overridable per unit.
 *                 eu_choose_order below is the whole "custom AI": change the mask, change the behaviour,
 *                 without a new C branch.
 *
 * Deliberately dependency-free (no GL, SDL, physics, protocol, story_ai.h): roles and kits are plain
 * integers so story_ai.h can hold an EUUnit without a cycle, and so this is testable headlessly.
 * Numbers come from empire_rules.h, which mirrors PARENA (see that header).
 */
#ifndef SHANKPIT_EMPIRE_UNITS_H
#define SHANKPIT_EMPIRE_UNITS_H

#include <math.h>
#include <stdio.h>
#include "empire_units_rules.h"

/* Archetypes. 0 is deliberately "none": a character with no archetype keeps exactly the pre-EMPIRE
   behaviour (its authored role and kit, no orders), which is what keeps old levels loading unchanged. */
#define EU_ARCH_NONE 0
#define EU_ARCH_CITIZEN 1
#define EU_ARCH_ZOMBIE 2
#define EU_ARCH_SOLDIER 3
#define EU_ARCH_ALLY 4
#define EU_ARCH_GUARD 5
#define EU_ARCH_WORKER 6
#define EU_ARCH_COUNT 6

#define EU_SPECIES_NONE 0
#define EU_SPECIES_HUMAN 1
#define EU_SPECIES_BEAST 2

#define EU_FACTION_NEUTRAL 0
#define EU_FACTION_ALLY 1
#define EU_FACTION_HOSTILE 2
#define EU_FACTION_COUNT 3

/* Orders, as bits. These values are the contract the level JSON's own `orders` field uses. */
#define EU_ORDER_NONE 0
#define EU_ORDER_FOLLOW 1
#define EU_ORDER_EXPLORE 2
#define EU_ORDER_ENGAGE 4
#define EU_ORDER_GATHER 8
#define EU_ORDER_CLAIM 16
#define EU_ORDER_GUARD 32
#define EU_ORDER_ALL (EU_ORDER_FOLLOW | EU_ORDER_EXPLORE | EU_ORDER_ENGAGE | EU_ORDER_GATHER | \
                      EU_ORDER_CLAIM | EU_ORDER_GUARD)

/* Order indexes for the rules table (eu_order_priority takes these, not the bits). */
#define EU_ORDER_IDX_FOLLOW 0
#define EU_ORDER_IDX_EXPLORE 1
#define EU_ORDER_IDX_ENGAGE 2
#define EU_ORDER_IDX_GATHER 3
#define EU_ORDER_IDX_CLAIM 4
#define EU_ORDER_IDX_GUARD 5

#define EU_MAX_NODES 16
#define EU_MAX_CLAIM_CELLS 32

/* A resource node is a place, not an item: standing on it is what gathers. Amount is what is left. */
typedef struct {
    float x, z;
    int amount;
    int capacity;
} EUResourceNode;

/* A claim cell is a piece of ground a faction can own. claim_ms is THIS claimant's progress, which is
   why claimant is stored: a second faction standing on it starts over rather than inheriting the work. */
typedef struct {
    float x, z;
    int radius;
    int owner;     /* EU_FACTION_* */
    int claimant;  /* EU_FACTION_* currently making progress, NEUTRAL when nobody */
    int claim_ms;
} EUClaimCell;

typedef struct {
    EUResourceNode nodes[EU_MAX_NODES];
    int node_count;
    EUClaimCell cells[EU_MAX_CLAIM_CELLS];
    int cell_count;
    int stockpile[EU_FACTION_COUNT]; /* gathered and banked, per faction */
    int gathered[EU_FACTION_COUNT];  /* lifetime total, for a HUD/test to read */
} EUWorld;

typedef struct {
    int archetype;   /* EU_ARCH_* */
    int faction;     /* EU_FACTION_* */
    int species;     /* EU_SPECIES_* -- the inheritance switch */
    int orders;      /* accepted order bits */
    int role;        /* AIRole this archetype's brain runs (story_ai.h values) */
    int kit;         /* default AIKit; an authored kit always wins */
    int health;
    /* The inherited baseline, permille. Humans get the human row, beasts the beast row. */
    int courage, aggression, loyalty, greed, curiosity;
    /* Live order state, owned by the host (story_ai.c). */
    int order;       /* EU_ORDER_* currently executing, NONE when falling back to the brain */
    int target_node, target_cell;
    int carrying;    /* units gathered, not yet banked separately from the faction stockpile */
    float explore_x, explore_z;
    int exploring;   /* 0 = needs a new explore point */
} EUUnit;

/* What the unit can currently perceive. Kept as a plain struct so the host decides how it is filled and
   so the decision is testable without a server. */
typedef struct {
    int enemy_visible;
    float enemy_dist;
    float leader_dist;
    int leader_alive;
    int idle_ms;      /* how long the current mode has been running */
    int at_home;      /* within a couple of steps of the spawn point */
} EUView;

static inline const char *eu_archetype_name(int arch) {
    switch (arch) {
        case EU_ARCH_CITIZEN: return "citizen";
        case EU_ARCH_ZOMBIE: return "zombie";
        case EU_ARCH_SOLDIER: return "soldier";
        case EU_ARCH_ALLY: return "ally";
        case EU_ARCH_GUARD: return "guard";
        case EU_ARCH_WORKER: return "worker";
        default: return "none";
    }
}

static inline const char *eu_order_name(int order) {
    switch (order) {
        case EU_ORDER_FOLLOW: return "follow";
        case EU_ORDER_EXPLORE: return "explore";
        case EU_ORDER_ENGAGE: return "engage";
        case EU_ORDER_GATHER: return "gather";
        case EU_ORDER_CLAIM: return "claim";
        case EU_ORDER_GUARD: return "guard";
        default: return "none";
    }
}

static inline int eu_order_bit_for_index(int index) {
    switch (index) {
        case EU_ORDER_IDX_FOLLOW: return EU_ORDER_FOLLOW;
        case EU_ORDER_IDX_EXPLORE: return EU_ORDER_EXPLORE;
        case EU_ORDER_IDX_ENGAGE: return EU_ORDER_ENGAGE;
        case EU_ORDER_IDX_GATHER: return EU_ORDER_GATHER;
        case EU_ORDER_IDX_CLAIM: return EU_ORDER_CLAIM;
        case EU_ORDER_IDX_GUARD: return EU_ORDER_GUARD;
        default: return EU_ORDER_NONE;
    }
}

static inline int eu_order_index_for_bit(int order) {
    switch (order) {
        case EU_ORDER_FOLLOW: return EU_ORDER_IDX_FOLLOW;
        case EU_ORDER_EXPLORE: return EU_ORDER_IDX_EXPLORE;
        case EU_ORDER_ENGAGE: return EU_ORDER_IDX_ENGAGE;
        case EU_ORDER_GATHER: return EU_ORDER_IDX_GATHER;
        case EU_ORDER_CLAIM: return EU_ORDER_IDX_CLAIM;
        case EU_ORDER_GUARD: return EU_ORDER_IDX_GUARD;
        default: return -1;
    }
}

/* eu_unit_init -- an archetype's defaults, then the author's overrides, then the inheritance row.
 *   kit:     > 0 = the author's skin, 0 = the archetype's default (AUTO stays AUTO for the zombie,
 *            whose clip set is chosen by the witness role, not by AIKit).
 *   faction: >= 0 = the author's faction, < 0 = the archetype's default.
 *   orders:  >= 0 = the author's mask (0 is legal: "does what its brain does, takes no orders"),
 *            < 0 = the archetype's default mask.
 * The mask is the ONLY gate. Every archetype scores every order (see empire_rules.prn), so an authored
 * bit is always usable -- the mask says what a unit accepts, the priorities say what wins when more than
 * one accepted order is triggered. Archetype 0 keeps no orders, because it has no brain role to run. */
static inline void eu_unit_init(EUUnit *u, int archetype, int kit, int faction, int orders) {
    if (!u) return;
    int arch = eu_clamp(0, EU_ARCH_COUNT, archetype);
    u->archetype = arch;
    u->species = g_eu_rules.archetype_species(arch);
    u->role = g_eu_rules.archetype_role(arch);
    u->kit = kit > 0 ? kit : g_eu_rules.archetype_kit(arch);
    u->health = g_eu_rules.archetype_health(arch);
    u->faction = faction >= 0 ? eu_clamp(0, EU_FACTION_COUNT - 1, faction)
                              : g_eu_rules.archetype_faction(arch);
    int mask = arch == EU_ARCH_NONE ? 0
                                    : (orders >= 0 ? (orders & EU_ORDER_ALL)
                                                   : g_eu_rules.archetype_orders(arch));
    u->orders = 0;
    for (int i = 0; i <= EU_ORDER_IDX_GUARD; i++) {
        int bit = eu_order_bit_for_index(i);
        if ((mask & bit) && g_eu_rules.order_priority(arch, i) > 0) u->orders |= bit;
    }

    /* The generic stuff every human inherits -- and what a non-human inherits instead. */
    if (u->species == EU_SPECIES_HUMAN) {
        u->courage = g_eu_rules.human_courage();
        u->aggression = g_eu_rules.human_aggression();
        u->loyalty = g_eu_rules.human_loyalty();
        u->greed = g_eu_rules.human_greed();
        u->curiosity = g_eu_rules.human_curiosity();
    } else if (u->species == EU_SPECIES_BEAST) {
        u->courage = g_eu_rules.beast_courage();
        u->aggression = g_eu_rules.beast_aggression();
        u->loyalty = g_eu_rules.beast_loyalty();
        u->greed = g_eu_rules.beast_greed();
        u->curiosity = g_eu_rules.beast_curiosity();
    } else {
        u->courage = u->aggression = u->loyalty = u->greed = u->curiosity = 0;
    }

    u->order = EU_ORDER_NONE;
    u->target_node = -1;
    u->target_cell = -1;
    u->carrying = 0;
    u->explore_x = u->explore_z = 0.0f;
    u->exploring = 0;
}

static inline int eu_orders_has(const EUUnit *u, int order) {
    return u && (u->orders & order) != 0;
}

/* Priority 0 for an order this unit does not accept, so callers never need a second mask test. */
static inline int eu_unit_priority(const EUUnit *u, int order) {
    if (!u || !eu_orders_has(u, order)) return 0;
    int idx = eu_order_index_for_bit(order);
    if (idx < 0) return 0;
    return g_eu_rules.order_priority(u->archetype, idx);
}

static inline void eu_world_clear(EUWorld *w) {
    if (!w) return;
    for (int i = 0; i < EU_MAX_NODES; i++) {
        w->nodes[i].x = w->nodes[i].z = 0.0f;
        w->nodes[i].amount = w->nodes[i].capacity = 0;
    }
    for (int i = 0; i < EU_MAX_CLAIM_CELLS; i++) {
        w->cells[i].x = w->cells[i].z = 0.0f;
        w->cells[i].radius = 0;
        w->cells[i].owner = EU_FACTION_NEUTRAL;
        w->cells[i].claimant = EU_FACTION_NEUTRAL;
        w->cells[i].claim_ms = 0;
    }
    w->node_count = 0;
    w->cell_count = 0;
    for (int f = 0; f < EU_FACTION_COUNT; f++) w->stockpile[f] = w->gathered[f] = 0;
}

static inline int eu_world_add_node(EUWorld *w, float x, float z, int amount) {
    if (!w || w->node_count >= EU_MAX_NODES || amount <= 0) return -1;
    int cap = g_eu_rules.node_capacity();
    EUResourceNode *n = &w->nodes[w->node_count];
    n->x = x;
    n->z = z;
    n->amount = amount > cap ? cap : amount;
    n->capacity = n->amount;
    return w->node_count++;
}

static inline int eu_world_add_claim_cell(EUWorld *w, float x, float z) {
    if (!w || w->cell_count >= EU_MAX_CLAIM_CELLS) return -1;
    EUClaimCell *c = &w->cells[w->cell_count];
    c->x = x;
    c->z = z;
    c->radius = g_eu_rules.claim_radius();
    c->owner = EU_FACTION_NEUTRAL;
    c->claimant = EU_FACTION_NEUTRAL;
    c->claim_ms = 0;
    return w->cell_count++;
}

/* eu_world_load -- an authored level's own contested ground, replacing whatever was there. Flat arrays,
 * not a level struct: this header stays free of packages/world, the same boundary
 * story_ai_load_nav_graph keeps. Both counts are capped by EU_MAX_* (a host that authors more than the
 * world holds keeps the first ones, never overruns). Returns the number of nodes loaded. A level that
 * authors nothing loads nothing -- this function never invents terrain; the caller decides whether to
 * fall back to the default ring. */
static inline int eu_world_load(EUWorld *w, int node_count, const float *node_x, const float *node_z,
                                const int *node_amount, int cell_count, const float *cell_x,
                                const float *cell_z, const int *cell_radius) {
    if (!w) return 0;
    eu_world_clear(w);
    if (node_count > 0 && node_x && node_z && node_amount) {
        for (int i = 0; i < node_count && w->node_count < EU_MAX_NODES; i++)
            eu_world_add_node(w, node_x[i], node_z[i], node_amount[i]);
    }
    if (cell_count > 0 && cell_x && cell_z) {
        for (int i = 0; i < cell_count && w->cell_count < EU_MAX_CLAIM_CELLS; i++) {
            int idx = eu_world_add_claim_cell(w, cell_x[i], cell_z[i]);
            /* 0 is the level schema's own "use the host default", not a one-unit cell. */
            if (idx >= 0 && cell_radius && cell_radius[i] > 0) w->cells[idx].radius = cell_radius[i];
        }
    }
    return w->node_count;
}

/* eu_hud_text -- one line for the HUD: how many units are under orders, what each side has banked, how
 * much ground is held, how many nodes still have something in them. Returns 1 when it wrote a line and 0
 * when the world has nothing to say (no nodes, no cells, no units), so a level with no EMPIRE in it draws
 * nothing rather than a row of zeroes. Pure text, no GL -- which is also what makes it testable. */
static inline int eu_hud_text(const EUWorld *w, int units, char *buf, int buflen) {
    if (!buf || buflen <= 0) return 0;
    buf[0] = '\0';
    int nodes = w ? w->node_count : 0;
    int cells = w ? w->cell_count : 0;
    if (nodes == 0 && cells == 0 && units <= 0) return 0;
    int held = 0, live = 0;
    if (w) {
        for (int i = 0; i < w->cell_count; i++)
            if (w->cells[i].owner != EU_FACTION_NEUTRAL) held++;
        for (int i = 0; i < w->node_count; i++)
            if (w->nodes[i].amount > 0) live++;
    }
    snprintf(buf, (size_t)buflen, "UNITS %d  ALLY %d  HOSTILE %d  GROUND %d/%d  NODES %d/%d", units,
             w ? w->stockpile[EU_FACTION_ALLY] : 0, w ? w->stockpile[EU_FACTION_HOSTILE] : 0,
             held, cells, live, nodes);
    return 1;
}

static inline float eu_dist2(float ax, float az, float bx, float bz) {
    float dx = ax - bx, dz = az - bz;
    return dx * dx + dz * dz;
}

/* Nearest node that still has something in it, or -1. */
static inline int eu_nearest_node(const EUWorld *w, float x, float z) {
    if (!w) return -1;
    int best = -1;
    float best_d = 0.0f;
    for (int i = 0; i < w->node_count; i++) {
        if (w->nodes[i].amount <= 0) continue;
        float d = eu_dist2(x, z, w->nodes[i].x, w->nodes[i].z);
        if (best < 0 || d < best_d) { best = i; best_d = d; }
    }
    return best;
}

/* Nearest cell this faction does not already own, or -1. */
static inline int eu_nearest_free_cell(const EUWorld *w, int faction, float x, float z) {
    if (!w) return -1;
    int best = -1;
    float best_d = 0.0f;
    for (int i = 0; i < w->cell_count; i++) {
        if (w->cells[i].owner == faction) continue;
        float d = eu_dist2(x, z, w->cells[i].x, w->cells[i].z);
        if (best < 0 || d < best_d) { best = i; best_d = d; }
    }
    return best;
}

/* Gather one unit from a node into the unit's hands and the faction's stockpile. Returns 1 when a unit
   was actually gathered, 0 when the node is empty/spent/out of range of the caller's own choice. */
static inline int eu_world_gather(EUWorld *w, EUUnit *u, int node_index) {
    if (!w || !u || node_index < 0 || node_index >= w->node_count) return 0;
    EUResourceNode *n = &w->nodes[node_index];
    if (n->amount <= 0) return 0;
    n->amount--;
    u->carrying++;
    w->stockpile[u->faction]++;
    w->gathered[u->faction]++;
    return 1;
}

/* Stand on a cell for eu_claim_ticks() and it becomes yours. Another faction's progress is reset the
   moment it starts its own attempt, so a contested cell is a race, not a shared bar. Returns 1 on the
   tick the cell changes hands. */
static inline int eu_world_claim_tick(EUWorld *w, EUUnit *u, int cell_index, int dt_ms) {
    if (!w || !u || cell_index < 0 || cell_index >= w->cell_count) return 0;
    EUClaimCell *c = &w->cells[cell_index];
    if (c->owner == u->faction) { return 0; }
    if (c->claimant != u->faction) {
        c->claimant = u->faction;
        c->claim_ms = 0;
    }
    int need = g_eu_rules.claim_ticks();
    c->claim_ms += dt_ms > 0 ? dt_ms : 0;
    if (c->claim_ms >= need) {
        c->owner = u->faction;
        c->claimant = EU_FACTION_NEUTRAL;
        c->claim_ms = 0;
        return 1;
    }
    return 0;
}

/* eu_choose_order -- the decision, and the whole programmable surface.
 *
 * ENGAGE is checked first and is not a candidate in the score race: if a unit accepts ENGAGE and can see
 * an enemy, that is what it does. Everything else is "the highest-priority order whose own trigger is
 * true right now", with GUARD as the standing fallback (it needs no trigger -- holding ground is always
 * possible), and NONE when nothing applies, which means "fall back to the role's own brain" rather than
 * "stand still". That distinction is what lets an authored soldier keep its GUARD post while a citizen
 * wanders off to gather.
 */
static inline int eu_choose_order(const EUUnit *u, const EUWorld *w, const EUView *v) {
    if (!u || !v) return EU_ORDER_NONE;
    if (eu_orders_has(u, EU_ORDER_ENGAGE) && v->enemy_visible) return EU_ORDER_ENGAGE;
    if (!w && (eu_orders_has(u, EU_ORDER_GATHER) || eu_orders_has(u, EU_ORDER_CLAIM)))
        return EU_ORDER_NONE;

    int best_order = EU_ORDER_NONE;
    int best_score = 0;
    for (int idx = 0; idx <= EU_ORDER_IDX_GUARD; idx++) {
        int bit = eu_order_bit_for_index(idx);
        int score = eu_unit_priority(u, bit);
        if (score <= 0) continue;
        int triggered = 0;
        switch (idx) {
            case EU_ORDER_IDX_FOLLOW:
                triggered = v->leader_alive && v->leader_dist > (float)g_eu_rules.follow_distance();
                break;
            case EU_ORDER_IDX_EXPLORE:
                triggered = v->idle_ms >= 2000;
                break;
            case EU_ORDER_IDX_GATHER:
                triggered = w && eu_nearest_node(w, 0.0f, 0.0f) >= 0;
                break;
            case EU_ORDER_IDX_CLAIM:
                triggered = w && eu_nearest_free_cell(w, u->faction, 0.0f, 0.0f) >= 0;
                break;
            case EU_ORDER_IDX_GUARD:
                triggered = 1;
                break;
            default:
                break;
        }
        if (!triggered) continue;
        if (score > best_score) { best_score = score; best_order = bit; }
    }
    return best_order;
}

#endif /* SHANKPIT_EMPIRE_UNITS_H */
