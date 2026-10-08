/* witness_ai_zombies_test.c -- MODE_ZOMBIES lifecycle, wall awareness, wall clawing, birds,
 * REFLUX eventing (ZOMBIE_SPAWNED/HARVESTED/MOOD_ESCALATED, WITNESS_ESCALATED).
 * Build/run (see Makefile target test-witness-ai-zombies). No physics.h: witness_ai.c reaches the world
 * only through its map / wall-hit hooks, which this test supplies. */
#include "witness_ai.h"
#include "day_night_clock.h"
#include "../reflux/reflux_runtime.h"

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
/* like walk(), but a mover cannot enter a box (+ the 1.6 body radius witness_ai.c steers with) */
static int solid_at(float x, float z) {
    for (int b = 1; b < nboxes; b++)
        if (x > boxes[b].x - boxes[b].w * 0.5f - 1.0f && x < boxes[b].x + boxes[b].w * 0.5f + 1.0f &&
            z > boxes[b].z - boxes[b].d * 0.5f - 1.0f && z < boxes[b].z + boxes[b].d * 0.5f + 1.0f) return 1;
    return 0;
}
static void walk_solid(float dt) {
    for (int i = 1; i < MAX_CLIENTS; i++) {
        PlayerState *p = &S.players[i];
        if (!p->active || p->state == STATE_DEAD) continue;
        float r = p->yaw * 0.0174533f;
        float nx = p->x + sinf(r) * p->in_fwd * 20.0f * dt, nz = p->z + cosf(r) * p->in_fwd * 20.0f * dt;
        if (!solid_at(nx, nz)) { p->x = nx; p->z = nz; }
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
        /* it may route AROUND the building now (card #522) -- what must never happen is entering it */
        if (fabsf(S.players[cid].z) < 40.0f && S.players[cid].x < 32.0f && S.players[cid].x > maxx) maxx = S.players[cid].x;
    }
    /* body radius is 1.6 around a face at x=28: the citizen must stay out of the box + margin */
    assert(maxx < 28.0f + 0.5f);
    printf("PASS: fleeing citizen never entered the building (max x beside it=%.2f, face at 28)\n", maxx);

    /* ---- card #522: a hunter in a dead-end pocket learns the walls and backs out around them ---- */
    witness_ai_set_wall_hit_hook(NULL); /* no clawing through: it has to find the way out */
    memset(&S, 0, sizeof S);
    S.game_mode = MODE_ZOMBIES;
    S.players[0].active = 1; S.players[0].state = STATE_ALIVE;
    S.players[0].x = 0.0f; S.players[0].y = 0.0f; S.players[0].z = 120.0f; /* hero straight "north", behind the pocket's back wall */
    witness_ai_reset(3u, 0);
    nboxes = 4;
    boxes[1] = (TBox){ -14.0f, 20.0f, 10.0f, 4.0f, 40.0f, 44.0f }; /* west wall  x=-16..-12, z -12..32 */
    boxes[2] = (TBox){  14.0f, 20.0f, 10.0f, 4.0f, 40.0f, 44.0f }; /* east wall  x= 12..16 */
    boxes[3] = (TBox){   0.0f, 20.0f, 30.0f, 32.0f, 40.0f, 4.0f }; /* back wall  z=28..32, open to the south */
    int pz = witness_ai_spawn_zombie(&S, 0.0f, 0.0f, 0.0f, 0);
    assert(pz > 0);
    S.players[pz].yaw = 0.0f;
    assert(witness_ai_learned_blocked_count() == 0);
    float maxz = -1e9f;
    t = 10000;
    for (int i = 0; i < 2400; i++) {            /* 120 s of game time at 50 ms */
        t += 50;
        witness_ai_force_zombie_mood(pz, 2 /* HUNTING */);
        witness_ai_tick(&S, t);
        walk_solid(0.05f);
        if (S.players[pz].z > maxz) maxz = S.players[pz].z;
    }
    assert(witness_ai_learned_blocked_count() > 0);          /* it remembered walls */
    assert(witness_ai_learned_cell(0.0f, 0.0f) == 1);        /* and the floor it stood on */
    assert(maxz > 45.0f || S.players[pz].z > 45.0f);         /* it got past the pocket instead of grinding in it */
    printf("PASS: pocketed zombie learned %d wall cells and escaped (max z=%.1f, back wall at 28)\n", witness_ai_learned_blocked_count(), maxz);
    witness_ai_set_wall_hit_hook(hit_cb);
    nboxes = 2;
    boxes[1] = (TBox){ 30.0f, 20.0f, 0.0f, 4.0f, 40.0f, 80.0f }; /* the building face again, for the tests below */

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

    /* ---- founder 2026-10-04: sandbox zombies wander and get hungry instead of always knowing
       where the hero is; a close hero is sensed and chased, a far one is found by hunger scent ---- */
    memset(&S, 0, sizeof S);
    S.game_mode = MODE_ZOMBIES;
    S.players[0].active = 1; S.players[0].state = STATE_ALIVE;
    S.players[0].x = 900.0f; S.players[0].z = 0.0f;
    nboxes = 1;
    witness_ai_reset(4u, 0);
    zid = witness_ai_spawn_zombie(&S, 0.0f, 0.0f, 0.0f, 0);
    witness_ai_force_zombie_mood(zid, 2);
    float yaw0 = 0.0f; int yaw_changes = 0, stood = 0, moved = 0;
    float fx0 = 0, fz0 = 0;
    for (int i = 0; i < 400; i++) {   /* 20 s */
        t += 50; witness_ai_tick(&S, t); walk(0.05f);
        assert(S.players[zid].in_fwd <= 0.45f);        /* far zombie shambles, never sprints at a hero it can't sense */
        if (i == 0 || fabsf(S.players[zid].yaw - yaw0) > 1.0f) { if (i) yaw_changes++; yaw0 = S.players[zid].yaw; }
        if (S.players[zid].in_fwd == 0.0f) stood++; else moved++;
        (void)fx0; (void)fz0;
    }
    assert(moved > 0);                                 /* it wanders... */
    assert(yaw_changes >= 2);                          /* ...changing heading */
    float h_before = witness_ai_zombie_hunger(zid);
    for (int i = 0; i < 1600; i++) { t += 50; witness_ai_tick(&S, t); walk(0.05f); }   /* +80 s */
    assert(witness_ai_zombie_hunger(zid) >= 0.4f || S.players[zid].x > 20.0f);  /* hunger climbs while it has no target */
    float far0 = S.players[0].x - S.players[zid].x;
    for (int i = 0; i < 24000; i++) { t += 50; witness_ai_tick(&S, t); walk(0.05f); }  /* +20 min */
    assert(S.players[0].x - S.players[zid].x < far0 - 100.0f || S.players[0].x - S.players[zid].x < 60.0f);
    printf("PASS: far zombie wanders (%d heading changes, hunger %.2f -> %.2f), scent closed 900 -> %.0f\n",
           yaw_changes, h_before, witness_ai_zombie_hunger(zid), S.players[0].x - S.players[zid].x);
    (void)stood;

    /* a hero inside sense range is locked and chased */
    memset(&S, 0, sizeof S);
    S.game_mode = MODE_ZOMBIES;
    S.players[0].active = 1; S.players[0].state = STATE_ALIVE;
    S.players[0].x = 18.0f; S.players[0].z = 0.0f;
    nboxes = 1;
    witness_ai_reset(4u, 0);
    zid = witness_ai_spawn_zombie(&S, 0.0f, 0.0f, 0.0f, 0);
    witness_ai_force_zombie_mood(zid, 2);
    for (int i = 0; i < 60; i++) { t += 50; witness_ai_tick(&S, t); walk(0.05f); }
    assert(S.players[zid].in_fwd > 0.0f && S.players[zid].x > 0.5f);
    printf("PASS: zombie 18 from the hero senses it and chases (x=%.1f)\n", S.players[zid].x);

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

    /* ---- cards #482/#488: MODE_SURVIVAL waves ---- */
    memset(&S, 0, sizeof S);
    S.game_mode = MODE_SURVIVAL;
    S.players[0].active = 1; S.players[0].state = STATE_ALIVE;
    nboxes = 1;
    witness_ai_reset(6u, 0);
    witness_ai_survival_reset();
    assert(witness_ai_survival_wave() == 0);
    assert(witness_ai_survival_quota(1) == 7 && witness_ai_survival_quota(3) == 13);
    assert(witness_ai_survival_maxalive(1) == 6);
    int max_alive_seen = 0, wave_seen_max = 0;
    for (int i = 0; i < 2000 && witness_ai_survival_wave() < 3; i++) {
        t += 5000; /* spawn gap is 15 s: three steps per zombie */
        witness_ai_survival_tick(&S, t); witness_ai_tick(&S, t);
        int alive = 0;
        for (int k = 1; k < MAX_CLIENTS; k++)
            if (S.players[k].active && S.players[k].state != STATE_DEAD && witness_ai_role_for_player(k) == WITNESS_AI_ROLE_ZOMBIE) alive++;
        if (alive > max_alive_seen) max_alive_seen = alive;
        if (witness_ai_survival_wave() > wave_seen_max) wave_seen_max = witness_ai_survival_wave();
        assert(alive <= witness_ai_survival_maxalive(witness_ai_survival_wave()));
        /* the player kills whatever has reached the field: a wave only ends when every zombie is dead */
        if (i % 4 == 3) for (int k = 1; k < MAX_CLIENTS; k++)
            if (S.players[k].active && S.players[k].state != STATE_DEAD && witness_ai_role_for_player(k) == WITNESS_AI_ROLE_ZOMBIE) { S.players[k].state = STATE_DEAD; break; }
    }
    assert(wave_seen_max >= 3);                     /* waves advance once the field is cleared */
    assert(max_alive_seen >= 2 && max_alive_seen <= witness_ai_survival_maxalive(3));
    printf("PASS: survival reached wave %d, never more than %d zombies at once\n", wave_seen_max, max_alive_seen);
    S.game_mode = MODE_ZOMBIES; /* survival tick is a no-op outside its own mode */
    int w = witness_ai_survival_wave();
    witness_ai_survival_tick(&S, t + 100000u);
    assert(witness_ai_survival_wave() == w);

    /* ---- founder real-time 2026-10-08: "bring in all the BIG_O affordances... we need it all
       evented with reflux" -- ZOMBIE_SPAWNED fires exactly once at the real spawn call site ---- */
    reflux_host_reset();
    memset(&S, 0, sizeof S);
    S.game_mode = MODE_ZOMBIES;
    S.players[0].active = 1; S.players[0].state = STATE_ALIVE;
    S.players[0].x = 18.0f; S.players[0].z = 0.0f; /* same "senses and chases" distance as above */
    nboxes = 1;
    witness_ai_reset(7u, 0);

    int before = reflux_host_log_size();
    zid = witness_ai_spawn_zombie(&S, 0.0f, 0.0f, 0.0f, 0);
    assert(zid > 0);
    assert(reflux_host_log_size() == before + 1);
    assert(reflux_host_action_type_at(before) == REFLUX_ACTION_ZOMBIE_SPAWNED);
    assert(reflux_host_action_a_at(before) == zid);
    printf("PASS: REFLUX_ACTION_ZOMBIE_SPAWNED dispatched once at spawn (player_id=%d)\n", zid);

    /* naturally reaches HUNTING via real has_target/mood_change_at_ms logic (zombie_values.c),
       not a forced mood -- same real "hero at 18 senses and chases" mechanic the test above
       already proves lands within 60 ticks. Must produce exactly one MOOD_ESCALATED edge. */
    int esc_count = 0;
    for (int i = 0; i < 60; i++) {
        t += 50;
        int n0 = reflux_host_log_size();
        witness_ai_tick(&S, t);
        walk(0.05f);
        for (int k = n0; k < reflux_host_log_size(); k++)
            if (reflux_host_action_type_at(k) == REFLUX_ACTION_ZOMBIE_MOOD_ESCALATED) esc_count++;
    }
    assert(esc_count == 1);
    assert(S.players[zid].in_fwd > 0.0f); /* confirms it is really HUNTING, same assert as the test above */
    printf("PASS: REFLUX_ACTION_ZOMBIE_MOOD_ESCALATED fired exactly once on the natural DORMANT->HUNTING edge\n");

    /* killing it must harvest exactly once, even checked over several more ticks */
    S.players[zid].state = STATE_DEAD;
    int harvest_count = 0;
    for (int i = 0; i < 5; i++) {
        t += 50;
        int n0 = reflux_host_log_size();
        witness_ai_tick(&S, t);
        for (int k = n0; k < reflux_host_log_size(); k++)
            if (reflux_host_action_type_at(k) == REFLUX_ACTION_ZOMBIE_HARVESTED) {
                harvest_count++;
                assert(reflux_host_action_a_at(k) == zid);
            }
    }
    assert(harvest_count == 1);
    printf("PASS: REFLUX_ACTION_ZOMBIE_HARVESTED fired exactly once per corpse\n");

    /* ---- WITNESS_ESCALATED: 5 citizens (>= silence_threshold) around one HUNTING zombie cross
       into SILENCING together -- the same real count/arrogance math the dispatch loop (The Men)
       already relies on, not a forced state ---- */
    reflux_host_reset();
    memset(&S, 0, sizeof S);
    S.game_mode = MODE_ZOMBIES;
    S.players[0].active = 1; S.players[0].state = STATE_ALIVE;
    S.players[0].x = 900.0f; S.players[0].z = 900.0f; /* far away: hero plays no part in this one */
    nboxes = 1;
    witness_ai_reset(8u, 0);
    for (int i = 0; i < 5; i++) {
        int cid2 = witness_ai_spawn_citizen(&S, ZONE_PUBLIC, 40, 50, 5.0f, 0.0f, 0.0f, 0); /* arrogance 50: lands on SILENCING, not PANIC/ENGAGE */
        assert(cid2 > 0);
    }
    zid = witness_ai_spawn_zombie(&S, 0.0f, 0.0f, 0.0f, 0);
    assert(zid > 0);
    witness_ai_force_zombie_mood(zid, 2 /* HUNTING */);
    before = reflux_host_log_size();
    witness_ai_tick(&S, 100); /* inside zombie_state_init's own real 1000ms mood_change_at_ms
                                  window, so zombie_tick_dt's own real re-evaluation does not
                                  overwrite the forced mood before the witness loop reads it */
    int witness_esc = 0;
    for (int k = before; k < reflux_host_log_size(); k++)
        if (reflux_host_action_type_at(k) == REFLUX_ACTION_WITNESS_ESCALATED) {
            witness_esc++;
            assert(reflux_host_action_b_at(k) == WS_SILENCING);
        }
    assert(witness_esc == 5); /* all 5 citizens cross the edge on the same tick */
    printf("PASS: REFLUX_ACTION_WITNESS_ESCALATED fired for all %d citizens crossing into SILENCING\n", witness_esc);

    printf("ALL PASS\n");
    return 0;
}
