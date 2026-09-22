/* world_alert_bridge_test.c -- real, live integration test for world_alert_bridge.c (BIG_O engine
 * merge phase 6): day_night_clock (phase 1) -> REFLUX (phase 1c) -> world_alerts_mod.prn (this
 * phase) -> phone.h (this phase), proving the whole pipeline actually works end to end, not just
 * that each piece compiles standalone. Plain assert() harness, same convention as every other
 * test in this merge.
 *
 * Build and run:
 *   gcc -Wall -Wextra -O2 -o /tmp/wab_test packages/simulation/world_alert_bridge_test.c \
 *       packages/simulation/world_alert_bridge.c packages/simulation/day_night_clock.c \
 *       packages/simulation/world_rules.c packages/simulation/world_alerts_mod.c \
 *       packages/reflux/reflux_mod.c packages/reflux/reflux_runtime.c && /tmp/wab_test
 */
#include "day_night_clock.h"
#include "world_alert_bridge.h"

#include <assert.h>
#include <stdio.h>

/* Declared directly (no shared .h for a host function, matching this codebase's own convention)
 * so the test can reset REFLUX's global log between cases, same as reflux_mod_test.c does. */
void reflux_host_reset(void);

int main(void) {
    reflux_host_reset();

    /* DAWN -> DAY transition raises phone message 3 ("daybreak", world_alerts_mod's own real
       mapping: PHASE_CHANGED with new phase == DAY). */
    {
        DayNightClock clock;
        Phone phone;
        WorldAlertBridge bridge;
        day_night_clock_init(&clock, 1, 6); /* starts at 06:00, minute 360, phase DAWN */
        phone_init(&phone);
        world_alert_bridge_init(&bridge);

        world_alert_bridge_tick(&bridge, &clock, &phone, 0); /* baseline observation, no event yet */
        assert(phone.message_count == 0);

        day_night_clock_tick(&clock, 61); /* minute 421 -- past the real DAY threshold (420) */
        assert(day_night_clock_phase(&clock) == DNC_DAY);
        world_alert_bridge_tick(&bridge, &clock, &phone, 1000);

        assert(phone.message_count == 1);
        assert(phone.messages[0] == 3);
        assert(phone.unread == 1);
        printf("PASS: a real DAWN->DAY transition dispatches through REFLUX and lands message 3 on the phone\n");
    }

    /* Weather forced to STORM raises phone message 4 -- proves the bridge also reacts to weather,
       not just phase. */
    {
        DayNightClock clock;
        Phone phone;
        WorldAlertBridge bridge;
        day_night_clock_init(&clock, 2, 12);
        phone_init(&phone);
        world_alert_bridge_init(&bridge);
        world_alert_bridge_tick(&bridge, &clock, &phone, 0);

        day_night_clock_force_weather(&clock, DNC_STORM);
        world_alert_bridge_tick(&bridge, &clock, &phone, 2000);

        assert(phone.message_count == 1);
        assert(phone.messages[0] == 4);
        printf("PASS: weather forced to STORM dispatches through REFLUX and lands message 4 on the phone\n");
    }

    /* A tick with no real transition raises nothing -- the bridge doesn't spam the phone every tick. */
    {
        DayNightClock clock;
        Phone phone;
        WorldAlertBridge bridge;
        day_night_clock_init(&clock, 3, 12);
        phone_init(&phone);
        world_alert_bridge_init(&bridge);
        world_alert_bridge_tick(&bridge, &clock, &phone, 0);

        day_night_clock_tick(&clock, 5); /* well within DAY, no phase/weather change */
        world_alert_bridge_tick(&bridge, &clock, &phone, 500);
        assert(phone.message_count == 0);
        printf("PASS: a tick with no real transition raises no phone message\n");
    }

    printf("ALL PASS\n");
    return 0;
}
