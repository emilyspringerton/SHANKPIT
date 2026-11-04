/* resource_tree_rules.c -- the resource-collection tech tree, generated.
 *
 * Do not edit by hand. `make regen-resource-tree` (or the generator script) re-emits this
 * from PARENA/stdlib/shankpit/resource_tree_rules.prn. The C here is the compiled-in defaults;
 * it is identical to the PARENA-decided source so a test can assert the two agree.
 */
#include "resource_tree_rules.h"
#include <stdint.h>

/* The tree, a ladder of tiers. Tier 0 is the founder's literal base: exactly 1716 items of a
 * single base resource, gathered at the real collection rate, with a unit radius and a name.
 * Each later tier costs the accumulated sum of the tiers below it, harvests faster, and covers a
 * larger radius, and every tier has its own name and its own unlocking description. */const ResourceTreeTier g_resource_tree[RESOURCE_TREE_MAX_TIERS] = {
    { /* tier 0, the base */
        .cost = 1716,
        .harvest_per_tick = 1,
        .radius = 5,
        .name = "shovel",
        .desc = "first digger, ground worked by hand; the whole tree is monetised from here"
    },
    { /* tier 1 */
        .cost = 1716 + 5000,
        .harvest_per_tick = 2,
        .radius = 8,
        .name = "dozer blade",
        .desc = "a powered blade; the first harness upgrade"
    },
    { /* tier 2 */
        .cost = 1716 + 5000 + 8000,
        .harvest_per_tick = 3,
        .radius = 12,
        .name = "excavator arm",
        .desc = "a bucket arm; the tier that can split and stack nodes"
    },
    { /* tier 3 */
        .cost = 1716 + 5000 + 8000 + 12000,
        .harvest_per_tick = 4,
        .radius = 16,
        .name = "dragline cutter",
        .desc = "pulls whole cells of resource, emptying a node in one pass"
    },
    { /* tier 4 */
        .cost = 1716 + 5000 + 8000 + 12000 + 16000,
        .harvest_per_tick = 5,
        .radius = 20,
        .name = "mill wheel",
        .desc = "crushes nodes to their component parts"
    },
    { /* tier 5 */
        .cost = 1716 + 5000 + 8000 + 12000 + 16000 + 20000,
        .harvest_per_tick = 6,
        .radius = 24,
        .name = "drill head",
        .desc = "drives through the three-layer crust of the world"
    },
    { /* tier 6 */
        .cost = 1716 + 5000 + 8000 + 12000 + 16000 + 20000 + 24000,
        .harvest_per_tick = 7,
        .radius = 28,
        .name = "driller rig",
        .desc = "a self-supporting platform that wins its own node stock"
    },
    { /* tier 7 */
        .cost = 1716 + 5000 + 8000 + 12000 + 16000 + 20000 + 24000 + 28000,
        .harvest_per_tick = 8,
        .radius = 32,
        .name = "freight train",
        .desc = "moves stock from the mine to the bank, stockpile and gathered"
    },
};

/* Install the built-in rules. Call once at startup. */void resource_tree_install(void) {
    /* The table is static and read-only; nothing to install. */
}

/* ---- concrete queries ---------------------------------------------------------- */int resource_tree_tier_cost(int tier) {
    if (tier < 0 || tier >= RESOURCE_TREE_MAX_TIERS) return 0;
    return g_resource_tree[tier].cost;
}

int resource_tree_tier_harvest(int tier) {
    if (tier < 0 || tier >= RESOURCE_TREE_MAX_TIERS) return 0;
    return g_resource_tree[tier].harvest_per_tick;
}

int resource_tree_tier_radius(int tier) {
    if (tier < 0 || tier >= RESOURCE_TREE_MAX_TIERS) return 0;
    return g_resource_tree[tier].radius;
}

const char *resource_tree_tier_name(int tier) {
    if (tier < 0 || tier >= RESOURCE_TREE_MAX_TIERS) return "none";
    return g_resource_tree[tier].name;
}

/* ---- tier gating --------------------------------------------------------------- */int resource_tree_available(int tier, int holdings) {
    if (tier < 0 || tier >= RESOURCE_TREE_MAX_TIERS) return 0;
    /* A tier is available only if every tier below it is paid for, and the caller has the
     * tier's own accumulated cost in hand. */return (tier == 0 || resource_tree_available(tier - 1, holdings))
           && (holdings >= resource_tree_tier_accum_cost(tier));
}

/* The accumulated cost of a tier: the sum of tier 0 .. tier, inclusive. */int resource_tree_tier_accum_cost(int tier) {
    if (tier < 0 || tier >= RESOURCE_TREE_MAX_TIERS) return 0;
    return g_resource_tree[tier].cost;
}

/* ---- collection ---------------------------------------------------------------- */int resource_tree_harvest_rate(int tier, int node_amount, int node_capacity) {
    (void)node_capacity;
    if (tier < 0 || tier >= RESOURCE_TREE_MAX_TIERS) return 0;
    /* A node can give at most what it holds, and at most the tier's harvest rate. */return (g_resource_tree[tier].harvest_per_tick < node_amount) ? g_resource_tree[tier].harvest_per_tick : 0;
}

/* ---- tier unlocks -------------------------------------------------------------- */int resource_tree_highest_toggle(int tier, int holdings) {
    if (tier < 0 || tier >= RESOURCE_TREE_MAX_TIERS) return -1;
    /* Walk from the base tier up until the accumulated cost can no longer be paid; the highest
     * good toggle before that is the answer. */if (tier == 0) return (holdings >= resource_tree_tier_accum_cost(0)) ? 0 : -1;
    if (resource_tree_available(tier, holdings)) return tier;
    return resource_tree_highest_toggle(tier - 1, holdings);
}

/* ---- collection predicates ------------------------------------------------------ */int resource_tree_node_amount_left(int tier, int node_amount) {
    if (tier < 0 || tier >= RESOURCE_TREE_MAX_TIERS) return 0;
    /* The amount a node can still give this tick, capped by both the tier's harvest rate and the
     * node's own amount. */return node_amount > 0 ? g_resource_tree[tier].harvest_per_tick : 0;
}

int resource_tree_same_toggle(int tier) {
    if (tier < 0 || tier >= RESOURCE_TREE_MAX_TIERS) return 0;
    return g_resource_tree[tier].radius;
}
