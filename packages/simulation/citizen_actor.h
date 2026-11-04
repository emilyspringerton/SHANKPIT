#ifndef CITIZEN_ACTOR_H
#define CITIZEN_ACTOR_H

/* citizen_actor.h -- the body a MODE_ZOMBIES citizen has, driven by PARENA.
 *
 * Founder real-time: "lets make the citizens in zombie mode more dynamic have them be able to jump
 * ... merge player physics with citizen physics ... allow them to jump crouch slide and slide jump
 * also wield weapons ... wire up heuristics mixed with a parena hand written llm".
 *
 * WHAT THIS IS NOT: a second movement system. A citizen is already a real PlayerState in the
 * shared players[] pool and already runs the player's own update_entity -> apply_friction /
 * accelerate / crouch-slide / gravity / collision path (local_game.h). What it never did was WRITE
 * the inputs that path consumes -- in_jump, crouching, in_shoot, current_weapon. That is the whole
 * bug this file fixes: the physics is merged because there is only one physics, and a citizen now
 * speaks to it in the same words a human does.
 *
 * WHAT IT ADDS: an action per citizen, chosen each tick by the PARENA mod
 * (citizen_actor_rules.prn: hand-written per-action heuristics blended with a hand-written
 * fixed-point policy -- see that file's own doc comment, which is explicit that this is not a
 * language model), applied here as inputs, and announced on REFLUX so a mod can watch:
 *
 *   REFLUX_ACTION_CITIZEN_JUMPED / _SLID / _SLIDE_JUMPED / _WIELDED / _FIRED
 *
 * RELAYED, not host-local: the dedicated server drains this family past an absolute REFLUX cursor
 * and ships the rows as PACKET_CITIZEN_EVENTS (packages/simulation/citizen_sync.h), and each client
 * re-dispatches them into its own log -- so a mod listening on a client sees a citizen slide that
 * happened on the server without knowing the server exists. There is deliberately no citizen STATE
 * packet: a citizen's body is a real PlayerState and already travels in the ordinary snapshot, so
 * only the FACT needs to cross the wire. Arriving rows are untrusted, and reflux_action_is_citizen is
 * what bounds what a peer can say.
 *
 * Cooldowns, timers and the current action are the only state (this struct); every number that
 * decides anything is the mod's. */

#include "../common/protocol.h"
#include "citizen_actor_rules.h"

/* The action vocabulary. Ids are the PARENA mod's own 0..8 contract -- do not renumber one side
 * without the other; citizen_actor_test.c asserts the mod's pick() only ever returns these. */
enum {
    CITACT_IDLE = 0,
    CITACT_WALK = 1,
    CITACT_RUN = 2,
    CITACT_JUMP = 3,
    CITACT_CROUCH = 4,
    CITACT_SLIDE = 5,
    CITACT_SLIDE_JUMP = 6,
    CITACT_SHOOT = 7,
    CITACT_RELOAD = 8,
    CITACT_COUNT = 9
};

typedef struct {
    int active;                       /* 1 while this slot has a live citizen body */
    int action;                        /* CITACT_* */
    int last_action;                   /* previous tick's choice, for edge-triggered events */
    int wielding;                      /* 1 while a weapon is drawn */
    int weapon;                        /* WPN_* when wielding */
    int boosted;                       /* the one-shot vertical impulse was applied for this action */
    unsigned int action_until_ms;      /* a slide holds this long even if the pick changes its mind */
    unsigned int jump_ready_ms;
    unsigned int slide_jump_until_ms;  /* the window in which leaving a slide counts as a slide jump */
    unsigned int next_fire_ms;
} CitizenActor;

/* Forgets every body. Called on match reset, so a new round never inherits a cooldown. */
void citizen_actor_reset(void);

/* One decision tick for one citizen body.
 *   `threat`      = 1 when a HUNTING/FRENZIED zombie shares its scene, else 0
 *   `dist_milli`  = distance to the nearest such zombie, milli (0 when threat is 0)
 * Called from witness_ai.c's citizen loop, which is where that fact is known. Does not move
 * anything: it sets the action and dispatches events. */
void citizen_actor_tick(int player_id, PlayerState *p, int threat, int dist_milli, unsigned int now_ms);

/* Turns the stored action into this tick's inputs (a jump impulse, crouch, trigger, covered by the
 * shared physics). Called from local_game.h's per-player loop immediately before update_entity, so
 * it wins over any generic bot input written earlier in the same tick. Keyed by the player slot
 * rather than PlayerState.id on purpose: an NPC body's own id field is not the authority here, the
 * slot it occupies is. A no-op for a body with no active actor (a human, a zombie, a spectator). */
void citizen_actor_apply(int player_id, PlayerState *p, unsigned int now_ms);

int citizen_actor_action(int player_id);
int citizen_actor_is_wielding(int player_id);
const char *citizen_actor_action_name(int action);

/* Test/inspection: how many citizen actions have ever been dispatched. Monotonic, resets with
 * citizen_actor_reset. Exists so a test can prove an event actually fired without reaching into
 * REFLUX's own log. */
int citizen_actor_actions_taken(void);

#endif /* CITIZEN_ACTOR_H */
