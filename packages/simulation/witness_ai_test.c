/* witness_ai_test.c -- real, live integration test for witness_ai.c (BIG_O engine merge phase
 * 7b): the real population/tick loop composing witness_sim (phase 2), npc_archetype (phase 3),
 * zombie_values (phase 3), and witness_live (phase 7a) end to end against a real ServerState, not
 * just each piece standalone. Plain assert() harness, same convention as every other test in this
 * merge.
 *
 * Build and run:
 *   gcc -Wall -Wextra -O2 -Ipackages/simulation -Ipackages/common \
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

    printf("\nALL PASS\n");
    return 0;
}
