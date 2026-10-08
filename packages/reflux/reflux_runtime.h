/* packages/reflux/reflux_runtime.h -- REFLUX, SHANKPIT's own real cross-mod/cross-object pub/sub
 * layer (S485, founder real-time: "all of these things proximity sensor button push looking a
 * certain way - these are all events that can be subscribed to for the designers to bring life
 * into the world - i want to follow the pub sub model ... we need to use the same pub sub mod as
 * ECOWAR" -> "bring in the ecowar pub sub mod" -> "but keep all of the code in shankpit"). A real,
 * native port of ECOWAR/packages/reflux/reflux_runtime.h (EMILY/BACKLOG.md SECTION 380/381) --
 * same API, same ABI, same PARENA stdlib file (stdlib/reflux/reflux.prn is already engine-
 * agnostic, a thin FFI shim over reflux_host_*), zero cross-repo dependency: this is SHANKPIT's
 * OWN copy of the runtime, with SHANKPIT's own log and SHANKPIT's own action-type constants,
 * matching the founder's own explicit "keep all of the code in shankpit" instruction.
 *
 * Real, checked-first constraint this design works within (same one ECOWAR's own doc names): VS0
 * (PARENA's current compiler) has no function pointers or closures -- a true push-callback/
 * subscriber-list dispatch table isn't buildable at the language level today. What's real and
 * buildable: a single, shared, append-only action log any mod (or, here, any native C
 * dispatcher/subscriber) can DISPATCH into and any OTHER one can POLL from, tracking its own
 * last-seen cursor. This is real, genuine inversion of control even without closures: a
 * dispatcher (a button) never needs to know who, if anyone, is listening; a subscriber (a door, a
 * vent, a bridge) never needs the dispatcher to know it exists -- the only real connection is the
 * shared log and an agreed-upon action-type constant, the same "both sides hardcode the same
 * literal, documented cross-reference" pattern protocol.h's own wire constants already use.
 *
 * Founder's own worked example, verbatim: "the bridge listens for a button the button has no idea
 * the bridge exists" -- REFLUX_ACTION_BUTTON_PRESSED is exactly that: dispatched once, by ID, by
 * whatever pressed a button; any number of subscribers (a door, a bridge rig, an NPC's own
 * scripted reaction) can independently poll for it filtered to that same button ID, with zero
 * coupling to each other or to the button itself beyond the shared action-type+ID convention.
 */
#ifndef SHANKPIT_REFLUX_RUNTIME_H
#define SHANKPIT_REFLUX_RUNTIME_H

/* Real, named action types every SHANKPIT dispatcher/subscriber pair agrees on by convention.
 * Payload convention (a, b, c) is per-action-type, documented at each dispatch site -- REFLUX
 * itself has no schema beyond the 3 raw I32 scalars (VS0's own scalar-only ABI). */

/* Dispatched by the button-interact handler (packages/simulation/story_buttons.h) on a real,
 * edge-triggered BTN_USE press within range of a placed button's own box. Payload: a = the
 * button's own author-assigned button_id, b = the pressing player's id, c unused (0). */
#define REFLUX_ACTION_BUTTON_PRESSED 1

/* Real, named, NOT YET DISPATCHED anywhere (S485 follow-up, proximity sensors -- founder
 * real-time: "sometimes they will open doors to fake motion detector doors - but mainly
 * proximity detection will be to queue events that drive the story"). Reserved here now so the
 * action-type numbering is stable once a real dispatcher lands, matching how REFLUX_ACTION_
 * BUTTON_PRESSED's own payload convention was documented before its first real subscriber
 * existed. Payload (once built): a = sensor_id, b = player_id, c unused (0). */
#define REFLUX_ACTION_PROXIMITY_ENTER 2
#define REFLUX_ACTION_PROXIMITY_EXIT  3

/* Real, named, NOT YET DISPATCHED anywhere (S485 follow-up, gaze/look-at triggers -- founder
 * real-time, citing Half-Life's own scripted_sequence: "half life would script certain scripted
 * sequences to only trigger when the player actually looks at the subject that is part of the
 * action"). Payload (once built): a = subject_id, b = player_id, c = dwell_ms (how long the
 * player had been looking when the threshold fired). */
#define REFLUX_ACTION_LOOK_AT 4

