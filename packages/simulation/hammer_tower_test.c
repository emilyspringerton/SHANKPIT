/* hammer_tower_test.c -- WPN_HAMMER's E power: the 3.0 s cast (still for the first 2.6 s, the
 * FFXI release in the last 0.4 s), and the mini gatling tower it drops -- zombies always, The Men
 * and citizens only once they are in a fight, deliberately weak, expiring, and never aimed at the
 * player. make test-hammer-tower
 *
 * Links the REAL physics.h (the cast, through the real update_weapons) and the REAL witness_ai.c
 * (the role/combat queries the targeting rule is built on), so nothing here is a copy of the rule.
 */
#include "physics.h"
#include "witness_ai.h"
#include "hammer_tower.h"

#include <stdio.h>
#include <string.h>

/* physics.h's own two bot-league hooks -- same stubs hammer_test.c links with. */
PlayerState *get_best_bot(void) { return NULL; }
void evolve_bot(PlayerState *a, PlayerState *b) { (void)a; (void)b; }

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static PlayerState g_targets[MAX_CLIENTS];
static Projectile  g_proj[MAX_PROJECTILES];
static ServerState g_srv;

/* ---- the cast ---------------------------------------------------------------------------- */

static void cast_setup(void) {
    memset(g_targets, 0, sizeof(g_targets));
    memset(g_proj, 0, sizeof(g_proj));
    g_targets[0].active = 1;
    g_targets[0].state = STATE_ALIVE;
    g_targets[0].id = 0;
    g_targets[0].scene_id = SCENE_CITY;
    g_targets[0].team_id = -1;
    g_targets[0].health = 100;
    g_targets[0].current_weapon = WPN_HAMMER;
}

static unsigned int g_now = 1000u;

static void tick(float fwd, int ability) {
    g_targets[0].in_fwd = fwd;
    g_now += 16u;
    update_weapons(&g_targets[0], g_targets, g_proj, 0, 0, ability, 0, g_now, 2000);
}

static void run_still(int ticks) {
    for (int i = 0; i < ticks; i++) tick(0.0f, 0);
}

/* ---- the world --------------------------------------------------------------------------- */

static void world_setup(void) {
    memset(&g_srv, 0, sizeof(g_srv));
    g_srv.scene_id = SCENE_CITY;
    g_srv.game_mode = MODE_ZOMBIES;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        g_srv.players[i].id = i;
        g_srv.players[i].team_id = -1;
        g_srv.players[i].y = 0.0f;
    }
    g_srv.players[0].active = 1;
    g_srv.players[0].state = STATE_ALIVE;
    g_srv.players[0].health = 100;
    g_srv.players[0].scene_id = SCENE_CITY;
    witness_ai_reset(4242u, 0);
    hammer_tower_reset();
}

static int spawn_tower(unsigned int now) {
    return hammer_tower_spawn(&g_srv, 0, 0.0f, 0.0f, 0.0f, 0.0f, now);
}

/* Runs the tower at 60 Hz from `from_ms` for `ms` milliseconds. */
static void run_tower(unsigned int *clock, unsigned int ms) {
    unsigned int end = *clock + ms;
    while (*clock < end) {
        *clock += 16;
        hammer_tower_tick(&g_srv, *clock);
    }
}

