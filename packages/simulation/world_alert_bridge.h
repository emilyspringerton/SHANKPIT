#ifndef WORLD_ALERT_BRIDGE_H
#define WORLD_ALERT_BRIDGE_H

/* world_alert_bridge.h -- BIG_O engine merge phase 6 (EMILY/BACKLOG.md SECTION 536, founder
 * real-time: "the phone in story mode everything", "all the events and messages on the phone").
 * The real, live wiring that makes that true: composes four already-landed, previously-standalone
 * pieces into one working pipeline, without editing any of their already-shipped files --
 *
 *   day_night_clock (phase 1) --ticks--> [this bridge detects the transition]
 *     --dispatches--> REFLUX (phase 1c, PARENA-powered)
 *       --polled by--> world_alerts_mod.prn (this phase, PARENA -- decides which phone message,
 *         if any, a given world event deserves)
 *         --feeds--> phone.h's phone_notify (this phase -- the real message/notification system)
 *
 * Real, honest scope: only phase/weather transitions are wired (day_night_clock has no zombie
 * population to source ZOMBIE_SPAWNED/ZOMBIE_HARVESTED events from -- a phase 1 scope cut, see
 * that module's own header comment). Call world_alert_bridge_tick once per real game tick, after
 * day_night_clock_tick and before reading phone state for rendering. */

#include "day_night_clock.h"
#include "../common/phone.h"

typedef struct {
    int have_prev;
    DncPhase prev_phase;
    DncWeather prev_weather;
    int last_index; /* REFLUX polling cursor -- see world_alert_bridge.c's own top doc comment for
                        the real, named limitation of this scheme (relative indexing, no
                        total-dispatched counter exposed by REFLUX's own PARENA ABI). */
} WorldAlertBridge;

void world_alert_bridge_init(WorldAlertBridge *b);

/* Detects a phase or weather change since the last call (comparing against clock's CURRENT state,
 * so it's safe to call every tick even when nothing changed), dispatches the matching REFLUX
 * event, and -- if world_alerts_mod.prn decides that event deserves a phone message -- calls
 * phone_notify on it. now_ms feeds both REFLUX's own log (if it ever needs real time; it doesn't
 * today) and phone_notify's own anti-spam window. */
void world_alert_bridge_tick(WorldAlertBridge *b, const DayNightClock *clock, Phone *phone, unsigned int now_ms);

#endif
