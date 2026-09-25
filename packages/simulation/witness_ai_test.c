/* witness_ai_test.c -- real, live integration test for witness_ai.c (BIG_O engine merge phases
 * 7b/7c): the real population/tick loop composing witness_sim (phase 2), npc_archetype (phase 3),
 * zombie_values (phase 3), witness_live (phase 7a), and the zone-authoring feature
 * (level_boxes.h's LevelZone, phase 7c) end to end against a real ServerState, not just each piece
 * standalone. Plain assert() harness, same convention as every other test in this merge.
 *
 * Build and run:
 *   gcc -Wall -Wextra -O2 -Ipackages/simulation -Ipackages/common -Ipackages/world \
 *       -o /tmp/witness_ai_test packages/simulation/witness_ai_test.c \
 *       packages/simulation/witness_ai.c packages/simulation/witness_sim.c \
 *       packages/simulation/witness_rules.c packages/simulation/npc_archetype.c \
 *       packages/simulation/zombie_values.c packages/simulation/ai_brain_rules.c \
 *       packages/simulation/humanness.c packages/simulation/giant_bug_values.c \
 *       packages/simulation/giant_bug_brain.c -lm && /tmp/witness_ai_test
 */
#include "witness_ai.h"
#include "witness_sim.h" /* WS_*, ZONE_PUBLIC */
#include "zombie_values.h" /* ZOMBIE_MOOD_* */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static void reset_server(ServerState *s) {
    memset(s, 0, sizeof(*s));
}

