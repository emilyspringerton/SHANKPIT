/* zombies_hud_bridge_test.c -- real, live integration test for zombies_hud_bridge.c (BIG_O engine
 * merge phase 8): a raw REFLUX dispatch (standing in for witness_ai.c's own real call sites) ->
 * REFLUX -> zombies_awareness_rules.prn -> awareness_compass.h, proving the whole pipeline
 * actually produces a real, correctly-directed, correctly-timed HUD alert, not just that each
 * piece compiles standalone. Plain assert() harness, same convention as world_alert_bridge_test.c.
 *
 * Build and run:
 *   gcc -Wall -Wextra -O2 -o /tmp/zhb_test packages/simulation/zombies_hud_bridge_test.c \
 *       packages/simulation/zombies_hud_bridge.c packages/simulation/zombies_awareness_rules.c \
 *       packages/reflux/reflux_mod.c packages/reflux/reflux_runtime.c -lm && /tmp/zhb_test
 */
#include "zombies_hud_bridge.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Declared directly (no shared .h for a host function, matching world_alert_bridge_test.c's own
 * identical convention) so the test can both reset and directly dispatch into REFLUX's global
 * log, standing in for witness_ai.c's own real dispatch call sites without needing to link the
 * entire witness_ai.c population/perception machinery just to raise one event. */
void reflux_host_reset(void);
void reflux_host_dispatch(int action_type, int a, int b, int c);

#define REFLUX_ACTION_ZOMBIE_MOOD_ESCALATED 105
#define REFLUX_ACTION_WITNESS_ESCALATED 106
#define REFLUX_ACTION_GIANT_BUG_ATE_ZOMBIE 107
#define REFLUX_ACTION_MEN_DISPATCHED 108
#define REFLUX_ACTION_MEN_RESOLVED 109
#define ZOMBIE_MOOD_FRENZIED 3
#define WS_SILENCING 3

static void make_hero_and_subject(ServerState *s, float subj_x, float subj_z) {
    memset(s, 0, sizeof(*s));
    s->players[0].active = 1;
    s->players[0].state = STATE_ALIVE;
    s->players[0].x = 0.0f; s->players[0].y = 0.0f; s->players[0].z = 0.0f;
    s->players[0].scene_id = 1;

    s->players[5].active = 1;
    s->players[5].state = STATE_ALIVE;
    s->players[5].x = subj_x; s->players[5].y = 0.0f; s->players[5].z = subj_z;
    s->players[5].scene_id = 1;
}

int main(void) {
    reflux_host_reset();

    /* A real FRENZIED mood escalation directly east of the hero produces a real, live, correctly
       east-pointing, high-intensity alert. */
    {
        ServerState s;
        make_hero_and_subject(&s, 10.0f, 0.0f); /* subject due east (+X, same Z) */
        zombies_hud_bridge_reset();
        zombies_hud_bridge_tick(&s, 0); /* baseline -- establishes the polling cursor */

        reflux_host_dispatch(REFLUX_ACTION_ZOMBIE_MOOD_ESCALATED, 5, ZOMBIE_MOOD_FRENZIED, 0);
        zombies_hud_bridge_tick(&s, 1000);

        int compass, intensity, action_type;
        int have = zombies_hud_bridge_current(1500, &compass, &intensity, &action_type);
        assert(have == 1);
        assert(compass == 2); /* E, awareness_compass.h's own index order */
        assert(intensity == 90); /* FRENZIED, zombies_awareness_rules.prn's own severity */
        assert(action_type == REFLUX_ACTION_ZOMBIE_MOOD_ESCALATED);
        printf("PASS: a real FRENZIED mood escalation east of the hero produces a real E/90 alert\n");
    }

    /* The alert expires after its real display window -- this isn't a permanent sticky HUD line. */
    {
        ServerState s;
        make_hero_and_subject(&s, 0.0f, 10.0f); /* subject due north */
        zombies_hud_bridge_reset();
        zombies_hud_bridge_tick(&s, 0);

        reflux_host_dispatch(REFLUX_ACTION_WITNESS_ESCALATED, 5, WS_SILENCING, 0);
        zombies_hud_bridge_tick(&s, 5000);

        int compass, intensity, action_type;
        assert(zombies_hud_bridge_current(5500, &compass, &intensity, &action_type) == 1);
        assert(compass == 0); /* N */
        assert(intensity == 60); /* SILENCING, not ENGAGE */

        unsigned int past_window = 5000 + ZOMBIES_HUD_BRIDGE_DISPLAY_MS + 1;
        assert(zombies_hud_bridge_current(past_window, &compass, &intensity, &action_type) == 0);
        printf("PASS: a real alert expires after its real display window instead of sticking forever\n");
    }

    /* ZOMBIE_SPAWNED (housekeeping, not an escalation) never produces an alert -- the bridge is
       selective, same "don't spam the HUD with every event" discipline alert_should_react uses. */
    {
        ServerState s;
        make_hero_and_subject(&s, 5.0f, 0.0f);
        zombies_hud_bridge_reset();
        zombies_hud_bridge_tick(&s, 0);

        reflux_host_dispatch(103 /* REFLUX_ACTION_ZOMBIE_SPAWNED */, 5, 0, 0);
        zombies_hud_bridge_tick(&s, 1000);

        int compass, intensity, action_type;
        assert(zombies_hud_bridge_current(1200, &compass, &intensity, &action_type) == 0);
        printf("PASS: a real ZOMBIE_SPAWNED event is real housekeeping, not an awareness alert\n");
    }

    /* Giant Zombie Bug feeding and The Men's own dispatch/resolve lifecycle each produce a real,
       distinctly-severed alert too, not just the zombie/witness families. */
    {
        ServerState s;
        make_hero_and_subject(&s, -10.0f, 0.0f); /* subject due west */
        zombies_hud_bridge_reset();
        zombies_hud_bridge_tick(&s, 0);

        reflux_host_dispatch(REFLUX_ACTION_GIANT_BUG_ATE_ZOMBIE, 5, 9, 0);
        zombies_hud_bridge_tick(&s, 1000);
        int compass, intensity, action_type;
        assert(zombies_hud_bridge_current(1200, &compass, &intensity, &action_type) == 1);
        assert(compass == 6 /* W */ && intensity == 70 && action_type == REFLUX_ACTION_GIANT_BUG_ATE_ZOMBIE);

        reflux_host_dispatch(REFLUX_ACTION_MEN_DISPATCHED, 5, 2, 0);
        zombies_hud_bridge_tick(&s, 2000);
        assert(zombies_hud_bridge_current(2200, &compass, &intensity, &action_type) == 1);
        assert(intensity == 20 && action_type == REFLUX_ACTION_MEN_DISPATCHED);

        reflux_host_dispatch(REFLUX_ACTION_MEN_RESOLVED, 5, 1, 0);
        zombies_hud_bridge_tick(&s, 3000);
        assert(zombies_hud_bridge_current(3200, &compass, &intensity, &action_type) == 1);
        assert(intensity == 15 && action_type == REFLUX_ACTION_MEN_RESOLVED);
        printf("PASS: Giant Zombie Bug feeding and The Men's dispatch/resolve each raise their own real, distinct alert\n");
    }

    printf("ALL PASS\n");
    return 0;
}
