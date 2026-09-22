#include "world_alert_bridge.h"

#include <string.h>

#include "../reflux/reflux_runtime.h" /* REFLUX_ACTION_PHASE_CHANGED/WEATHER_CHANGED constants */

/* Prototypes for the PARENA-generated glue this bridge composes (reflux_mod.c,
 * world_alerts_mod.c, do-not-edit-by-hand). No shared .h between generated units in this codebase
 * (see day_night_clock.c/witness_sim.c's own identical comment). */
void reflux_dispatch(int action_type, int a, int b, int c);
int reflux_log_size(void);
int reflux_action_type_at(int index);
int reflux_action_a_at(int index);
int reflux_action_b_at(int index);
int alert_should_react(int action_type);
int alert_message_id(int action_type, int a, int b);

void world_alert_bridge_init(WorldAlertBridge *b) {
    memset(b, 0, sizeof(*b));
}

/* Real, known limitation, named rather than silently assumed away: REFLUX's own PARENA-exposed
 * ABI (reflux_log_size/reflux_action_type_at) gives no total-dispatched counter, only a count of
 * currently-retained entries (capped at REFLUX_LOG_CAPACITY=256, shared across every REFLUX
 * dispatcher in the whole game, not just this bridge) -- so b->last_index tracking here can drift
 * if 256+ OTHER events land between two of this bridge's own ticks. Calling this every real game
 * tick (as intended) makes that practically unreachable for the 1-2 events this bridge itself
 * ever dispatches per call, but it is a real, structural limitation of the current REFLUX polling
 * contract in general, not papered over here. */
static void poll_and_notify(WorldAlertBridge *b, Phone *phone, unsigned int now_ms) {
    int n = reflux_log_size();
    for (int i = b->last_index; i < n; i++) {
        int type = reflux_action_type_at(i);
        if (!alert_should_react(type)) continue;
        int a = reflux_action_a_at(i);
        int bb = reflux_action_b_at(i);
        int msg = alert_message_id(type, a, bb);
        if (msg > 0) phone_notify(phone, msg, now_ms);
    }
    b->last_index = n;
}

void world_alert_bridge_tick(WorldAlertBridge *b, const DayNightClock *clock, Phone *phone, unsigned int now_ms) {
    DncPhase phase = day_night_clock_phase(clock);
    DncWeather weather = clock->weather;

    if (!b->have_prev) {
        b->have_prev = 1;
        b->prev_phase = phase;
        b->prev_weather = weather;
        /* First observation establishes a baseline -- matching BIG_O's own world_init (no
         * PHASE_CHANGED/WEATHER_CHANGED fires just for starting the clock, only real transitions do). */
        b->last_index = reflux_log_size();
        return;
    }

    if (phase != b->prev_phase) {
        reflux_dispatch(REFLUX_ACTION_PHASE_CHANGED, (int)b->prev_phase, (int)phase, day_night_clock_day(clock));
        b->prev_phase = phase;
    }
    if (weather != b->prev_weather) {
        reflux_dispatch(REFLUX_ACTION_WEATHER_CHANGED, (int)b->prev_weather, (int)weather, 0);
        b->prev_weather = weather;
    }

    poll_and_notify(b, phone, now_ms);
}
