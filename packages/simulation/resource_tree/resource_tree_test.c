/* resource_tree_test.c -- the resource-collection tech tree, headless.
 *
 * What this proves: tier 0 is the founder's literal base case -- 1716 shovels and the real
 * collection curve behind that number; each later tier costs the accumulated sum of the tiers
 * below it, so a player can only pay tier N if they have already paid every tier before it;
 * a player holds a tier when their stockpile of the base resource is at least the tier's
 * accumulated cost. The test asserts the tree of a fresh box, one that has worked the base
 * shovel node on tier 0, and the exact tier unlock threshold.
 */
#include "resource_tree.h"
#include "resource_tree_rules_host.h"

#include <stdio.h>
#include <string.h>

static int g_checks = 0, g_failures = 0;

static void ok(int cond, const char *what) {
    g_checks++;
    if (!cond) { g_failures++; printf("FAIL  %s\n", what); }
    else printf("ok    %s\n", what);
}

int main(void) {
    printf("== resource_tree: tier 0 == 1716 shovels ==\n");
    ok(resource_tree_tier_cost(0) == 1716, "tier 0 costs exactly 1716 shovels");
    ok(resource_tree_tier_harvest(0) == 1, "tier 0 harvests 1 unit per tick");
    ok(resource_tree_tier_radius(0) == 5, "tier 0 has a 5-metre radius");
    ok(strcmp(resource_tree_tier_name(0), "shovel") == 0, "tier 0 is named 'shovel'");
    ok(strcmp(resource_tree_tier_name(7), "freight train") == 0,
       "the top tier is named 'freight train'");

    printf("== resource_tree: collection rate ==\n");
    ok(resource_tree_harvest_rate(0, 100, 200) == 1, "tier 0 harvests 1 of a 100-unit node");
    ok(resource_tree_harvest_rate(1, 50, 100) == 2, "tier 1 harvests 2 of a 50-unit node");

    printf("== resource_tree: accumulated tier costs ==\n");
    ok(resource_tree_tier_accum_cost(0) == 1716, "tier 0's accumulated cost is 1716");
    ok(resource_tree_tier_accum_cost(1) == 1716 + 5000, "tier 1's accumulated cost is 6716");
    ok(resource_tree_tier_accum_cost(2) == 1716 + 5000 + 8000, "tier 2's accumulated cost is 14716");

    printf("== resource_tree: tier unlocks (the founder's 1716-threshold test) ==\n");
    ok(resource_tree_available(0, 1715) == 0, "1715 shovels do not open tier 0");
    ok(resource_tree_available(0, 1716) == 1, "exactly 1716 shovels open tier 0");
    ok(resource_tree_available(1, 1715) == 0, "1715 shovels do not open tier 1");
    ok(resource_tree_available(1, 1716) == 0, "1716 shovels do NOT open tier 1 (needs 6716)");
    ok(resource_tree_available(2, 14715) == 0, "14715 shovels do NOT open tier 2");
    ok(resource_tree_available(2, 14716) == 1, "exactly 14716 shovels open tier 2");

    printf("== resource_tree: highest toggle ==\n");
    ok(resource_tree_highest_toggle(7, 1000) == -1, "no money, no toggle");
    ok(resource_tree_highest_toggle(7, 1716) == 0, "1716 shovels -> tier 0 only");
    ok(resource_tree_highest_toggle(7, 6715) == 0, "6715 shovels -> tier 0");
    ok(resource_tree_highest_toggle(7, 6716) == 1, "6716 shovels -> tier 1");
    ok(resource_tree_highest_toggle(7, 14715) == 1, "14715 -> tier 1");
    ok(resource_tree_highest_toggle(7, 14716) == 2, "14716 -> tier 2");
    ok(resource_tree_highest_toggle(7, 1716 + 5000 + 8000 + 12000 + 16000 + 20000 + 24000 + 28000)
       == 7, "the full tree costs every tier");

    printf("== resource_tree: tier names ==\n");
    ok(strcmp(resource_tree_tier_name(3), "dragline cutter") == 0,
       "tier 3 is named 'dragline cutter'");

    printf("\nresource_tree: %d passed, %d failed\n", g_checks - g_failures, g_failures);
    return g_failures ? 1 : 0;
}
