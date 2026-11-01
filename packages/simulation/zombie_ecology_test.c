/* zombie_ecology_test.c -- the population the founder asked for: zombies with real classes and
 * levels whose bodies carry the DNA the lab reads, the bioslurry a kill can leave, and citizens
 * who are living their own lives (and are sometimes somebody else's).
 *
 * Pure modules only -- zombie_taxonomy.c, bioslurry.c, citizen_life.c -- so this runs with no
 * physics, no map and no clock. make test-zombie-ecology
 */
#include "zombie_taxonomy.h"
#include "bioslurry.h"
#include "citizen_life.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static ServerState g_srv;

static void world(float hero_x) {
    memset(&g_srv, 0, sizeof(g_srv));
    g_srv.scene_id = SCENE_CITY;
    for (int i = 0; i < MAX_CLIENTS; i++) { g_srv.players[i].id = i; g_srv.players[i].team_id = -1; }
    g_srv.players[0].active = 1;
    g_srv.players[0].state = STATE_ALIVE;
    g_srv.players[0].health = 100;
    g_srv.players[0].scene_id = SCENE_CITY;
    g_srv.players[0].x = hero_x;
    g_srv.players[0].y = 0.0f;
    g_srv.players[0].z = 0.0f;
    bioslurry_reset();
}

