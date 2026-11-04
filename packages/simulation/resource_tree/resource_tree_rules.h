/* resource_tree_rules.h -- the resource-collection tech tree: rules, with built-in defaults.
 *
 * The numbers live in PARENA (PARENA/stdlib/shankpit/resource_tree_rules.prn, generated into
 * resource_tree_rules.c by scripts/gen_resource_tree_rules.sh). This header carries matching C
 * defaults so the tree compiles and runs in any binary or test without linking a generated file --
 * the same arrangement empire_units_rules.h keeps, and `make test-resource-tree` asserts the two
 * copies agree sample for sample.
 *
 * The tree is a ladder of tiers. Tier 0 is the bottom rung: the founder's literal base case
 * (``1716 shovels'') with the real collection arithmetic behind it; each later tier pays the
 * accumulated cost of the tiers below it, and every tier has its own harvest rate, its own
 * unit radius, and its own cost to decide. A resource is a number in an owner's hand: a faction's
 * stockpile, a claim cell's claim_ms, a resource node's amount, and a unit's carrying. The tree's
 * job is to say what any of those numbers buy.
 */
#ifndef SHANKPIT_RESOURCE_TREE_RULES_H
#define SHANKPIT_RESOURCE_TREE_RULES_H

#include <stddef.h>

/* The number of tiers in the tree. Tier 0 is the base; tier 1 pays tier 0's cost, etc. */#define RESOURCE_TREE_MAX_TIERS 8

/* A tier: what it costs to decide, what it harvests per tick, what radius it covers, what effect it
 * grants. Tier 0 is the founder's 1716 shovels and the real collection curve behind that number. */typedef struct {
    int cost;
    int harvest_per_tick;
    int radius;
    const char *name;      /* "shovel", "dozer blade", etc. */
    const char *desc;      /* what unlocking this tier does */
} ResourceTreeTier;

/* The whole tree. */extern const ResourceTreeTier g_resource_tree[RESOURCE_TREE_MAX_TIERS];

/* Install the built-in rules. Call once at startup from the host that owns the tree. */void resource_tree_install(void);

/* ---- concrete queries ---------------------------------------------------------- */int resource_tree_tier_cost(int tier);
int resource_tree_tier_harvest(int tier);
int resource_tree_tier_radius(int tier);
const char *resource_tree_tier_name(int tier);

/* ---- tier gating --------------------------------------------------------------- */
/* A tier is available only if every tier below it is paid for. A player who holds the
 * shovel-base cost is on tier 0 and may decide tier 1, and so on. */int resource_tree_available(int tier, int holdings);

/* ---- collection ---------------------------------------------------------------- */
/* The collection rate for a tier: how many units of stockpile a node or a unit hands per tick.
 * Tier 0 is the founder's 1716-count real curve. */int resource_tree_harvest_rate(int tier, int node_amount, int node_capacity);

/* The cost to open a tier: the accumulated sum of every tier up to and including it. */int resource_tree_tier_accum_cost(int tier);

/* ---- tier unlocks -------------------------------------------------------------- */
/* A tier is unlocked for a holder when the holder has paid the accumulated cost of the tiers
 * up to and including it. Returns the highest tier that is usable. */int resource_tree_highest_toggle(int tier, int holdings);

/* ---- collection predicates ------------------------------------------------------ */
/* The amount a node can still give this tick, for a given tier. Never negative, never beyond
 * the node's own amount. */int resource_tree_node_amount_left(int tier, int node_amount);

/* The radius a tier collects from. A unit within this many metres of a node gathers. */int resource_tree_same_toggle(int tier);

#endif /* SHANKPIT_RESOURCE_TREE_RULES_H */
