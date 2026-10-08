#ifndef ZOMBIES_HUD_BRIDGE_H
#define ZOMBIES_HUD_BRIDGE_H

/* zombies_hud_bridge.h -- BIG_O engine merge phase 8 (EMILY/BACKLOG.md SECTION 536 follow-up,
 * founder real-time, 2026-10-08: "continue to bring in the BIG_O affordances into shankpit
 * zombies... we need it all evented with reflux"). The real, live wiring that gives
 * MODE_ZOMBIES/MODE_SURVIVAL's own REFLUX_ACTION_ZOMBIE_MOOD_ESCALATED/WITNESS_ESCALATED/
 * GIANT_BUG_ATE_ZOMBIE/MEN_DISPATCHED/MEN_RESOLVED dispatches (packages/reflux/reflux_runtime.h)
 * a real subscriber for the first time -- until this file, every one of those was real,
 * dispatched, unread telemetry (witness_ai.h's own top doc comment: "no *_bridge.c consumer
 * exists yet ... no smartphone to bridge to"). Composes three already-landed, previously-
 * standalone pieces into one working pipeline, same shape world_alert_bridge.c's own
 * day_night_clock -> REFLUX -> world_alerts_mod.prn -> phone_notify pipeline already proves out --
 * just with a HUD readout in place of a phone, since MODE_ZOMBIES/MODE_SURVIVAL have no phone:
 *
 *   witness_ai.c (already shipped) --dispatches--> REFLUX (already shipped)
 *     --polled by--> zombies_awareness_rules.prn (this phase, PARENA -- decides which events
 *       deserve a HUD ping, and how severe)
 *       --feeds--> awareness_compass.h's pure direction/compass math (this phase, ported from
 *         BIG_O/NORTHSTAR.md §35's own bigo_awareness.h) --> one real on-screen alert
 *
 * Call zombies_hud_bridge_reset once per match init (packages/simulation/local_game.h's
 * local_init_match, alongside witness_ai_reset) and zombies_hud_bridge_tick every real tick
 * right after witness_ai_tick (same local_update call sites witness_ai_zombies_tick/
 * witness_ai_survival_tick already use) -- safe to call every tick even when nothing happened.
 * apps/lobby/src/main.c (the only binary with a renderer) queries zombies_hud_bridge_current
 * each frame to draw the HUD; apps/server links and ticks this too (same TU, same "the button
 * doesn't know about the bridge" shape) but never queries it -- a real, harmless no-op there.
 *
 * Real, honest scope: this is NOT BIG_O's own §35 feature re-created verbatim -- that one keys off
 * the QUIET costume/Decorum observation path, which doesn't exist in these modes. This reuses the
 * same compass+intensity *mechanism* on the LOUD zombie-event channel these modes actually have:
 * "something dangerous just changed near you," not "you've been spotted." See awareness_compass.h
 * and zombies_awareness_rules.prn's own doc comments for the full account. */

#include "../common/protocol.h" /* ServerState, PlayerState, MAX_CLIENTS */

void zombies_hud_bridge_reset(void);

void zombies_hud_bridge_tick(const ServerState *s, unsigned int now_ms);

#define ZOMBIES_HUD_BRIDGE_DISPLAY_MS 3000u

/* Real, live query for the renderer. Returns 1 and fills every out-param if an alert dispatched
 * within the last ZOMBIES_HUD_BRIDGE_DISPLAY_MS is still showing, 0 otherwise (out-params
 * untouched on a 0 return -- same "caller-owned, only written on success" convention
 * pheromone_find_nearest already uses). compass_out is one of AWARENESS_COMPASS_NAMES's own 8
 * indices (awareness_compass.h); intensity_out is 0..100; action_type_out is the raw
 * REFLUX_ACTION_* constant that caused it, for a caller that wants a type-specific label. */
int zombies_hud_bridge_current(unsigned int now_ms, int *compass_out, int *intensity_out, int *action_type_out);

#endif