int main(void) {
    /* 0. the numbers the founder asked for, in real time */
    CHECK(HAMMER_CAST_TICKS == 180);                          /* 3.0 s at 60 Hz */
    CHECK(HAMMER_CAST_STILL_TICKS == 156);                    /* 2.6 s */
    CHECK(HAMMER_CAST_FREE_TICKS == 24);                      /* 0.4 s FFXI release window */

    /* 1. E starts the cast, plants your feet, and spends the cooldown */
    cast_setup();
    g_targets[0].vx = 0.45f; g_targets[0].vz = 0.20f;          /* "auto running" */
    g_now = 1000u;
    tick(0.0f, 1);
    CHECK(g_targets[0].hammer_cast_ticks == HAMMER_CAST_TICKS);
    CHECK(g_targets[0].hammer_tower_ready == 0);
    CHECK(g_targets[0].ability_cooldown == HAMMER_CAST_COOLDOWN);
    CHECK(g_targets[0].vx == 0.0f && g_targets[0].vz == 0.0f); /* you stop running */
    printf("cast: started, %d ticks, cooldown %d, momentum zeroed\n",
           g_targets[0].hammer_cast_ticks, g_targets[0].ability_cooldown);

    /* 2. holding still through the still window completes it */
    run_still(HAMMER_CAST_STILL_TICKS);
    CHECK(g_targets[0].hammer_cast_ticks == HAMMER_CAST_FREE_TICKS);
    CHECK(g_targets[0].hammer_tower_ready == 0);               /* not built yet: 0.4 s to go */
    run_still(HAMMER_CAST_FREE_TICKS);
    CHECK(g_targets[0].hammer_cast_ticks == 0);
    CHECK(g_targets[0].hammer_tower_ready == 1);
    printf("cast: lands after the full 3.0 s of standing still\n");

    /* 3. moving inside the first 2.6 s breaks it, and the cooldown is refunded to the short one */
    cast_setup();
    g_now = 5000u;
    tick(0.0f, 1);
    run_still(10);
    tick(1.0f, 0);                                             /* start walking */
    CHECK(g_targets[0].hammer_cast_ticks == 0);
    CHECK(g_targets[0].hammer_tower_ready == 0);
    CHECK(g_targets[0].ability_cooldown == HAMMER_CAST_CANCEL_COOLDOWN);
    printf("cast: movement at 2.8 s remaining breaks it (cooldown -> %d)\n",
           g_targets[0].ability_cooldown);

    /* 4. FFXI early release: moving inside the LAST 0.4 s does not break it */
    cast_setup();
    g_now = 9000u;
    tick(0.0f, 1);
    run_still(HAMMER_CAST_STILL_TICKS);                        /* down to exactly 24 left */
    CHECK(g_targets[0].hammer_cast_ticks == HAMMER_CAST_FREE_TICKS);
    tick(1.0f, 0);                                             /* start running early */
    CHECK(g_targets[0].hammer_cast_ticks == HAMMER_CAST_FREE_TICKS - 1);
    run_still(HAMMER_CAST_FREE_TICKS - 1);
    CHECK(g_targets[0].hammer_tower_ready == 1);
    printf("cast: running in the last 0.4 s still lands\n");

    /* 5. a dead player cannot build, and the flag is cleared by respawn */
    {
        PlayerState dead;
        memset(&dead, 0, sizeof(dead));
        dead.hammer_tower_ready = 1;
        dead.hammer_cast_ticks = 40;
        phys_respawn(&dead, 0);
        CHECK(dead.hammer_tower_ready == 0);
        CHECK(dead.hammer_cast_ticks == 0);
    }

    /* ---- the tower --------------------------------------------------------------------- */

    /* 6. a zombie is always a target, and one tower is weak on purpose */
    world_setup();
    int zid = witness_ai_spawn_zombie(&g_srv, 10.0f, 0.0f, 0.0f, 0u);
    CHECK(zid > 0);
    CHECK(witness_ai_role_for_player(zid) == WITNESS_AI_ROLE_ZOMBIE);
    int ti = spawn_tower(1000u);
    CHECK(ti >= 0);
    CHECK(hammer_tower_count() == 1);
    {
        unsigned int clock = 1000u;
        run_tower(&clock, 1000u);                              /* windup 0.5 s, then ~3 rounds */
        const HammerTower *t = hammer_tower_at(ti);
        CHECK(t != NULL && t->shots >= 2);
        CHECK(t->target_id == zid);
        CHECK(g_srv.players[zid].health < 100);
        CHECK(g_srv.players[zid].health >= 80);                /* "not that strong for now" */
        printf("tower: zombie shot after %u ms, health %d/100 (%d rounds)\n",
               clock - 1000u, g_srv.players[zid].health, t->shots);
    }

    /* 7. a calm citizen is NOT a target */
    world_setup();
    {
        int cid = witness_ai_spawn_citizen(&g_srv, ZONE_PUBLIC, 20, 30, 8.0f, 0.0f, 0.0f, 0u);
        CHECK(cid > 0);
        CHECK(witness_ai_npc_in_combat(&g_srv, cid) == 0);
        spawn_tower(1000u);
        unsigned int clock = 1000u;
        run_tower(&clock, 3000u);
        CHECK(g_srv.players[cid].health == 100);
        const HammerTower *t = hammer_tower_at(0);
        CHECK(t != NULL && t->shots == 0);
        printf("tower: calm citizen untouched for 3 s\n");
    }

    /* 8. a citizen in combat IS a target (hurt is the combat signal here) */
    world_setup();
    {
        int cid = witness_ai_spawn_citizen(&g_srv, ZONE_PUBLIC, 20, 30, 8.0f, 0.0f, 0.0f, 0u);
        CHECK(cid > 0);
        g_srv.players[cid].health = 50;
        CHECK(witness_ai_npc_in_combat(&g_srv, cid) == 1);
        spawn_tower(1000u);
        unsigned int clock = 1000u;
        run_tower(&clock, 1500u);
        CHECK(g_srv.players[cid].health < 50);
        printf("tower: citizen in combat shot (health %d)\n", g_srv.players[cid].health);
    }

    /* 9. The Men in combat are shot; a closer calm citizen is not */
    world_setup();
    {
        int man = witness_ai_spawn_the_men(&g_srv, ZONE_PUBLIC, 50, 40, 12.0f, 0.0f, 0.0f, 0u);
        int cid = witness_ai_spawn_citizen(&g_srv, ZONE_PUBLIC, 20, 30, 5.0f, 0.0f, 0.0f, 0u);
        CHECK(man > 0 && cid > 0);
        CHECK(witness_ai_role_for_player(man) == WITNESS_AI_ROLE_THE_MEN);
        g_srv.players[man].health = 60;                        /* in combat */
        CHECK(witness_ai_npc_in_combat(&g_srv, man) == 1);
        spawn_tower(1000u);
        unsigned int clock = 1000u;
        run_tower(&clock, 2000u);
        CHECK(g_srv.players[man].health < 60);
        CHECK(g_srv.players[cid].health == 100);
        printf("tower: The Men in combat shot past a closer bystander\n");
    }

    /* 10. priority: a zombie outranks a closer citizen who is in combat */
    world_setup();
    {
        int z = witness_ai_spawn_zombie(&g_srv, 20.0f, 0.0f, 0.0f, 0u);
        int cid = witness_ai_spawn_citizen(&g_srv, ZONE_PUBLIC, 20, 30, 5.0f, 0.0f, 0.0f, 0u);
        g_srv.players[cid].health = 50;
        spawn_tower(1000u);
        unsigned int clock = 1000u;
        run_tower(&clock, 2500u);
        CHECK(g_srv.players[z].health < 100);
        CHECK(g_srv.players[cid].health == 50);
        printf("tower: zombie outranks a closer citizen in combat\n");
    }

    /* 11. the player and plain bots are never targets */
    world_setup();
    {
        g_srv.players[1].active = 1;
        g_srv.players[1].state = STATE_ALIVE;
        g_srv.players[1].health = 100;
        g_srv.players[1].x = 4.0f;
        g_srv.players[1].scene_id = SCENE_CITY;
        CHECK(witness_ai_role_for_player(1) == WITNESS_AI_ROLE_NONE);
        spawn_tower(1000u);
        unsigned int clock = 1000u;
        run_tower(&clock, 2000u);
        CHECK(g_srv.players[0].health == 100);
        CHECK(g_srv.players[1].health == 100);
        CHECK(hammer_tower_at(0) && hammer_tower_at(0)->shots == 0);
        printf("tower: never shoots the player or a role-less bot\n");
    }

    /* 12. out of range: nothing is acquired */
    world_setup();
    {
        int z = witness_ai_spawn_zombie(&g_srv, HAMMER_TOWER_RANGE + 5.0f, 0.0f, 0.0f, 0u);
        spawn_tower(1000u);
        unsigned int clock = 1000u;
        run_tower(&clock, 2000u);
        CHECK(g_srv.players[z].health == 100);
        CHECK(hammer_tower_at(0) && hammer_tower_at(0)->shots == 0);
        printf("tower: zombie just out of range untouched\n");
    }

    /* 13. it expires, and a scene change takes it with it */
    world_setup();
    {
        spawn_tower(1000u);
        hammer_tower_tick(&g_srv, 1000u + HAMMER_TOWER_LIFETIME_MS - 1u);
        CHECK(hammer_tower_count() == 1);
        hammer_tower_tick(&g_srv, 1000u + HAMMER_TOWER_LIFETIME_MS);
        CHECK(hammer_tower_count() == 0);

        spawn_tower(50000u);
        g_srv.scene_id = SCENE_VOXWORLD;
        hammer_tower_tick(&g_srv, 50100u);
        CHECK(hammer_tower_count() == 0);
        hammer_tower_reset();
        CHECK(hammer_tower_count() == 0);
        printf("tower: expires after %u ms; a scene change clears it\n", HAMMER_TOWER_LIFETIME_MS);
    }

    /* 14. a full pool refuses rather than stealing a tower */
    world_setup();
    {
        for (int i = 0; i < HAMMER_TOWER_MAX; i++) CHECK(spawn_tower(1000u) == i);
        CHECK(spawn_tower(1000u) == -1);
        CHECK(hammer_tower_count() == HAMMER_TOWER_MAX);
        printf("tower: pool caps at %d\n", HAMMER_TOWER_MAX);
    }

    /* 15. placement: in front of the caster, on their feet */
    {
        PlayerState p;
        memset(&p, 0, sizeof(p));
        p.x = 10.0f; p.y = 3.0f; p.z = 20.0f; p.yaw = 0.0f;
        float tx = 0, ty = 0, tz = 0;
        CHECK(hammer_tower_place_for_owner(&p, &tx, &ty, &tz) == 1);
        CHECK(fabsf(tx - 10.0f) < 0.001f);
        CHECK(fabsf(tz - (20.0f - HAMMER_TOWER_FORWARD_OFFSET)) < 0.001f);
        CHECK(fabsf(ty - 3.0f) < 0.001f);
        printf("placement: %.1f/%.1f/%.1f from 10/3/20 facing +Z\n", tx, ty, tz);
    }

    if (g_fail) {
        printf("\nFAILED: %d check(s)\n", g_fail);
        return 1;
    }
    printf("\nALL PASS\n");
    return 0;
}
