/* resource_tree_rules_host.h -- the resource-tree rules from PARENA.
 *
 * The rules live in PARENA/stdlib/shankpit/resource_tree_rules.prn, generated into
 * resource_tree_rules.c by scripts/gen_resource_tree_rules.sh. A host that plays the
 * resource tree (apps/lobby, apps/server) includes this header and links resource_tree_rules.c.
 * Directories of pure queries: nothing here is tunable at runtime, because the tree is
 * economy, not feel.
 */
#ifndef SHANKPIT_RESOURCE_TREE_RULES_HOST_H
#define SHANKPIT_RESOURCE_TREE_RULES_HOST_H

#include "resource_tree_rules.h"

/* PARENA-generated (resource_tree_rules.c) */
int resource_tree_tier_cost(int tier);
int resource_tree_tier_harvest(int tier);
int resource_tree_tier_radius(int tier);

/* The tree, a ladder of tiers. Tier 0 is the founder's literal base case. */extern const ResourceTreeTier g_resource_tree[RESOURCE_TREE_MAX_TIERS];

/* Install the built-in rules. Call once at startup from the host that owns the tree. */void resource_tree_install(void);

#endif /* SHANKPIT_RESOURCE_TREE_RULES_HOST_H */
