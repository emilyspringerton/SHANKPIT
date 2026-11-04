/* resource_tree.h -- the resource-collection tech tree: the rule queries a host installs.
 *
 * The tree is a ladder of tiers. Tier 0 is the bottom rung: the founder's literal base case --
 * 1716 shovels -- with the real collection arithmetic behind that number; each later tier pays
 * the accumulated cost of the tiers below it, and every tier has its own harvest rate, its own
 * unit radius, and its own cost to decide. A resource is a number in an owner's hand: a faction's
 * stockpile, a claim cell's claim_ms, a resource node's amount, or a unit's carrying. The tree's
 * job is to say what any of those numbers buy.
 *
 * Pattern followed: the numbers live in resource_tree_rules.c (generated from PARENA, or the
 * compiled-in defaults when PARENA is not present), and this header carries the rule queries a
 * caller can make. There is no rules table to install -- nothing here is tunable at runtime --
 * because the tree is economy, not feel. Hosts install the rules via resource_tree_install() and
 * then query them directly.
 */
#ifndef SHANKPIT_RESOURCE_TREE_H
#define SHANKPIT_RESOURCE_TREE_H

/* Install the built-in rules. Call once at startup from the host that owns the tree. */void resource_tree_install(void);

/* The number of tiers in the tree. Tier 0 is the base; tier 1 pays tier 0's cost, etc. */#define RESOURCE_TREE_MAX_TIERS 8

/* ---- concrete queries ---------------------------------------------------------- */int resource_tree_tier_cost(int tier);
int resource_tree_tier_harvest(int tier);
int resource_tree_tier_radius(int tier);
const char *resource_tree_tier_name(int tier);

/* ---- tier gating --------------------------------------------------------------- */int resource_tree_available(int tier, int holdings);

/* The accumulated cost of a tier: the sum of every tier up to and including it. */int resource_tree_tier_accum_cost(int tier);

/* A tier is unlocked for a holder when the holder has paid the accumulated cost of the tiers
 * up to and including it. Returns the highest tier that is usable. */int resource_tree_highest_toggle(int tier, int holdings);

/* The amount a node can still give this tick, for a given tier. Never negative, never beyond
 * the node's own amount. */int resource_tree_node_amount_left(int tier, int node_amount);

/* The radius a tier collects from. A unit within this many metres of a node gathers. */int resource_tree_same_toggle(int tier);

#endif /* SHANKPIT_RESOURCE_TREE_H */
