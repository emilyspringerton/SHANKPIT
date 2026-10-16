/* pheromone_grenade_test.c -- the pheromone thrower's canister (pheromone_grenade.h) on its own, and
 * its live behaviour through witness_ai: flight, bounce, mass, rest, zombies tracking it in the air,
 * and hunger infighting (zombie-on-zombie, zombie-on-citizen, a Man fighting back).
 * Build/run: make test-pheromone-grenade. No physics.h; the level reaches witness_ai through a map hook. */
#include "witness_ai.h"
#include "pheromone_grenade.h"
#include "day_night_clock.h"
#include "../reflux/reflux_runtime.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float x, y, z, w, h, d; } TBox;
static TBox boxes[8];
static int nboxes = 1;
static int push_cb(void *u, const float c[3], float r, float n[3], float *pen) {
    (void)u;
    float best = 0; int hit = 0;
    for (int i = 1; i < nboxes; i++) {
        float nn[3], d;
        if (pg_sphere_aabb(c, r, boxes[i].x, boxes[i].y, boxes[i].z, boxes[i].w, boxes[i].h, boxes[i].d, nn, &d) && d > best) {
            best = d; n[0] = nn[0]; n[1] = nn[1]; n[2] = nn[2]; hit = 1;
        }
    }
    *pen = best; return hit;
}
static int map_cb(const void **b) { *b = boxes; return nboxes; }

static void fly(PgGrenade *g, float seconds) {
    for (float t = 0; t < seconds && !g->landed; t += PG_FIXED_DT) pg_step(g, PG_FIXED_DT, push_cb, NULL, 0.0f);
}

static ServerState S;
static void walk(float dt) {
    for (int i = 1; i < MAX_CLIENTS; i++) {
        PlayerState *p = &S.players[i];
        if (!p->active || p->state == STATE_DEAD) continue;
        float r = p->yaw * 0.0174533f;
        p->x += sinf(r) * p->in_fwd * 20.0f * dt;
        p->z += cosf(r) * p->in_fwd * 20.0f * dt;
    }
}