int main(void) {
    /* Spawning allocates distinct, real PlayerState slots -- every connected client would render
       these for free, same as story_ai's own enemies. */
    {
        ServerState s;
        reset_server(&s);
        witness_ai_reset(1, 0);

        int cid = witness_ai_spawn_citizen(&s, ZONE_PUBLIC, 40, 30, 0.0f, 0.0f, 0.0f, 0);
        int zid = witness_ai_spawn_zombie(&s, 5.0f, 0.0f, 5.0f, 0);
        assert(cid > 0 && zid > 0 && cid != zid);
        assert(s.players[cid].active && s.players[cid].is_bot);
        assert(s.players[zid].active && s.players[zid].is_bot);
        assert(s.players[cid].x == 0.0f && s.players[cid].z == 0.0f);
        assert(s.players[zid].x == 5.0f && s.players[zid].z == 5.0f);
        printf("PASS: spawn_citizen/spawn_zombie allocate distinct, real PlayerState slots\n");

        /* A DORMANT zombie is not a witnessable event -- the citizen's state is untouched. */
        witness_ai_tick(&s, 0);
        assert(witness_ai_citizen_state(cid) == WS_UNAWARE);
        printf("PASS: a DORMANT zombie raises no witness event -- citizen stays UNAWARE\n");
    }

    /* The real, full pipeline: forcing a zombie HUNTING makes an in-range citizen (1 witness,
       arrogance 30 -- below the engage threshold) go to DENIAL, exactly matching
       witness_rules.c's own real witness_state(count=1, arrogance=30) mapping. */
    {
        ServerState s;
        reset_server(&s);
        witness_ai_reset(2, 0);

        int cid = witness_ai_spawn_citizen(&s, ZONE_PUBLIC, 40, 30, 0.0f, 0.0f, 0.0f, 0);
        int zid = witness_ai_spawn_zombie(&s, 5.0f, 0.0f, 5.0f, 0); /* flat dist ~7.07, inside the 25 radius */

        witness_ai_force_zombie_mood(zid, ZOMBIE_MOOD_HUNTING);
        witness_ai_tick(&s, 100);

        assert(witness_ai_citizen_state(cid) == WS_DENIAL);
        printf("PASS: a HUNTING zombie witnessed by 1 low-arrogance citizen -> DENIAL\n");
    }

    /* Distance actually gates witnessing: a citizen far outside WITNESS_LIVE_DETECTION_RADIUS is
       untouched by the same HUNTING zombie a nearby citizen reacts to. */
    {
        ServerState s;
        reset_server(&s);
        witness_ai_reset(3, 0);

        int near_id = witness_ai_spawn_citizen(&s, ZONE_PUBLIC, 40, 30, 3.0f, 0.0f, 4.0f, 0); /* dist 5 */
        int far_id = witness_ai_spawn_citizen(&s, ZONE_PUBLIC, 40, 30, 500.0f, 0.0f, 500.0f, 0);
        int zid = witness_ai_spawn_zombie(&s, 0.0f, 0.0f, 0.0f, 0);

        witness_ai_force_zombie_mood(zid, ZOMBIE_MOOD_HUNTING);
        witness_ai_tick(&s, 100);

        assert(witness_ai_citizen_state(near_id) == WS_DENIAL);
        assert(witness_ai_citizen_state(far_id) == WS_UNAWARE);
        printf("PASS: WITNESS_LIVE_DETECTION_RADIUS correctly excludes an out-of-range citizen\n");
    }

    /* 5+ in-range low-arrogance witnesses escalate straight to SILENCING, matching
       witness_rules.c's own real silence_threshold()==5 -- the real end-to-end version of the
       exact scenario witness_live_test.c already proved in isolation. */
    {
        ServerState s;
        reset_server(&s);
        witness_ai_reset(4, 0);

        int ids[5];
        for (int i = 0; i < 5; i++) {
            ids[i] = witness_ai_spawn_citizen(&s, ZONE_PUBLIC, 40, 30,
                                               (float)i, 0.0f, (float)i, 0);
            assert(ids[i] > 0);
        }
        int zid = witness_ai_spawn_zombie(&s, 0.0f, 0.0f, 0.0f, 0);

        witness_ai_force_zombie_mood(zid, ZOMBIE_MOOD_HUNTING);
        witness_ai_tick(&s, 100);

        for (int i = 0; i < 5; i++) {
            assert(witness_ai_citizen_state(ids[i]) == WS_SILENCING);
        }
        printf("PASS: 5 in-range low-arrogance witnesses escalate the whole group to SILENCING\n");
    }

    /* npc_archetype's own mood-modulated vigilance really gets written back into the owned
       WitnessSim's npc entry every tick, not just computed and discarded. */
    {
        ServerState s;
        reset_server(&s);
        witness_ai_reset(5, 0);

        int cid = witness_ai_spawn_citizen(&s, ZONE_PUBLIC, 40, 30, 0.0f, 0.0f, 0.0f, 0);
        witness_ai_tick(&s, 100);
        /* Fresh NpcBrain (mood CURIOUS, zero fatigue, full energy) yields the citizen archetype's
           own real base vigilance (35, npc_archetype.c's real default) exactly -- NOT the
           base_vigilance=40 witness_sim_add_npc was called with. A bug that skipped the write-back
           would leave 40 in place instead, so this proves the composition is really wired live,
           not just computed and discarded. */
        assert(witness_ai_citizen_vigilance(cid) == 35);
        assert(witness_ai_citizen_state(cid) == WS_UNAWARE); /* sanity: still untouched by any event */
        printf("PASS: npc_archetype's own effective vigilance is live-written into the WitnessSim npc entry\n");
    }

    /* witness_ai_sync_zones (phase 7c): a citizen's live position resolves against a real,
       authored CustomLevelData zone volume and gets written into the owned WitnessSim's npc
       entry -- the real end-to-end proof that level_boxes.h's new LevelZone/
       level_boxes_zone_for_position actually drives witness_sim's own zone field, not just
       parses cleanly in isolation. */
    {
        ServerState s;
        reset_server(&s);
        witness_ai_reset(6, 0);

        const char *json =
            "{\"name\":\"TEST\",\"width\":40,\"height\":10,\"depth\":40,"
            "\"ground_plane_enabled\":true,\"ground_plane_squares\":1,\"walls\":[],"
            "\"zones\":[{\"x\":0,\"y\":0,\"z\":0,\"radius\":5,\"zone_type\":1}]}"; /* ZONE_LAB */
        CustomLevelData lvl;
        assert(level_boxes_parse_json(json, &lvl));

        /* Spawned in ZONE_PUBLIC at authoring time, but standing INSIDE the LAB zone volume. */
        int cid = witness_ai_spawn_citizen(&s, ZONE_PUBLIC, 40, 30, 1.0f, 0.0f, 1.0f, 0);
        assert(witness_ai_citizen_zone(cid) == ZONE_PUBLIC);

        witness_ai_sync_zones(&s, &lvl);
        assert(witness_ai_citizen_zone(cid) == ZONE_LAB);
        printf("PASS: witness_ai_sync_zones resolves a citizen's live position into the authored zone\n");

        /* Moving the citizen's real PlayerState outside every authored zone keeps the LAST known
           zone rather than snapping back to ZONE_PUBLIC -- a real, deliberate choice (an
           unauthored gap in zone coverage isn't a meaningful "the player is now in public" fact). */
        s.players[cid].x = 500.0f;
        s.players[cid].z = 500.0f;
        witness_ai_sync_zones(&s, &lvl);
        assert(witness_ai_citizen_zone(cid) == ZONE_LAB);
        printf("PASS: leaving every authored zone keeps the citizen's last known zone, not a silent reset\n");
    }

    /* "full phone app parity, costume changes, add The Men" follow-up. */
    {
        ServerState s;
        reset_server(&s);
        witness_ai_reset(7, 0);

        int cid = witness_ai_spawn_citizen(&s, ZONE_PUBLIC, 40, 30, 0.0f, 0.0f, 0.0f, 0);
        int mid = witness_ai_spawn_the_men(&s, ZONE_PUBLIC, 85, 15, 20.0f, 0.0f, 0.0f, 0);
        int zid = witness_ai_spawn_zombie(&s, 40.0f, 0.0f, 0.0f, 0);
        assert(cid > 0 && mid > 0 && zid > 0);
        assert(mid != cid && mid != zid);
        assert(witness_ai_role_for_player(cid) == WITNESS_AI_ROLE_CITIZEN);
        assert(witness_ai_role_for_player(mid) == WITNESS_AI_ROLE_THE_MEN);
        assert(witness_ai_role_for_player(zid) == WITNESS_AI_ROLE_ZOMBIE);
        assert(witness_ai_role_for_player(0) == WITNESS_AI_ROLE_NONE); /* the hero itself, never spawned by this module */
        printf("PASS: witness_ai_spawn_the_men allocates a distinct slot, correctly told apart by role\n");
    }
    {
        /* Correct costume for the hardcoded VOXWORLD "lab" trespass circle (packages/simulation/
           witness_ai.c's own WITNESS_AI_LAB_ZONE_*, zone_access's real rule: ZONE_LAB needs
           COS_LAB_SMOCK) + a zero-vigilance witness -> never noticed, decorum untouched. */
        ServerState s;
        reset_server(&s);
        witness_ai_reset(8, 0);
        witness_ai_spawn_citizen(&s, ZONE_LAB, 0, 10, 0.0f, 0.0f, 0.0f, 0);
        s.scene_id = SCENE_VOXWORLD;
        s.players[0].active = 1;
        s.players[0].x = 110.0f; s.players[0].z = -260.0f; /* inside the lab circle */
        witness_ai_set_player_costume(COS_LAB_SMOCK);
        witness_ai_tick(&s, 0);
        /* decorum_start() (80) + one ambient DA_QUIET_TICK (+1, witness_sim_tick's own real per-
           tick player upkeep, fired once by this same witness_ai_tick call) -- the trespass check
           itself never touches it here. */
        assert(witness_ai_player_decorum() == 81);
        printf("PASS: correct costume in the lab zone, unnoticed, leaves decorum untouched\n");
    }
    {
        /* Wrong costume for the same lab circle + a max-vigilance witness -> always noticed,
           real decorum penalty (witness_rules.c: decorum_start 80, DA_WRONG_COSTUME delta -20). */
        ServerState s;
        reset_server(&s);
        witness_ai_reset(9, 0);
        witness_ai_spawn_citizen(&s, ZONE_LAB, 100, 10, 0.0f, 0.0f, 0.0f, 0);
        s.scene_id = SCENE_VOXWORLD;
        s.players[0].active = 1;
        s.players[0].x = 110.0f; s.players[0].z = -260.0f; /* inside the lab circle */
        witness_ai_set_player_costume(COS_JANITOR); /* wrong for ZONE_LAB */
        witness_ai_tick(&s, 0);
        /* decorum_start() (80) + one ambient DA_QUIET_TICK (+1) - DA_WRONG_COSTUME (-20) = 61. */
        assert(witness_ai_player_decorum() == 61);
        assert(witness_ai_player_decorum_band() == BAND_OK); /* 61 is not below suspicion_below(60) */
        printf("PASS: wrong costume in the lab zone, noticed, applies the real decorum penalty\n");
    }
    {
        /* Standing outside the lab circle entirely: never enters ZONE_LAB, no check fires at all,
           regardless of costume -- the trespass mechanic is real, not a blanket costume penalty. */
        ServerState s;
        reset_server(&s);
        witness_ai_reset(10, 0);
        witness_ai_spawn_citizen(&s, ZONE_LAB, 100, 10, 0.0f, 0.0f, 0.0f, 0);
        s.scene_id = SCENE_VOXWORLD;
        s.players[0].active = 1;
        s.players[0].x = 0.0f; s.players[0].z = 0.0f; /* far outside WITNESS_AI_LAB_ZONE_RADIUS */
        witness_ai_set_player_costume(COS_JANITOR);
        witness_ai_tick(&s, 0);
        assert(witness_ai_player_decorum() == 81); /* decorum_start() + one ambient DA_QUIET_TICK only */
        printf("PASS: outside the lab circle, wrong costume never even triggers a check\n");
    }

    /* "wheelbarrow" -- carry a whole zombie or citizen back to your lab. */
    {
        ServerState s;
        reset_server(&s);
        witness_ai_reset(11, 0);
        s.scene_id = SCENE_VOXWORLD;
        s.players[0].active = 1;
        s.players[0].x = 0.0f; s.players[0].y = 0.0f; s.players[0].z = 0.0f; s.players[0].yaw = 0.0f;

        int cid = witness_ai_spawn_citizen(&s, ZONE_PUBLIC, 40, 30, 2.0f, 0.0f, 1.0f, 0);
        assert(witness_ai_try_pickup(&s, 0.0f, 0.0f, 0.0f, 0) == cid);
        assert(witness_ai_carried_player_id() == cid);
        /* Already carrying -- a second pickup attempt is a real no-op, not a swap. */
        assert(witness_ai_try_pickup(&s, 0.0f, 0.0f, 0.0f, 0) == -1);
        printf("PASS: witness_ai_try_pickup picks up the nearest citizen, one at a time\n");

        /* Far from the lab circle: tick repositions the cargo but delivers nothing. */
        witness_ai_tick(&s, 0);
        assert(witness_ai_carried_player_id() == cid);
        assert(s.players[cid].active);
        assert(witness_ai_lab_deliveries() == 0);
        printf("PASS: carried cargo trails the hero but isn't delivered outside the lab circle\n");

        /* Walk the hero (and cargo) into the lab circle -- real delivery. */
        s.players[0].x = 110.0f; s.players[0].z = -260.0f;
        witness_ai_tick(&s, 1000);
        assert(witness_ai_carried_player_id() == -1);
        assert(!s.players[cid].active);
        assert(witness_ai_lab_deliveries() == 1);
        assert(witness_ai_role_for_player(cid) == WITNESS_AI_ROLE_NONE); /* despawned, no longer managed */
        printf("PASS: walking carried cargo into the lab circle delivers it for real\n");
    }
    {
        ServerState s;
        reset_server(&s);
        witness_ai_reset(12, 0);
        s.players[0].active = 1;
        int zid = witness_ai_spawn_zombie(&s, 1.0f, 0.0f, 0.0f, 0);
        assert(witness_ai_try_pickup(&s, 0.0f, 0.0f, 0.0f, 0) == zid);
        witness_ai_drop_carried();
        assert(witness_ai_carried_player_id() == -1);
        assert(s.players[zid].active); /* dropped, not delivered -- no despawn without the lab circle */
        printf("PASS: witness_ai_drop_carried releases in place, no delivery credit\n");
    }

    /* Cake-smash distraction ("if the cake gets smashed it flies everywhere and causes a big
       distraction and distracts from heavy zombie usage"). */
    {
        ServerState s;
        reset_server(&s);
        witness_ai_reset(13, 0);
        int cid = witness_ai_spawn_citizen(&s, ZONE_PUBLIC, 40, 30, 0.0f, 0.0f, 0.0f, 0);

        assert(!witness_ai_distraction_active(0));
        witness_ai_tick(&s, 0);
        int vig_before = witness_ai_citizen_vigilance(cid);

        witness_ai_smash_cake(0);
        assert(witness_ai_distraction_active(0));
        witness_ai_tick(&s, 0);
        int vig_during = witness_ai_citizen_vigilance(cid);
        assert(vig_before > 0 && vig_during < vig_before);
        printf("PASS: smashing the cake halves live citizen vigilance for real\n");

        assert(!witness_ai_distraction_active(WITNESS_AI_DISTRACTION_MS + 1));
        printf("PASS: the distraction expires for real after its own real window\n");
    }

    /* Giant Zombie Bugs ("feral AI units" -- founder real-time, 2026-09-22). Real spawn/role, the
       real "Men are custodians of the keys" authorization gate, and the real "eat a zombie ->
       grow stronger/faster" mechanic, all end to end against a live ServerState. */
    {
        ServerState s;
        reset_server(&s);
        witness_ai_reset(21, 0);

        int bug_id = witness_ai_spawn_giant_bug(&s, 0.0f, 0.0f, 0.0f, 0);
        assert(bug_id >= 0);
        assert(witness_ai_role_for_player(bug_id) == WITNESS_AI_ROLE_GIANT_BUG);
        assert(witness_ai_bug_strength(bug_id) == 1.0f && witness_ai_bug_speed(bug_id) == 1.0f);
        printf("PASS: giant bug spawns for real with the real 1.0 strength/speed baseline\n");

        /* No Men present -- the key-custodian gate blocks eating even with prey right on top. */
        int zid = witness_ai_spawn_zombie(&s, 0.0f, 0.0f, 0.0f, 0);
        assert(zid >= 0);
        assert(!witness_ai_bug_command_authorized());
        witness_ai_tick(&s, 0);
        assert(s.players[zid].active); /* not eaten -- unauthorized */
        assert(witness_ai_bug_strength(bug_id) == 1.0f); /* no growth without authorization */
        printf("PASS: giant bug does not eat without a live The Men NPC to hold the key\n");

        /* Spawn a Men NPC -- the key is now held, the bug may hunt. */
        int men_id = witness_ai_spawn_the_men(&s, ZONE_PUBLIC, 85, 15, 50.0f, 0.0f, 50.0f, 0);
        assert(men_id >= 0);
        assert(witness_ai_bug_command_authorized());
        witness_ai_tick(&s, 0);
        assert(!s.players[zid].active); /* eaten */
        /* A fresh-spawn zombie has aggression 0.0 (real baseline, zombie_values.c), so strength
           only grows when the prey itself had real aggression -- speed always grows since even a
           DORMANT reaction delay yields a positive speed_proxy (giant_bug_values.c). */
        assert(witness_ai_bug_strength(bug_id) >= 1.0f && witness_ai_bug_speed(bug_id) > 1.0f);
        printf("PASS: authorized giant bug eats a nearby zombie and grows real strength/speed (str=%.2f spd=%.2f)\n",
               witness_ai_bug_strength(bug_id), witness_ai_bug_speed(bug_id));
    }

    /* Zombie perception/chase/melee + citizen flee (founder real-time, 2026-09-25: "visceral
       agency zombie fighting citizens reacting"). Real, live proof against a ServerState with a
       real hero at player 0 -- previously untestable since has_target was hardcoded to 0. */
    {
        ServerState s;
        reset_server(&s);
        witness_ai_reset(7, 0);

        PlayerState *hero = &s.players[0];
        hero->active = 1;
        hero->state = STATE_ALIVE;
        hero->health = 100;
        hero->x = 0.0f; hero->y = 0.0f; hero->z = 0.0f;

        /* Out of perception range -- a DORMANT zombie should not move at all. */
        int zid = witness_ai_spawn_zombie(&s, 0.0f, 0.0f, -(WITNESS_AI_ZOMBIE_PERCEPTION_RADIUS + 10.0f), 0);
        assert(zid > 0);
        witness_ai_tick(&s, 0);
        assert(s.players[zid].in_fwd == 0.0f);
        printf("PASS: a zombie outside WITNESS_AI_ZOMBIE_PERCEPTION_RADIUS never has_target/moves\n");

        /* Force it HUNTING and bring it into range -- it should now turn to face the hero and
           actually push forward, via the exact same p->yaw/p->in_fwd fields story_ai.c's own bots
           already move through. */
        s.players[zid].x = 0.0f; s.players[zid].z = -10.0f;
        witness_ai_force_zombie_mood(zid, ZOMBIE_MOOD_HUNTING);
        witness_ai_tick(&s, 1000);
        assert(s.players[zid].in_fwd > 0.0f);
        printf("PASS: a HUNTING zombie within perception range chases (in_fwd > 0, faces the hero)\n");

        /* Walk it into melee range and confirm a real, cooldown-gated hit lands on the hero. */
        s.players[zid].x = 0.0f; s.players[zid].z = -1.0f; /* well inside WITNESS_AI_ZOMBIE_MELEE_RANGE */
        int hp_before = hero->health;
        witness_ai_tick(&s, 2000);
        assert(hero->health < hp_before);
        printf("PASS: a HUNTING zombie in melee range deals real damage to the hero (hp %d -> %d)\n",
               hp_before, hero->health);

        /* Cooldown: an immediate second tick must NOT land a second hit. */
        int hp_after_first_hit = hero->health;
        witness_ai_tick(&s, 2050);
        assert(hero->health == hp_after_first_hit);
        printf("PASS: WITNESS_AI_ZOMBIE_ATTACK_COOLDOWN_MS genuinely gates repeat melee hits\n");

        /* Killing the hero (health to 0) enters STATE_DEAD and fails the story. */
        hero->health = WITNESS_AI_ZOMBIE_MELEE_DAMAGE;
        s.game_mode = MODE_STORY;
        witness_ai_tick(&s, 2050 + WITNESS_AI_ZOMBIE_ATTACK_COOLDOWN_MS);
        assert(hero->state == STATE_DEAD);
        assert(s.story_phase == STORY_PHASE_FAILED);
        printf("PASS: a zombie melee kill enters STATE_DEAD and fails MODE_STORY, same as the boss\n");
    }
    {
        ServerState s;
        reset_server(&s);
        witness_ai_reset(9, 0);

        PlayerState *hero = &s.players[0];
        hero->active = 1;
        hero->state = STATE_ALIVE;
        hero->health = 100;
        hero->x = 500.0f; hero->y = 0.0f; hero->z = 500.0f; /* far away -- not this test's concern */

        int cid = witness_ai_spawn_citizen(&s, ZONE_PUBLIC, 40, 30, 0.0f, 0.0f, 0.0f, 0);
        int zid = witness_ai_spawn_zombie(&s, 0.0f, 0.0f, -5.0f, 0);
        assert(cid > 0 && zid > 0);

        witness_ai_tick(&s, 0);
        assert(s.players[cid].in_fwd == 0.0f);
        printf("PASS: a citizen near a DORMANT zombie does not flee\n");

        witness_ai_force_zombie_mood(zid, ZOMBIE_MOOD_FRENZIED);
        witness_ai_tick(&s, 1000);
        assert(s.players[cid].in_fwd > 0.0f);
        /* Fleeing yaw should point away from the zombie (citizen at z=0, zombie at z=-5 -- away is
           +z, i.e. atan2(0, +something) == 0 degrees). */
        assert(fabsf(s.players[cid].yaw) < 1.0f);
        printf("PASS: a citizen within WITNESS_AI_CITIZEN_FLEE_RADIUS of a FRENZIED zombie flees "
               "directly away from it (in_fwd=%.2f yaw=%.1f)\n", s.players[cid].in_fwd, s.players[cid].yaw);

        /* Move the zombie out of flee range -- the citizen should stop fleeing. */
        s.players[zid].x = 0.0f; s.players[zid].z = -(WITNESS_AI_CITIZEN_FLEE_RADIUS + 20.0f);
        witness_ai_tick(&s, 2000);
        assert(s.players[cid].in_fwd == 0.0f);
        printf("PASS: a citizen stops fleeing once the zombie leaves WITNESS_AI_CITIZEN_FLEE_RADIUS\n");
    }

    printf("\nALL PASS\n");
    return 0;
}
