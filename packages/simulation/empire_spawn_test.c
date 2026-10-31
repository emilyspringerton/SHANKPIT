/* empire_spawn_test.c -- the spawn path itself, from an authored archetype to a real order.
 *
 * empire_units_test.c proves the model; this proves the wiring: story_ai_spawn_unit turns an archetype
 * into a real player slot with the archetype's brain role, skin and health, the unit's own order is chosen
 * by the same decision the live tick uses, and the gather/claim runs mutate the shared world through the
 * real functions (a node is drained, a cell changes owner after thirty seconds of standing on it).
 *
 * story_ai.c's only outside dependency here is the BIG_O witness roster it seeds for VOXWORLD, stubbed
 * below so this stays a test of the EMPIRE spawn path rather than of the citizen sim.
 *
 * Build/run: make test-empire-spawn
 */
#include "story_ai.h"
#include "empire_units_rules_host.h"

#include <stdio.h>
#include <string.h>

/* No-op stand-in for packages/simulation/witness_ai.c (see this file's own header comment). */
void witness_ai_seed_voxworld_encounter(ServerState *s, unsigned int now_ms) { (void)s; (void)now_ms; }

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

static void stand_at(ServerState *s, int pid, float x, float z) {
    s->players[pid].x = x;
    s->players[pid].z = z;
    s->players[pid].y = 8.0f;
}

int main(void) {
    ServerState s;
    memset(&s, 0, sizeof s);
    s.scene_id = SCENE_VOXWORLD;
    /* Deliberately NOT MODE_STORY: an authored EMPIRE unit must tick in any mode, which is the widening
       this test is here to hold in place. */
    s.game_mode = MODE_ZOMBIES;
    s.story_phase = STORY_PHASE_PLAYING;
    s.players[0].active = 1;
    s.players[0].state = STATE_ALIVE;
    s.players[0].x = 0.0f;
    s.players[0].y = 8.0f;
    s.players[0].z = 0.0f;

    story_ai_reset(&s);
    EUWorld *w = story_ai_empire_world();

    printf("== a spawned ally is a real unit ==\n");
    int ally = story_ai_spawn_unit(&s, EU_ARCH_ALLY, 0, -1, -1, 5.0f, 8.0f, 5.0f);
    ok(ally >= 1, "an ally takes a real player slot");
    ok(story_ai_unit_archetype(ally) == EU_ARCH_ALLY, "and it is recorded as that archetype");
    ok(s.players[ally].forced_kit == 3, "with Mike's skin by default (AIKit 3)");
    ok(s.players[ally].health == 120, "and the archetype's health pool, not the generic 100");
    ok(story_ai_unit_order(ally) == EU_ORDER_NONE, "no order has been chosen before the first tick");

    int authored = story_ai_spawn_unit(&s, EU_ARCH_ALLY, 5 /* GEORGE */, EU_FACTION_HOSTILE, 4 /* engage */,
                                       -5.0f, 8.0f, 5.0f);
    ok(authored >= 1, "a second unit with an authored skin/faction/mask spawns");
    ok(s.players[authored].forced_kit == 5, "an authored skin beats the archetype's default");
    ok(story_ai_unit_archetype(authored) == EU_ARCH_ALLY, "its archetype is still the ally body");

    printf("== the order follows the world, not the spawn ==\n");
    s.players[0].x = 100.0f;
    story_ai_tick(&s, 1000);
    ok(story_ai_unit_order(ally) == EU_ORDER_FOLLOW, "with its leader far away the ally follows");

    s.players[0].x = 0.0f;
    story_ai_tick(&s, 5000);
    ok(story_ai_unit_order(ally) == EU_ORDER_GATHER,
       "leader close and something to gather: the ally gathers (gather outranks idle exploring)");

    printf("== gathering and claiming are the real world ==\n");
    int node = eu_nearest_node(w, 5.0f, 5.0f);
    int before = w->stockpile[EU_FACTION_ALLY];
    /* The leader walks with it: follow outranks gather, so a leader left behind would (correctly) turn this
       into a follow test instead. */
    s.players[0].x = w->nodes[node].x;
    s.players[0].z = w->nodes[node].z;
    stand_at(&s, ally, w->nodes[node].x, w->nodes[node].z);
    for (unsigned int t = 6000; t <= 8000; t += 500) story_ai_tick(&s, t);
    ok(w->stockpile[EU_FACTION_ALLY] > before,
       "standing on a node banks resources for the ally faction through the gather run");

    int soldier = story_ai_spawn_unit(&s, EU_ARCH_SOLDIER, 0, -1, -1, 70.0f, 8.0f, 0.0f);
    ok(soldier >= 1, "a soldier spawns");
    int cell = eu_nearest_free_cell(w, EU_FACTION_HOSTILE, 70.0f, 0.0f);
    stand_at(&s, soldier, w->cells[cell].x, w->cells[cell].z);
    /* Out of sight/earshot, so the soldier's order race is CLAIM vs GUARD (engage outranks both). */
    s.players[0].x = 5000.0f;
    s.players[0].z = 5000.0f;
    unsigned int t = 10000;
    for (int i = 0; i < 31; i++, t += 1000) story_ai_tick(&s, t);
    ok(w->cells[cell].owner == EU_FACTION_HOSTILE,
       "thirty seconds on a cell makes it the soldier's faction's ground");

    printf("== a zombie is not a person with orders ==\n");
    int zombie = story_ai_spawn_unit(&s, EU_ARCH_ZOMBIE, 0, -1, -1, 12.0f, 8.0f, 12.0f);
    ok(zombie >= 1, "a zombie spawns");
    ok(s.players[zombie].health == 40, "with the archetype's own (fragile) health");
    story_ai_tick(&s, 50000);
    ok(story_ai_unit_order(zombie) == EU_ORDER_NONE,
       "with nothing in sight it has no EMPIRE order and its own brain hunts instead");

    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
