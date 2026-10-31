/* empire_units_test.c -- the EMPIRE unit model, without a server, a GL context or a display.
 *
 * What this proves, in the founder's own terms: an archetype is a real thing to spawn (citizen, zombie,
 * soldier, ally, guard, worker) with its own brain role and skin; the generic human traits are inherited
 * by humans and NOT by the zombie; the orders are programmable (a mask plus priorities, with masked-out
 * orders scoring zero); the decision picks the highest triggered order, engages first when it can see an
 * enemy, and says NONE -- "use the role's own brain" -- when nothing applies; and a world really gives up
 * resources and ground through the same functions the AI tick calls.
 *
 * Build/run: make test-empire-units
 */
#include "empire_units.h"
#include "empire_units_rules_host.h"

#include <stdio.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

static void ok(int cond, const char *what) {
    g_checks++;
    if (!cond) {
        g_failures++;
        printf("FAIL  %s\n", what);
    } else {
        printf("ok    %s\n", what);
    }
}

/* A world with two nodes and two cells, the shape a level would seed. */
static void build_world(EUWorld *w) {
    eu_world_clear(w);
    eu_world_add_node(w, 10.0f, 0.0f, 5);
    eu_world_add_node(w, -10.0f, 0.0f, 25);
    eu_world_add_claim_cell(w, 20.0f, 0.0f);
    eu_world_add_claim_cell(w, -20.0f, 0.0f);
}

