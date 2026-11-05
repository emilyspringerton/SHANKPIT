/* loft_city_test.c -- the authored loft city (levels/lofts, tools/build_loft_city.py) through the REAL
 * loaders, closing the two gaps the city shipped with.
 *
 * Named-not-done, in the city's own docs, before this file existed:
 *   - "the 24 chests are authored and parsed, but nobody has hit one": this loads the checked-in city
 *     through level_boxes.h, places every authored chest through the real chests_place pool, and shoots
 *     one apart until it spills a gun onto the ground.
 *   - "an upper-floor zombie has never been watched walking": this spawns EVERY authored character
 *     through story_ai_spawn_unit -- which is also the proof that the file fits the engine's own
 *     simultaneous slots, MAX_CLIENTS-1 = 69 -- and then ticks the highest-placed one with the same
 *     minimal walk integrator witness_ai_zombies_test.c uses, asserting it closes distance.
 *
 * Build/run: make test-loft-city
 */
#include "story_ai.h"
#include "empire_units_rules_host.h"
#include "chests.h"
#include "gun_items.h"
#include "../common/protocol.h"
#include "../world/level_boxes.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* story_ai.c seeds the BIG_O witness roster for VOXWORLD; stubbed here so this stays a test of the
   authored city rather than of the citizen sim (empire_spawn_test.c stubs it for the same reason). */
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

static CustomLevelData g_level;

/* The AI writes intent (in_fwd, yaw); something has to integrate it. This is the same minimal step
   witness_ai_zombies_test.c uses, kept deliberately dumb so a failure here is the brain's, not physics'. */
static void walk_step(ServerState *s, float dt) {
    for (int i = 1; i < MAX_CLIENTS; i++) {
        PlayerState *p = &s->players[i];
        if (!p->active || p->state == STATE_DEAD) continue;
        float r = p->yaw * 0.0174533f;
        p->x += sinf(r) * p->in_fwd * 20.0f * dt;
        p->z += cosf(r) * p->in_fwd * 20.0f * dt;
    }
}