/* TYLER VALHANNA cold open (S536, episodes/vh01_valhanna_coldopen.md), packages/simulation/
 * tyler_coldopen.c -- dispatched once per scripted-sequence beat transition (Tyler/Hana wake,
 * Tyler reaches the printer, Hana pleads, Tyler feeds the paper back in, Tyler hits the button,
 * ...). This is the real "script it in with REFLUX" mechanism: the coordinator that drives
 * story_ai_trigger_scripted never knows or cares who's listening, and the client-side subtitle
 * renderer (apps/lobby/src/main.c) never knows or cares who dispatched it -- same dispatcher-
 * never-knows-the-subscriber shape REFLUX_ACTION_BUTTON_PRESSED already established. Payload:
 * a = beat id (see TylerBeat enum, tyler_coldopen.h), b/c unused (0). */
#define REFLUX_ACTION_TYLER_BEAT 5

/* BIG_O engine merge phase 6 (EMILY/BACKLOG.md SECTION 536) -- real world-clock events, dispatched
 * by packages/simulation/world_alert_bridge.c on every real day_night_clock (phase 1) phase/
 * weather transition, matching PARENA/stdlib/shankpit/world_alerts_mod.prn's own expected action-
 * type numbering (copied verbatim from BIG_O's stdlib/big_o/world_alerts_mod.prn, which reserves
 * 103/104 for a zombie population). Payload: PHASE_CHANGED a=old phase b=new phase c=day number;
 * WEATHER_CHANGED a=old weather b=new weather c=0. */
#define REFLUX_ACTION_PHASE_CHANGED 101
#define REFLUX_ACTION_WEATHER_CHANGED 102

/* MODE_ZOMBIES/MODE_SURVIVAL population events -- founder real-time, 2026-10-08: "bring in all
 * the BIG_O affordances into shankpit zombies... we need it all evented with reflux" (no
 * smartphone bridge exists yet for this population -- unlike world_alert_bridge.c's own real
 * phone_notify consumer, there is deliberately no *_bridge.c here yet, same "the button has no
 * idea the bridge exists" shape, just with no real subscriber built so far). Dispatched directly
 * from packages/simulation/witness_ai.c at the real moment each state change happens, not by
 * polling a diff each tick (ZOMBIE_SPAWNED: witness_ai_spawn_zombie; ZOMBIE_HARVESTED: the
 * per-zombie tick loop's own STATE_DEAD edge, once per corpse; ZOMBIE_MOOD_ESCALATED: the same
 * tick loop's own mood edge, the instant a zombie crosses into witness_live_zombie_is_
 * witnessable_event(); WITNESS_ESCALATED: the citizen-witnessing loop's own state edge, the
 * instant a human crosses into {WS_SILENCING, WS_ENGAGE} -- the same real, reachable threshold
 * the dispatch loop right below it already uses to send The Men, not BIG_O/core/avian_live.h's
 * own literal WS_COMPROMISED threshold, which is unreachable from this call site: witness_live.h
 * always passes compromised=0, and witness_rules.c's generated witness_state()/npc_next_state()
 * only ever return WS_COMPROMISED when compromised==1). These were reserved-but-unused here until this
 * change -- SHANKPIT's own MODE_ZOMBIES population (witness_ai.c) now exists where the original
 * comment said it didn't. */
#define REFLUX_ACTION_ZOMBIE_SPAWNED 103        /* a = zombie's player_id, b/c unused (0) */
#define REFLUX_ACTION_ZOMBIE_HARVESTED 104      /* a = player_id, b = final ZombieMood, c unused */
#define REFLUX_ACTION_ZOMBIE_MOOD_ESCALATED 105 /* a = player_id, b = new ZombieMood (>=HUNTING), c unused */
#define REFLUX_ACTION_WITNESS_ESCALATED 106     /* a = citizen player_id, b = new WS_* state, c unused */

/* Same MODE_ZOMBIES/MODE_SURVIVAL population, continuing the 2026-10-08 "evented with reflux" pass
 * into the two entity families the first round named as still-open (witness_ai.h's own top doc
 * comment, EMILY/BACKLOG.md #4450): Giant Zombie Bug and The Men. Dispatched directly from
 * packages/simulation/witness_ai.c at the real moment each happens (GIANT_BUG_ATE_ZOMBIE: the
 * giant-bug tick loop's own real eaten_zi>=0 branch; MEN_DISPATCHED: the real edge where a Man
 * acquires a NEW hunt target it didn't have last tick, tracked per-Man so a Man that's still
 * chasing the same target doesn't re-fire every tick; MEN_RESOLVED: the dispatch/resolution loop's
 * own real resolved_count>0 branch). First real subscriber: packages/simulation/
 * zombies_hud_bridge.c (phase 8), which also finally gives REFLUX_ACTION_ZOMBIE_MOOD_ESCALATED/
 * WITNESS_ESCALATED above their first real consumer too -- see that file's own doc comment. */