int main(void) {
    printf("== empire_rules: PARENA and the header's defaults agree ==\n");
    {
        int mismatch = 0;
        for (int a = -1; a <= 8; a++) {
            if (eu_archetype_species(a) != eu_c_archetype_species(a)) mismatch++;
            if (eu_archetype_role(a) != eu_c_archetype_role(a)) mismatch++;
            if (eu_archetype_kit(a) != eu_c_archetype_kit(a)) mismatch++;
            if (eu_archetype_faction(a) != eu_c_archetype_faction(a)) mismatch++;
            if (eu_archetype_orders(a) != eu_c_archetype_orders(a)) mismatch++;
            if (eu_archetype_health(a) != eu_c_archetype_health(a)) mismatch++;
            for (int o = -1; o <= 6; o++)
                if (eu_order_priority(a, o) != eu_c_order_priority(a, o)) mismatch++;
        }
        ok(mismatch == 0, "every archetype field and order priority matches the C defaults");
        ok(eu_archetype_species(1) == g_eu_rules.archetype_species(1),
           "the generated table is what g_eu_rules actually points at");
        ok(eu_human_loyalty() == eu_c_human_loyalty() && eu_beast_loyalty() == eu_c_beast_loyalty(),
           "both inheritance rows match");
        ok(eu_claim_ticks() == eu_c_claim_ticks() && eu_gather_radius() == eu_c_gather_radius() &&
           eu_explore_radius() == eu_c_explore_radius() && eu_follow_distance() == eu_c_follow_distance(),
           "the world numbers match");
        eu_rules_install();
        ok(g_eu_rules.archetype_count() == 6, "installing PARENA's copy keeps the same table");
    }

    printf("== archetypes are real spawns ==\n");
    {
        ok(eu_c_archetype_species(EU_ARCH_NONE) == EU_SPECIES_NONE && eu_c_archetype_orders(EU_ARCH_NONE) == 0,
           "archetype 0 is 'none' -- an unauthored character keeps its old behaviour");
        ok(eu_c_archetype_orders(EU_ARCH_ZOMBIE) == EU_ORDER_ENGAGE,
           "a zombie accepts exactly one order: engage");
        ok((eu_c_archetype_orders(EU_ARCH_ALLY) & EU_ORDER_FOLLOW) &&
           (eu_c_archetype_orders(EU_ARCH_ALLY) & EU_ORDER_GATHER),
           "an ally follows and gathers");
        ok((eu_c_archetype_orders(EU_ARCH_SOLDIER) & EU_ORDER_CLAIM) &&
           (eu_c_archetype_orders(EU_ARCH_CITIZEN) & EU_ORDER_EXPLORE),
           "a soldier takes ground and a citizen explores");
        ok(eu_c_archetype_kit(EU_ARCH_ALLY) == 3,
           "the ally's default skin is Mike (AIKit 3), the founder's own example");
        ok(eu_c_archetype_faction(EU_ARCH_ALLY) == EU_FACTION_ALLY &&
           eu_c_archetype_faction(EU_ARCH_SOLDIER) == EU_FACTION_HOSTILE &&
           eu_c_archetype_faction(EU_ARCH_CITIZEN) == EU_FACTION_NEUTRAL,
           "factions are archetype defaults, overridable per spawn");
        ok(eu_c_archetype_health(EU_ARCH_ALLY) == 120 &&
           eu_c_archetype_health(EU_ARCH_ZOMBIE) == 40,
           "health comes from the archetype (an ally outlasts a zombie)");
    }

    printf("== the two inheritances ==\n");
    {
        EUUnit citizen, soldier, ally, zombie, worker, guard;
        eu_unit_init(&citizen, EU_ARCH_CITIZEN, 0, -1, -1);
        eu_unit_init(&soldier, EU_ARCH_SOLDIER, 0, -1, -1);
        eu_unit_init(&ally, EU_ARCH_ALLY, 0, -1, -1);
        eu_unit_init(&zombie, EU_ARCH_ZOMBIE, 0, -1, -1);
        eu_unit_init(&worker, EU_ARCH_WORKER, 0, -1, -1);
        eu_unit_init(&guard, EU_ARCH_GUARD, 0, -1, -1);

        ok(citizen.species == EU_SPECIES_HUMAN && soldier.species == EU_SPECIES_HUMAN &&
           ally.species == EU_SPECIES_HUMAN && worker.species == EU_SPECIES_HUMAN &&
           guard.species == EU_SPECIES_HUMAN,
           "every human archetype is human -- and inherits the same row");
        ok(citizen.courage == 600 && citizen.loyalty == 700 && citizen.greed == 500 &&
           soldier.courage == citizen.courage && ally.loyalty == citizen.loyalty,
           "humans inherit courage 600, loyalty 700, greed 500 from the baseline, not per-archetype");
        ok(zombie.species == EU_SPECIES_BEAST, "the zombie is not a human with different numbers");
        ok(zombie.loyalty == 0 && zombie.greed == 0 && zombie.courage == 900,
           "the beast row has no loyalty or greed to spend, and no fear deficit either");
        ok(zombie.curiosity == 300 && citizen.curiosity == 500,
           "curiosity differs by inheritance, which is what makes a citizen wander and a zombie not");
        ok(ally.role == 3 && ally.kit == 3 && ally.faction == EU_FACTION_ALLY,
           "an initialized ally carries its role, skin and faction");
    }

    printf("== orders are programmable ==\n");
    {
        int unscored = 0;
        for (int a = 1; a <= EU_ARCH_COUNT; a++)
            for (int i = 0; i <= EU_ORDER_IDX_GUARD; i++)
                if (eu_c_order_priority(a, i) <= 0) unscored++;
        ok(unscored == 0,
           "every archetype can score every order, so an authored mask is never silently swallowed");

        EUUnit custom;
        eu_unit_init(&custom, EU_ARCH_CITIZEN, 0, -1, EU_ORDER_ENGAGE | EU_ORDER_FOLLOW);
        ok(custom.orders == (EU_ORDER_ENGAGE | EU_ORDER_FOLLOW),
           "an author's mask replaces the archetype's (a citizen can be told to fight and follow)");
        ok(eu_unit_priority(&custom, EU_ORDER_ENGAGE) == eu_c_order_priority(EU_ARCH_CITIZEN, EU_ORDER_IDX_ENGAGE),
           "and that order is usable, with the archetype's own priority, not zero");
        ok(eu_unit_priority(&custom, EU_ORDER_EXPLORE) == 0, "a masked-out order scores zero");

        EUUnit mute;
        eu_unit_init(&mute, EU_ARCH_ALLY, 0, -1, 0);
        ok(mute.orders == 0, "orders 0 is legal: takes no orders, just does what its brain does");

        EUUnit authored;
        eu_unit_init(&authored, EU_ARCH_ALLY, 5 /* AIKit GEORGE */, -1, -1);
        ok(authored.kit == 5, "an authored skin wins over the archetype's default (Mike would be 3)");
        EUUnit factioned;
        eu_unit_init(&factioned, EU_ARCH_SOLDIER, 0, EU_FACTION_ALLY, -1);
        ok(factioned.faction == EU_FACTION_ALLY,
           "and an authored faction wins -- a soldier on your side is a real thing to spawn");
    }

    printf("== the decision ==\n");
    {
        EUWorld w;
        build_world(&w);
        EUView view = {0};
        view.leader_alive = 1;
        view.leader_dist = 5.0f;

        EUUnit zombie, ally, soldier, citizen, guard, worker;
        eu_unit_init(&zombie, EU_ARCH_ZOMBIE, 0, -1, -1);
        eu_unit_init(&ally, EU_ARCH_ALLY, 0, -1, -1);
        eu_unit_init(&soldier, EU_ARCH_SOLDIER, 0, -1, -1);
        eu_unit_init(&citizen, EU_ARCH_CITIZEN, 0, -1, -1);
        eu_unit_init(&guard, EU_ARCH_GUARD, 0, -1, -1);
        eu_unit_init(&worker, EU_ARCH_WORKER, 0, -1, -1);

        ok(eu_choose_order(&zombie, &w, &view) == EU_ORDER_NONE,
           "an idle zombie with no enemy has no order -- its role brain hunts instead");
        view.enemy_visible = 1;
        view.enemy_dist = 30.0f;
        ok(eu_choose_order(&zombie, &w, &view) == EU_ORDER_ENGAGE, "a zombie that sees you engages");
        ok(eu_choose_order(&ally, &w, &view) == EU_ORDER_ENGAGE,
           "so does an ally -- engage outranks every other order when there is a target");
        view.enemy_visible = 0;

        ok(eu_choose_order(&ally, &w, &view) == EU_ORDER_GATHER,
           "an ally with its leader close and a node in the world gathers");
        view.leader_dist = 40.0f;
        ok(eu_choose_order(&ally, &w, &view) == EU_ORDER_FOLLOW,
           "and follows when the leader has gone further than the follow distance");
        view.leader_alive = 0;
        view.leader_dist = 40.0f;
        ok(eu_choose_order(&ally, &w, &view) == EU_ORDER_GATHER,
           "with no leader left, following stops being an option instead of being chased forever");
        view.leader_alive = 1;

        ok(eu_choose_order(&soldier, &w, &view) == EU_ORDER_CLAIM,
           "a soldier with ground to take claims it");
        EUWorld empty;
        eu_world_clear(&empty);
        ok(eu_choose_order(&soldier, &empty, &view) == EU_ORDER_GUARD,
           "with nothing to take it holds its post (GUARD is the standing fallback)");
        ok(eu_choose_order(&guard, &empty, &view) == EU_ORDER_GUARD, "a guard guards");

        view.idle_ms = 0;
        ok(eu_choose_order(&citizen, &w, &view) == EU_ORDER_GATHER,
           "a citizen with something to gather gathers it");
        view.idle_ms = 3000;
        ok(eu_choose_order(&citizen, &w, &view) == EU_ORDER_EXPLORE,
           "and once idle explores, because exploring outranks gathering for a citizen");
        EUWorld no_nodes;
        eu_world_clear(&no_nodes);
        eu_world_add_claim_cell(&no_nodes, 0.0f, 0.0f);
        EUView idle_view = {0};
        idle_view.idle_ms = 5000;
        idle_view.leader_alive = 1;
        idle_view.leader_dist = 1.0f;
        ok(eu_choose_order(&citizen, &no_nodes, &idle_view) == EU_ORDER_EXPLORE,
           "with no nodes left it still explores rather than standing in the claim it cannot use");
        ok(eu_choose_order(&worker, &w, &view) == EU_ORDER_GATHER,
           "a worker gathers before it follows");
    }

    printf("== territory and resources ==\n");
    {
        EUWorld w;
        build_world(&w);
        ok(w.node_count == 2 && w.cell_count == 2, "the world keeps what was seeded");
        ok(eu_world_add_node(&w, 0.0f, 0.0f, 1) == 2, "a third node is accepted");
        EUUnit ally;
        eu_unit_init(&ally, EU_ARCH_ALLY, 0, -1, -1);
        ok(w.nodes[1].amount == 25 && w.nodes[1].capacity == 25, "a node is seeded at its capacity");

        int gathered = 0;
        while (eu_world_gather(&w, &ally, 1)) gathered++;
        ok(gathered == 25, "gathering a node yields exactly its amount, no more");
        ok(w.stockpile[EU_FACTION_ALLY] == 25 && w.gathered[EU_FACTION_ALLY] == 25,
           "the faction's stockpile and lifetime total both see it");
        ok(ally.carrying == 25, "and the unit itself is carrying what it gathered");
        ok(eu_world_gather(&w, &ally, 1) == 0, "an empty node gives nothing");
        ok(eu_nearest_node(&w, 100.0f, 100.0f) == 0, "the nearest node with something left is chosen");

        EUUnit soldier;
        eu_unit_init(&soldier, EU_ARCH_SOLDIER, 0, -1, -1);
        int claim_ms = 0;
        int claimed_at = -1;
        while (claim_ms < 30000 && claimed_at < 0) {
            claim_ms += 1000;
            if (eu_world_claim_tick(&w, &soldier, 0, 1000)) claimed_at = claim_ms;
        }
        ok(claimed_at == 30000, "a claim takes exactly the rules' 30 seconds and not a tick less");
        ok(w.cells[0].owner == EU_FACTION_HOSTILE, "and the cell is the faction's afterwards");
        ok(eu_world_claim_tick(&w, &soldier, 0, 1000) == 0, "claiming your own cell is not an event");

        EUUnit rival;
        eu_unit_init(&rival, EU_ARCH_ALLY, 0, -1, -1);
        eu_world_claim_tick(&w, &rival, 1, 20000);
        eu_world_claim_tick(&w, &soldier, 1, 1000);
        ok(w.cells[1].claimant == EU_FACTION_HOSTILE && w.cells[1].claim_ms == 1000,
           "a second faction standing on a cell starts its own clock instead of inheriting progress");
    }

    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
