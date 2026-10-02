/* witness_ai_zombies_test.c -- MODE_ZOMBIES lifecycle, wall awareness, wall clawing, birds.
 * Build/run (see Makefile target test-witness-ai-zombies). No physics.h: witness_ai.c reaches the world
 * only through its map / wall-hit hooks, which this test supplies. */
#include "witness_ai.h"
#include "day_night_clock.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static ServerState S;
typedef struct { float x, y, z, w, h, d; } TBox;
static TBox boxes[8];
static int nboxes = 1;
static int map_cb(const void **b) { *b = boxes; return nboxes; }
static int hits; static float hit_nx, hit_nz, hit_x;
static void hit_cb(int scene, float hx, float hy, float hz, float nx, float ny, float nz, int dmg) {
    (void)scene; (void)hy; (void)hz; (void)ny; (void)dmg; hits++; hit_x = hx; hit_nx = nx; hit_nz = nz;
}
/* integrate bots the way local_game.h does, minimally: walk along yaw (sin, cos) */
static void walk(float dt) {
    for (int i = 1; i < MAX_CLIENTS; i++) {
        PlayerState *p = &S.players[i];
        if (!p->active || p->state == STATE_DEAD || i == 0) continue;
        float r = p->yaw * 0.0174533f;
        p->x += sinf(r) * p->in_fwd * 20.0f * dt;
        p->z += cosf(r) * p->in_fwd * 20.0f * dt;
    }
}
static int count_role(int role) {
    int n = 0;
    for (int i = 1; i < MAX_CLIENTS; i++) if (S.players[i].active && witness_ai_role_for_player(i) == role) n++;
    return n;
}