int main(void) {
    printf("== the city through the engine's own parser ==\n");
    ok(level_boxes_load_from_file("var/zombie/nextown_zombies.json", &g_level),
       "the loft city loads from var/zombie/nextown_zombies.json");
    ok(g_level.count > 1500, "with all eight lofts' geometry in it");
    /* 24 loft chests plus one per municipal building (levels/municipal) -- the district beside the
       lofts authors its own loot in the same file, so this asserts the lofts' share rather than the
       city's whole count. */
    ok(g_level.chest_count >= 24, "at least the 24 authored loft chests (each theme's own loot table)");
    printf("      %d authored characters\n", g_level.character_count);
    ok(g_level.character_count > 0 && g_level.character_count <= MAX_CLIENTS - 1,
       "the population fits MAX_CLIENTS-1 simultaneous slots (the tool budgets for exactly this)");

    printf("== the loot is real, and it breaks ==\n");
    chests_reset();
    gun_items_reset();
    int placed = 0;
    for (int i = 0; i < g_level.chest_count; i++)
        if (chests_place(g_level.chests[i].tier, g_level.chests[i].x, g_level.chests[i].y, g_level.chests[i].z) >= 0)
            placed++;
    ok(placed == g_level.chest_count, "every authored chest enters the real pool");

    int rare = -1, rares = 0;
    for (int i = 0; i < g_level.chest_count; i++)
        if (g_level.chests[i].tier == 2) { rares++; rare = i; }
    ok(rares > 0, "the themes' rare tier is in the city");
    ok(chests_active_count() == g_level.chest_count, "the pool agrees with the level");

    int broke = 0;
    if (rare >= 0) {
        float cx = g_level.chests[rare].x, cy = g_level.chests[rare].y, cz = g_level.chests[rare].z;
        for (int n = 0; n < 80 && !broke; n++)
            chests_hit_ray(cx - 8.0f, cy + 0.3f, cz, 1.0f, 0.0f, 0.0f, 20.0f, WPN_AR, 40, 3, &broke);
    }
    ok(broke == 1, "shooting it apart really breaks a chest");
    ok(gun_items_active_count() >= 1, "and it spills a gun onto the ground");
    chests_reset();
    gun_items_reset();

    printf("== every authored character gets a slot ==\n");
    ServerState s;
    memset(&s, 0, sizeof s);
    s.scene_id = SCENE_VOXWORLD;
    s.game_mode = MODE_ZOMBIES;   /* not MODE_STORY: an authored character ticks in any mode */
    s.story_phase = STORY_PHASE_PLAYING;
    s.players[0].active = 1;
    s.players[0].state = STATE_ALIVE;
    s.players[0].y = 8.0f;
    story_ai_reset(&s);

    int spawned = 0, upper_id = -1;
    float upper_y = -1e9f;
    for (int i = 0; i < g_level.character_count; i++) {
        const LevelCharacter *lc = &g_level.characters[i];
        int id = story_ai_spawn_unit(&s, lc->archetype, lc->kit, lc->faction, lc->orders, lc->x, lc->y, lc->z);
        if (id < 0) continue;
        spawned++;
        if (lc->y > upper_y) { upper_y = lc->y; upper_id = id; }
    }
    ok(spawned == g_level.character_count, "every authored character spawns (no silent slot exhaustion)");
    ok(upper_id >= 1, "one of them is on the highest floor of the tallest loft");
    ok(upper_y > 8.0f, "which really is an upper floor, not the ground");

    printf("== the city's own EMPIRE ground ==\n");
    ok(g_level.resource_node_count == 8, "a resource node per loft");
    ok(g_level.claim_cell_count == 8, "and a claim cell per loft");
    {
        float nxs[LEVEL_BOXES_MAX_RESOURCE_NODES], nzs[LEVEL_BOXES_MAX_RESOURCE_NODES];
        float cxs[LEVEL_BOXES_MAX_CLAIM_CELLS], czs[LEVEL_BOXES_MAX_CLAIM_CELLS];
        int amt[LEVEL_BOXES_MAX_RESOURCE_NODES], rad[LEVEL_BOXES_MAX_CLAIM_CELLS];
        for (int i = 0; i < g_level.resource_node_count; i++) {
            nxs[i] = g_level.resource_nodes[i].x; nzs[i] = g_level.resource_nodes[i].z;
            amt[i] = g_level.resource_nodes[i].amount;
        }
        for (int i = 0; i < g_level.claim_cell_count; i++) {
            cxs[i] = g_level.claim_cells[i].x; czs[i] = g_level.claim_cells[i].z;
            rad[i] = g_level.claim_cells[i].radius;
        }
        int loaded = story_ai_empire_load_level(g_level.resource_node_count, nxs, nzs, amt,
                                                g_level.claim_cell_count, cxs, czs, rad);
        ok(loaded == g_level.resource_node_count, "the city's ground loads into the real EMPIRE world");
        ok(fabsf(nxs[0]) + fabsf(nzs[0]) > 50.0f,
           "out at a loft -- not story_ai's default ring at the world origin");
        ok(amt[0] > 0, "with something in it to gather");
    }

    printf("== an upper-floor zombie actually walks ==\n");
    ok(story_ai_unit_archetype(upper_id) == EU_ARCH_ZOMBIE, "the top of a loft is a zombie");
    PlayerState *up = &s.players[upper_id];
    s.players[0].x = up->x + 6.0f;   /* a pursuer needs something to pursue */
    s.players[0].z = up->z;
    s.players[0].y = up->y;
    float x0 = up->x, z0 = up->z;
    unsigned int t = 1000;
    for (int i = 0; i < 60; i++, t += 16) { story_ai_tick(&s, t); walk_step(&s, 0.016f); }
    float moved = sqrtf((up->x - x0) * (up->x - x0) + (up->z - z0) * (up->z - z0));
    ok(moved > 0.5f, "it closed distance on the player instead of standing in the furniture");
    ok(fabsf(up->y - upper_y) < 0.001f, "and stayed on its own floor (no falling through the deck)");

    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