int main(void) {
    const float org[3] = { 0, 4.6f, 0 };
    PgGrenade g;

    /* ---- ballistic flight: goes forward, comes down, bounces, comes to rest on the floor ---- */
    nboxes = 1;
    pg_throw(&g, org, 0.0f, 0.0f, NULL, PG_MASS_KG, PG_THROW_IMPULSE);
    float peak = org[1];
    for (float t = 0; t < 8 && !g.landed; t += PG_FIXED_DT) { pg_step(&g, PG_FIXED_DT, push_cb, NULL, 0.0f); if (g.pos[1] > peak) peak = g.pos[1]; }
    assert(g.landed);
    assert(peak > org[1] + 1.5f);                    /* it arced up */
    assert(g.pos[2] > 12.0f && fabsf(g.pos[0]) < 0.01f); /* went the way it was thrown (yaw 0 = +z) */
    assert(fabsf(g.pos[1] - PG_RADIUS) < 0.05f);     /* resting on the floor */
    assert(g.bounces >= 1);                          /* it BOUNCED, it did not just stop */
    printf("PASS: canister arcs (peak %.1f), bounces %d times, rests on the floor %.1f units out\n", peak, g.bounces, g.pos[2]);

    /* ---- mass: same impulse, heavier canister leaves slower, lands shorter, bounces lower ---- */
    PgGrenade light, heavy;
    pg_throw(&light, org, 0, 0, NULL, 0.4f, PG_THROW_IMPULSE);
    pg_throw(&heavy, org, 0, 0, NULL, 4.0f, PG_THROW_IMPULSE);
    assert(pg_launch_speed(0.4f, PG_THROW_IMPULSE) > pg_launch_speed(4.0f, PG_THROW_IMPULSE) * 5.0f);
    assert(pg_restitution(0.4f) > pg_restitution(4.0f));
    fly(&light, 8); fly(&heavy, 8);
    assert(light.pos[2] > heavy.pos[2] + 10.0f);
    printf("PASS: mass matters: 0.4 kg lands %.1f out, 4 kg lands %.1f out\n", light.pos[2], heavy.pos[2]);

    /* ---- the thrower's own motion is inherited ---- */
    PgGrenade run, still; float inh[3] = { 0, 0, 15.0f };
    pg_throw(&run, org, 0, 0, inh, PG_MASS_KG, PG_THROW_IMPULSE);
    pg_throw(&still, org, 0, 0, NULL, PG_MASS_KG, PG_THROW_IMPULSE);
    fly(&run, 8); fly(&still, 8);
    assert(run.pos[2] > still.pos[2] + 5.0f);
    printf("PASS: a running throw carries further (%.1f vs %.1f)\n", run.pos[2], still.pos[2]);

    /* ---- a wall: it hits and comes back, it does not tunnel through ---- */
    nboxes = 2;
    boxes[1] = (TBox){ 0.0f, 10.0f, 18.0f, 40.0f, 20.0f, 2.0f }; /* wall across z in [17,19] */
    pg_throw(&g, org, 0, 0, NULL, PG_MASS_KG, PG_THROW_IMPULSE);
    fly(&g, 8);
    assert(g.landed);
    assert(g.pos[2] < 17.0f);                        /* on the near side of the wall */
    assert(g.bounces >= 1);
    printf("PASS: canister bounces off a wall and stays on the near side (z=%.1f)\n", g.pos[2]);

    /* ---- deterministic ---- */
    PgGrenade a, b;
    pg_throw(&a, org, 33.0f, 5.0f, NULL, PG_MASS_KG, PG_THROW_IMPULSE);
    pg_throw(&b, org, 33.0f, 5.0f, NULL, PG_MASS_KG, PG_THROW_IMPULSE);
    fly(&a, 8); fly(&b, 8);
    assert(a.pos[0] == b.pos[0] && a.pos[2] == b.pos[2]);
    printf("PASS: same throw, same flight\n");

    /* ---- live: the marker rides the canister, and zombies track it while it is airborne ---- */
    witness_ai_set_map_hook(map_cb);
    nboxes = 1;
    memset(&S, 0, sizeof S);
    S.game_mode = MODE_ZOMBIES;
    S.players[0].active = 1; S.players[0].state = STATE_ALIVE; S.players[0].yaw = 0.0f;
    witness_ai_reset(77u, 0);
    unsigned int t = 1000;
    assert(witness_ai_try_throw_pheromone(&S, 0, t) == 1);
    S.players[0].x = 5000; S.players[0].z = 5000;                  /* hero far away: only the canister can lure */
    int zid = witness_ai_spawn_zombie(&S, 40.0f, 0.0f, 20.0f, 0);
    witness_ai_force_zombie_mood(zid, 2);
    WitnessAiPheromoneView v;
    assert(witness_ai_pheromone_view(0, &v) && !v.landed);
    float v0z = v.z; int moved_in_air = 0;
    for (int i = 0; i < 30; i++) {
        t += 33; witness_ai_tick(&S, t); walk(0.033f);
        if (witness_ai_pheromone_view(0, &v) && !v.landed && v.z > v0z + 3.0f) moved_in_air = 1;
    }
    assert(moved_in_air);                                           /* the canister really travels */
    for (int i = 0; i < 200; i++) { t += 33; witness_ai_tick(&S, t); walk(0.033f); }
    assert(witness_ai_pheromone_view(0, &v) && v.landed);
    assert(v.trail_n > 0);
    float dx = S.players[zid].x - v.x, dz = S.players[zid].z - v.z;
    assert(sqrtf(dx * dx + dz * dz) < 8.0f);                        /* the zombie walked to where it came to rest */
    printf("PASS: marker rides the canister; zombie ends at it (canister rested at %.1f,%.1f)\n", v.x, v.z);

    /* ---- infighting: a starving zombie bites a citizen; the citizen dies ---- */
    memset(&S, 0, sizeof S);
    S.game_mode = MODE_ZOMBIES;
    S.players[0].active = 1; S.players[0].state = STATE_ALIVE;
    S.players[0].x = 5000; S.players[0].z = 5000;
    witness_ai_reset(78u, 0);
    zid = witness_ai_spawn_zombie(&S, 0.0f, 0.0f, 0.0f, 0);
    int cid = witness_ai_spawn_citizen(&S, ZONE_PUBLIC, 50, 50, 20.0f, 0.0f, 0.0f, 0);
    assert(zid > 0 && cid > 0);
    S.players[cid].in_fwd = 0.0f;
    int teleported = 0;
    t = 1000;
    for (int i = 0; i < 6000 && S.players[cid].state != STATE_DEAD; i++) {
        t += 33;
        witness_ai_tick(&S, t);
        S.players[cid].in_fwd = 0.0f; /* a sitting duck: this test is about the bite, not fleeing */
        if (!teleported && witness_ai_zombie_hunger(zid) >= WITNESS_AI_ZOMBIE_STARVING_HUNGER) { /* it wandered somewhere... a citizen is nearby */
            S.players[cid].x = S.players[zid].x + 25.0f; S.players[cid].z = S.players[zid].z; teleported = 1;
        }
        walk(0.033f);
        if (getenv("PG_DEBUG") && i % 300 == 0) fprintf(stderr, "t=%u hunger=%.2f prey=%d mood=%d z=(%.1f,%.1f) c=(%.1f,%.1f) hp=%d\n", t, witness_ai_zombie_hunger(zid), witness_ai_zombie_has_prey(zid), (int)0, S.players[zid].x, S.players[zid].z, S.players[cid].x, S.players[cid].z, S.players[cid].health);
        if (i == 5) assert(witness_ai_zombie_hunger(zid) < WITNESS_AI_ZOMBIE_STARVING_HUNGER); /* not hungry yet: no fight */
    }
    assert(S.players[cid].state == STATE_DEAD);
    assert(t - 1000 > 20000);                                       /* it took real hunger to get there, not instant */
    printf("PASS: hunger drove a zombie to kill a citizen after %.0f s\n", (t - 1000) / 1000.0f);

    /* ---- zombies fight each other, and the bitten one fights back ---- */
    memset(&S, 0, sizeof S);
    S.game_mode = MODE_ZOMBIES;
    S.players[0].active = 1; S.players[0].state = STATE_ALIVE;
    S.players[0].x = 5000; S.players[0].z = 5000;
    witness_ai_reset(79u, 0);
    int z1 = witness_ai_spawn_zombie(&S, 0.0f, 0.0f, 0.0f, 0);
    int z2 = witness_ai_spawn_zombie(&S, 12.0f, 0.0f, 0.0f, 0);
    t = 1000;
    int h1 = S.players[z1].health, h2 = S.players[z2].health, both_hurt = 0, tele2 = 0;
    for (int i = 0; i < 9000; i++) {
        t += 33; witness_ai_tick(&S, t); walk(0.033f);
        if (!tele2 && witness_ai_zombie_hunger(z1) >= WITNESS_AI_ZOMBIE_STARVING_HUNGER) { S.players[z2].x = S.players[z1].x + 20.0f; S.players[z2].z = S.players[z1].z; tele2 = 1; }
        if (S.players[z1].health < h1 && S.players[z2].health < h2) { both_hurt = 1; break; }
        if (S.players[z1].state == STATE_DEAD || S.players[z2].state == STATE_DEAD) break;
    }
    assert(both_hurt || S.players[z1].state == STATE_DEAD || S.players[z2].state == STATE_DEAD);
    printf("PASS: starving zombies turned on each other (hp %d/%d)\n", S.players[z1].health, S.players[z2].health);

    /* ---- a Man fights a zombie off ---- */
    memset(&S, 0, sizeof S);
    S.game_mode = MODE_ZOMBIES;
    S.players[0].active = 1; S.players[0].state = STATE_ALIVE;
    S.players[0].x = 5000; S.players[0].z = 5000;
    witness_ai_reset(80u, 0);
    int man = witness_ai_spawn_the_men(&S, ZONE_PUBLIC, 80, 50, 0.0f, 0.0f, 0.0f, 0);
    int zz = witness_ai_spawn_zombie(&S, 8.0f, 0.0f, 0.0f, 0);
    assert(man > 0 && zz > 0);
    t = 1000;
    for (int i = 0; i < 600 && S.players[zz].state != STATE_DEAD && S.players[man].state != STATE_DEAD; i++) {
        t += 33; witness_ai_tick(&S, t); walk(0.033f);
    }
    assert(S.players[zz].health < 100 || S.players[zz].state == STATE_DEAD);
    printf("PASS: the Man engaged the zombie (zombie hp %d, man hp %d)\n", S.players[zz].health, S.players[man].health);

    printf("ALL PASS\n");
    return 0;
}
