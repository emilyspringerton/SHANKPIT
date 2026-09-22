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
 *       packages/simulation/humanness.c -lm && /tmp/witness_ai_test
 */
#include "witness_ai.h"
#include "witness_sim.h" /* WS_*, ZONE_PUBLIC */
#include "zombie_values.h" /* ZOMBIE_MOOD_* */

#include <assert.h>
#include <stdio.h>
#include <string.h>

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

    printf("\nALL PASS\n");
    return 0;
}