int main(void) {
    witness_ai_set_map_hook(map_cb);
    witness_ai_set_wall_hit_hook(hit_cb);

    /* ---- birds are there from the very first tick, day or night ---- */
    memset(&S, 0, sizeof S);
    S.game_mode = MODE_ZOMBIES;
    S.players[0].active = 1; S.players[0].state = STATE_ALIVE;
    day_night_clock_init(&S.story_clock, 1u, 10); /* 10:00, DAY */
    witness_ai_reset(1u, 0);
    witness_ai_zombies_tick(&S, 1000);
    assert(witness_ai_bird_count() >= 5);
    assert(count_role(WITNESS_AI_ROLE_BIRD) >= 5);
    printf("PASS: birds seeded on the first tick (%d)\n", witness_ai_bird_count());

    /* ---- day/night lifecycle: zombies rise at night, burn off by day ---- */
    unsigned int t = 1000;
    for (int i = 0; i < 400; i++) { t += 3000; witness_ai_zombies_tick(&S, t); witness_ai_tick(&S, t); }
    int day_z = count_role(WITNESS_AI_ROLE_ZOMBIE), day_c = count_role(WITNESS_AI_ROLE_CITIZEN);
    assert(day_c >= 6);
    assert(day_z <= 2);
    day_night_clock_init(&S.story_clock, 1u, 23); /* 23:00, NIGHT */
    assert(day_night_clock_phase(&S.story_clock) == DNC_NIGHT);
    for (int i = 0; i < 400; i++) { t += 3000; witness_ai_zombies_tick(&S, t); witness_ai_tick(&S, t); }
    int night_z = count_role(WITNESS_AI_ROLE_ZOMBIE), night_c = count_role(WITNESS_AI_ROLE_CITIZEN);
    assert(night_z >= 8);
    assert(night_c <= 3);
    printf("PASS: lifecycle day z=%d c=%d -> night z=%d c=%d\n", day_z, day_c, night_z, night_c);
    day_night_clock_init(&S.story_clock, 1u, 10);
    for (int i = 0; i < 600; i++) { t += 3000; witness_ai_zombies_tick(&S, t); witness_ai_tick(&S, t); }
    assert(count_role(WITNESS_AI_ROLE_ZOMBIE) <= 2);
    printf("PASS: horde burned off at day (z=%d)\n", count_role(WITNESS_AI_ROLE_ZOMBIE));

    /* ---- wall awareness: a citizen fleeing a zombie behind a wall must not grind into it ---- */
    memset(&S, 0, sizeof S);
    S.game_mode = MODE_ZOMBIES;
    S.players[0].active = 1; S.players[0].state = STATE_ALIVE;
    S.players[0].x = 500; S.players[0].z = 500; /* hero far away: nothing else interferes */
    witness_ai_reset(2u, 0);
    nboxes = 2;
    boxes[1] = (TBox){ 30.0f, 20.0f, 0.0f, 4.0f, 40.0f, 80.0f }; /* a building face at x=28..32, z -40..40 */
    int cid = witness_ai_spawn_citizen(&S, ZONE_PUBLIC, 40, 30, 20.0f, 0.0f, 0.0f, 0);
    int zid = witness_ai_spawn_zombie(&S, 10.0f, 0.0f, 0.0f, 0); /* zombie WEST of the citizen: flee east, into the wall */
    assert(cid > 0 && zid > 0);
    witness_ai_force_zombie_mood(zid, 2 /* HUNTING */);
    S.players[0].x = 10.0f; S.players[0].z = 0.0f; /* hero beside the zombie so it keeps HUNTING state, harmless */
    float maxx = -1e9f;
    for (int i = 0; i < 400; i++) {
        t += 50;
        S.players[zid].x = 10.0f; S.players[zid].z = 0.0f;             /* pin the threat */
        witness_ai_tick(&S, t);
        walk(0.05f);
        if (S.players[cid].x > maxx) maxx = S.players[cid].x;
    }
    /* body radius is 1.6 around a face at x=28: the citizen must stay out of the box + margin */
    assert(maxx < 28.0f + 0.5f);
    printf("PASS: fleeing citizen stopped/steered at the wall (max x=%.2f, face at 28)\n", maxx);

    /* ---- zombies claw brick walls that block the way to their target ---- */
    memset(&S, 0, sizeof S);
    S.game_mode = MODE_ZOMBIES;
    S.players[0].active = 1; S.players[0].state = STATE_ALIVE;
    S.players[0].x = 60.0f; S.players[0].y = 0.0f; S.players[0].z = 0.0f; /* hero behind the wall; sandbox zombies smell from 400 */
    witness_ai_reset(3u, 0);
    zid = witness_ai_spawn_zombie(&S, 10.0f, 0.0f, 0.0f, 0);
    witness_ai_force_zombie_mood(zid, 2);
    hits = 0;
    for (int i = 0; i < 300; i++) {
        t += 50;
        witness_ai_tick(&S, t);
        walk(0.05f);
    }
    assert(hits >= 3);
    assert(hit_nx < -0.9f && fabsf(hit_nz) < 0.1f);   /* struck the west face, normal points back at the zombie */
    assert(fabsf(hit_x - 28.0f) < 0.6f);
    assert(S.players[zid].x < 28.0f);                 /* and did not walk through it */
    printf("PASS: zombie clawed the wall %d times, face x=%.2f normal=(%.0f,%.0f)\n", hits, hit_x, hit_nx, hit_nz);

    /* ---- card #486: a sandbox zombie always hunts, however far the hero is ---- */
    memset(&S, 0, sizeof S);
    S.game_mode = MODE_ZOMBIES;
    S.players[0].active = 1; S.players[0].state = STATE_ALIVE;
    S.players[0].x = 900.0f; S.players[0].z = 0.0f;
    nboxes = 1;
    witness_ai_reset(4u, 0);
    zid = witness_ai_spawn_zombie(&S, 0.0f, 0.0f, 0.0f, 0);
    witness_ai_force_zombie_mood(zid, 2);
    for (int i = 0; i < 40; i++) { t += 50; witness_ai_tick(&S, t); walk(0.05f); }
    assert(S.players[zid].in_fwd > 0.0f);          /* chasing from 900 away (old radius was 400) */
    assert(S.players[zid].x > 0.5f);               /* and actually moving toward the hero (+x) */
    printf("PASS: zombie 900 away from the hero still hunts (x=%.1f)\n", S.players[zid].x);

    /* ---- card #486: with a building available, zombies spawn out of the hero's sight ---- */
    memset(&S, 0, sizeof S);
    S.game_mode = MODE_ZOMBIES;
    S.players[0].active = 1; S.players[0].state = STATE_ALIVE;
    S.players[0].x = 0.0f; S.players[0].z = 0.0f;
    day_night_clock_init(&S.story_clock, 1u, 23);  /* night: wants a horde */
    nboxes = 1; /* index 0 is skipped by the walker; boxes[1..] are real */
    boxes[1] = (TBox){ 0.0f, 10.0f, 80.0f, 200.0f, 30.0f, 8.0f }; /* a long building across +z, 80 away */
    nboxes = 2;
    witness_ai_reset(5u, 0);
    for (int i = 0; i < 60; i++) { t += 3000; witness_ai_zombies_tick(&S, t); }
    int seen = 0, total = 0;
    for (int i = 1; i < MAX_CLIENTS; i++) {
        PlayerState *zp = &S.players[i];
        if (!zp->active || witness_ai_role_for_player(i) != WITNESS_AI_ROLE_ZOMBIE) continue;
        total++;
        /* visible = on the hero's side of the building plane and not walled off from the hero */
        if (zp->z < 70.0f && zp->z > -1000.0f) {
            seen++;
        }
    }
    assert(total >= 6);
    assert(seen == 0 || seen * 4 < total);          /* the hidden-first spawner put (nearly) all of them behind the building */
    printf("PASS: %d/%d zombies spawned in the hero's open view (rest hidden behind the building)\n", seen, total);
    nboxes = 1;

    printf("ALL PASS\n");
    return 0;
}