#define REFLUX_ACTION_GIANT_BUG_ATE_ZOMBIE 107  /* a = bug player_id, b = eaten zombie's player_id, c unused */
#define REFLUX_ACTION_MEN_DISPATCHED 108        /* a = The Men player_id, b = target citizen player_id, c unused */
#define REFLUX_ACTION_MEN_RESOLVED 109           /* a = The Men player_id, b = resolved_count, c = zone */

/* Pheromone command tool (BIG_O/NORTHSTAR.md section 10, "tactical pheromone arrays... paint a
 * target") going live in MODE_ZOMBIES/MODE_SURVIVAL (2026-10-08, same-day continuation after the
 * REFLUX_ACTION_MEN_RESOLVED pass above). Dispatched from witness_ai_try_throw_pheromone the
 * instant a player's own throw is accepted (not rate-limited away by its own cooldown). Not in
 * zombies_awareness_rules.prn's 105-109 "should ping the HUD" range on purpose -- this is the
 * THROWING player's own deliberate action, not a new fact about the world noticing THEM, so it
 * never feeds the awareness compass/intensity readout that event family drives. */
#define REFLUX_ACTION_PHEROMONE_THROWN 110      /* a = thrower player_id, b = marker slot index, c unused */

/* The basement's shadow war (BIG_O/NORTHSTAR.md Sec.1/4; SHANKPIT docs2/specs/
 * BIGO_ENGINE_MERGE_NORTHSTAR.md Sec.3's own "entirely unstarted" gap) resolving a battle --
 * dispatched from shadow_war_deploy the instant a deterministic bot battle finishes (win, loss
 * or draw), matching every other meaningful-event-gets-a-REFLUX-action precedent in this file.
 * No subscriber yet -- same honest "reserved for a future HUD/league consumer" shape
 * REFLUX_ACTION_ZOMBIE_SPAWNED carried before zombies_hud_bridge.c became its first real one. */
#define REFLUX_ACTION_SHADOW_WAR_RESOLVED 111    /* a = player_id, b = SHADOW_WAR_RESULT_*, c = new elo */

#define REFLUX_LOG_CAPACITY 256

typedef struct {
    int action_type;
    int a, b, c; /* real, action-specific scalar payload -- matches VS0's own scalar-only ABI */
} RefluxAction;

typedef struct {
    RefluxAction actions[REFLUX_LOG_CAPACITY];
    int total_dispatched; /* every dispatch ever, even past capacity */
} RefluxLog;

void reflux_log_reset(RefluxLog *log);
void reflux_log_dispatch(RefluxLog *log, int action_type, int a, int b, int c);

/* Number of actions currently retained (min(total_dispatched, CAPACITY)). */
int reflux_log_length(const RefluxLog *log);

/* index 0 = oldest still-retained action, reflux_log_length()-1 = most recent. NULL if index is
 * out of that range. */
const RefluxAction *reflux_log_at(const RefluxLog *log, int index);

/* ---- The one, real, per-server-process REFLUX log + its PARENA-callable host wrappers --------
 * reflux_host_* are the real functions PARENA/stdlib/reflux/reflux.prn's own #target inline-c
 * bodies call into by name -- same call-by-name FFI convention story_doors.h's dlopen'd door
 * scripts already use, applied to a shared log instead of a per-door door_tick export. Real,
 * deliberate scope: one global log per running server process (SHANKPIT has no per-match arena
 * boundary the way ECOWAR does -- a level load/reload does NOT reset this log, since a level
 * transition mid-server-run is a real, normal event here, not a match boundary). A future
 * per-level-instance log is real, later work if cross-level event bleed ever becomes a real
 * problem -- not assumed a problem today. */
void reflux_host_reset(void);
void reflux_host_dispatch(int action_type, int a, int b, int c);
int reflux_host_log_size(void);
int reflux_host_action_type_at(int index); /* -1 if index out of range */
int reflux_host_action_a_at(int index);    /* 0 if index out of range */
int reflux_host_action_b_at(int index);    /* 0 if index out of range */
int reflux_host_action_c_at(int index);    /* 0 if index out of range */

#endif /* SHANKPIT_REFLUX_RUNTIME_H */