int main(void) {
    ZombieProfile p, q;

    /* ---- 1. the classes are real bodies, and WALKER is exactly the zombie that already existed */
    printf("== zombie taxonomy ==\n");
    for (int i = 0; i < ZCLASS_COUNT; i++) {
        const char *n = zombie_taxonomy_class_name(i);
        CHECK(n != NULL && n[0] != 0);
        for (int j = 0; j < i; j++)
            CHECK(strcmp(n, zombie_taxonomy_class_name(j)) != 0);
    }
    zombie_taxonomy_profile(&p, ZCLASS_WALKER, 1);
    CHECK(p.max_health == 100);      /* the pre-existing single zombie, unchanged */
    CHECK(p.melee_damage == 14);     /* WITNESS_AI_ZOMBIE_MELEE_DAMAGE */
    CHECK(p.move_permille == 1000);
    printf("WALKER lv1: %d hp, %d dmg, move %d/1000\n", p.max_health, p.melee_damage, p.move_permille);

    /* every class has a distinct body, and the archetypes read the way their names promise */
    zombie_taxonomy_profile(&p, ZCLASS_SHAMBLER, 1);
    zombie_taxonomy_profile(&q, ZCLASS_ALPHA, 1);
    CHECK(q.max_health > p.max_health);
    CHECK(q.melee_damage > p.melee_damage);
    CHECK(q.dna_quality > p.dna_quality);
    CHECK(q.slurry_permille > p.slurry_permille);
    zombie_taxonomy_profile(&p, ZCLASS_RUNNER, 1);
    zombie_taxonomy_profile(&q, ZCLASS_WALKER, 1);
    CHECK(p.move_permille > q.move_permille);
    CHECK(p.max_health < q.max_health);
    zombie_taxonomy_profile(&p, ZCLASS_BRUTE, 1);
    zombie_taxonomy_profile(&q, ZCLASS_WALKER, 1);
    CHECK(p.melee_damage > q.melee_damage && p.move_permille < q.move_permille);

    /* levels are real and monotonic, and the clamps are total */
    zombie_taxonomy_profile(&p, ZCLASS_BRUTE, 1);
    zombie_taxonomy_profile(&q, ZCLASS_BRUTE, 9);
    CHECK(q.max_health > p.max_health && q.melee_damage > p.melee_damage && q.dna_quality > p.dna_quality);
    zombie_taxonomy_profile(&p, ZCLASS_BRUTE, 99);
    CHECK(p.level == ZOMBIE_MAX_LEVEL);
    zombie_taxonomy_profile(&p, -5, -3);
    CHECK(p.level == 1 && p.cls == ZCLASS_SHAMBLER);
    zombie_taxonomy_profile(&p, ZCLASS_ALPHA, 12);
    CHECK(p.dna_quality == 100);   /* capped, never over */
    printf("ALPHA lv12: %d hp, %d dmg, dna %d (%s), slurry %d/1000\n",
           p.max_health, p.melee_damage, p.dna_quality, zombie_taxonomy_trait_name(p.dna_trait), p.slurry_permille);

    /* DNA is the body's own identity */
    zombie_taxonomy_profile(&p, ZCLASS_RUNNER, 4);
    CHECK(p.dna_trait == ZTRAIT_SPEED);
    zombie_taxonomy_profile(&p, ZCLASS_BRUTE, 4);
    CHECK(p.dna_trait == ZTRAIT_ARMOR);
    zombie_taxonomy_profile(&p, ZCLASS_ALPHA, 4);
    CHECK(p.dna_trait == ZTRAIT_SCENT);
    CHECK(strcmp(zombie_taxonomy_trait_name(ZTRAIT_SCENT), "SCENT") == 0);

    /* ---- 2. spawn rolls are deterministic, and the night is different from the day */
    {
        int night_classes[ZCLASS_COUNT] = {0};
        int day_classes[ZCLASS_COUNT] = {0};
        for (uint32_t seed = 1; seed <= 400; seed++) {
            zombie_taxonomy_roll(&p, seed, 1);
            ZombieProfile again;
            zombie_taxonomy_roll(&again, seed, 1);
            CHECK(again.cls == p.cls && again.level == p.level && again.max_health == p.max_health);
            CHECK(p.level >= 1 && p.level <= ZOMBIE_MAX_LEVEL);
            night_classes[p.cls]++;
            zombie_taxonomy_roll(&p, seed, 0);
            day_classes[p.cls]++;
            /* day has no BRUTE and no ALPHA in its own weight table */
            CHECK(p.cls != ZCLASS_BRUTE && p.cls != ZCLASS_ALPHA);
        }
        int kinds = 0;
        for (int i = 0; i < ZCLASS_COUNT; i++) if (night_classes[i] > 0) kinds++;
        CHECK(kinds == ZCLASS_COUNT);
        printf("rolls: day %d/%d/%d/%d/%d, night %d/%d/%d/%d/%d (shambler..alpha)\n",
               day_classes[0], day_classes[1], day_classes[2], day_classes[3], day_classes[4],
               night_classes[0], night_classes[1], night_classes[2], night_classes[3], night_classes[4]);
    }

    /* ---- 3. the drop is a function of the body, not a second dice roll nobody can reproduce */
    {
        zombie_taxonomy_profile(&p, ZCLASS_ALPHA, 6);
        int drops = 0, twice = 0;
        for (uint32_t seed = 1; seed <= 200; seed++) {
            int a = zombie_taxonomy_slurry_roll(seed, &p);
            int b = zombie_taxonomy_slurry_roll(seed, &p);
            if (a == b) twice++;
            drops += a;
        }
        CHECK(twice == 200);
        CHECK(drops > 100);                       /* an ALPHA usually leaves something */
        ZombieProfile never;
        zombie_taxonomy_profile(&never, ZCLASS_WALKER, 3);
        never.slurry_permille = 0;
        CHECK(zombie_taxonomy_slurry_roll(7u, &never) == 0);
        never.slurry_permille = 1000;
        CHECK(zombie_taxonomy_slurry_roll(7u, &never) == 1);
        printf("drops: ALPHA left %d canisters in 200 kills, always the same answer twice\n", drops);
    }

    /* ---- 4. bioslurry: on the ground, in your hands, and only sometimes worth the trip ---- */
    printf("== bioslurry ==\n");
    world(0.0f);
    int idx = bioslurry_drop(SCENE_CITY, 1.0f, 0.0f, 0.0f, ZCLASS_ALPHA, 8, 1000u);
    CHECK(idx >= 0);
    CHECK(bioslurry_drop_count() == 1);
    CHECK(bioslurry_drop_at(idx)->dna_quality == 100);
    CHECK(bioslurry_collect(&g_srv, 1016u) == 1);
    CHECK(bioslurry_carried(0) == 1);
    CHECK(bioslurry_drop_count() == 0);
    /* an ALPHA strand reads: 100% grade, and the trait is the alpha's own */
    CHECK(bioslurry_deliver(0, 1100u) == 1);
    CHECK(bioslurry_dna_count() == 1);
    CHECK(bioslurry_dna_at(0)->quality == 100);
    CHECK(bioslurry_dna_at(0)->trait == ZTRAIT_SCENT);
    CHECK(bioslurry_dna_at(0)->level == 8);
    CHECK(bioslurry_carried(0) == 0);
    CHECK(bioslurry_deliver(0, 1200u) == -1);          /* nothing in hand */
    CHECK(bioslurry_delivered_total() == 1 && bioslurry_harvested_total() == 1);
    printf("dna: ALPHA lv8 -> quality %d, trait %s, level %d\n",
           bioslurry_dna_at(0)->quality, zombie_taxonomy_trait_name(bioslurry_dna_at(0)->trait),
           bioslurry_dna_at(0)->level);

    /* a shambler is a wasted trip -- under the usable grade, every time */
    world(0.0f);
    for (int i = 0; i < 6; i++) {
        bioslurry_drop(SCENE_CITY, 0.5f, 0.0f, 0.0f, ZCLASS_SHAMBLER, 1, 2000u);
        bioslurry_collect(&g_srv, 2016u);
    }
    CHECK(bioslurry_carried(0) == BIOSLURRY_MAX_CARRIED);
    int read = 0;
    for (int i = 0; i < BIOSLURRY_MAX_CARRIED; i++) read += bioslurry_deliver(0, 2100u) == 1;
    CHECK(read == 0);
    CHECK(bioslurry_harvested_total() == 0);
    CHECK(bioslurry_delivered_total() == BIOSLURRY_MAX_CARRIED);
    printf("dna: %d shambler canisters delivered, 0 usable strands\n", BIOSLURRY_MAX_CARRIED);

    /* rot, scene scoping and the pool cap */
    world(0.0f);
    bioslurry_drop(SCENE_CITY, 0.5f, 0.0f, 0.0f, ZCLASS_WALKER, 2, 5000u);
    bioslurry_tick(&g_srv, 5000u + BIOSLURRY_ROT_MS);
    CHECK(bioslurry_drop_count() == 0);
    bioslurry_drop(SCENE_VOXWORLD, 0.5f, 0.0f, 0.0f, ZCLASS_WALKER, 2, 6000u);
    CHECK(bioslurry_collect(&g_srv, 6016u) == 0);      /* another place's canister */
    for (int i = 0; i < BIOSLURRY_MAX_DROPS + 3; i++)
        bioslurry_drop(SCENE_CITY, 100.0f, 0.0f, 0.0f, ZCLASS_WALKER, 2, 7000u);
    CHECK(bioslurry_drop_count() == BIOSLURRY_MAX_DROPS);
    /* out of reach is not a pickup */
    world(0.0f);
    g_srv.players[0].x = 50.0f;
    bioslurry_drop(SCENE_CITY, 0.0f, 0.0f, 0.0f, ZCLASS_WALKER, 2, 8000u);
    CHECK(bioslurry_collect(&g_srv, 8016u) == 0);

    /* ---- 5. citizens have lives, personalities, and occasionally a secret ---- */
    printf("== citizen lives ==\n");
    {
        CitizenLife life;
        int paths[CILIFE_PATH_COUNT] = {0};
        int spies = 0;
        for (uint32_t seed = 1; seed <= 600; seed++) {
            citizen_life_roll(&life, seed, (int)(seed % 60), 30, 40, 0.0f, 0.0f);
            CHECK(life.industry >= 0 && life.industry <= 100);
            CHECK(life.commerce >= 0 && life.commerce <= 100);
            CHECK(life.ambition >= 0 && life.ambition <= 100);
            CHECK(life.bravery >= 0 && life.bravery <= 100);
            CHECK(life.sociability >= 0 && life.sociability <= 100);
            CHECK(life.path >= 0 && life.path < CILIFE_PATH_COUNT);
            CHECK(life.handle[0] != 0);
            CHECK(strcmp(citizen_life_path_name(life.path), "IDLE") != 0 || life.path == CILIFE_IDLE);
            paths[life.path]++;
            if (life.spy) spies++;
        }
        for (int i = 0; i < CILIFE_PATH_COUNT; i++) CHECK(paths[i] > 0);
        CHECK(spies > 0 && spies < 200);     /* rare, not absent */
        printf("lives: %d/%d/%d/%d/%d across %d citizens, %d of them secretly somebody\n",
               paths[0], paths[1], paths[2], paths[3], paths[4], 600, spies);

        /* the same body is the same person */
        CitizenLife a, b;
        citizen_life_roll(&a, 99u, 4, 30, 40, 0.0f, 0.0f);
        citizen_life_roll(&b, 99u, 4, 30, 40, 0.0f, 0.0f);
        CHECK(a.path == b.path && a.spy == b.spy && strcmp(a.handle, b.handle) == 0);

        /* a citizen walks to its post instead of standing in the street */
        PlayerState ps;
        memset(&ps, 0, sizeof(ps));
        ps.state = STATE_ALIVE;
        ps.x = 0.0f; ps.z = 0.0f;
        CitizenLife walker;
        citizen_life_roll(&walker, 7u, 3, 20, 30, 0.0f, 0.0f);
        walker.goal_until_ms = 0;
        int moved = 0, stood = 0;
        float last_x = ps.x, last_z = ps.z;
        for (unsigned int t = 0; t < 60000u; t += 16u) {
            citizen_life_tick(&walker, &ps, t);
            ps.x += sinf(ps.yaw * 0.0174533f) * ps.in_fwd;
            ps.z += cosf(ps.yaw * 0.0174533f) * ps.in_fwd;
            if (ps.x != last_x || ps.z != last_z) moved++;
            if (ps.in_fwd == 0.0f) stood++;
            last_x = ps.x; last_z = ps.z;
        }
        /* A life, not a treadmill: it leaves home to be somewhere, and it is not always walking. */
        CHECK(moved > 0);
        CHECK(stood > 0);
        CHECK(ps.x != 0.0f || ps.z != 0.0f);
        CHECK(sqrtf(ps.x * ps.x + ps.z * ps.z) > 3.0f);
        printf("lives: a %s citizen walked %.1f units from home in a minute\n",
               citizen_life_path_name(walker.path), sqrtf(ps.x * ps.x + ps.z * ps.z));
    }

    if (g_fail) { printf("\nFAILED: %d check(s)\n", g_fail); return 1; }
    printf("\nALL PASS\n");
    return 0;
}
