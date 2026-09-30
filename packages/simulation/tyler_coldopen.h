/* tyler_coldopen.h -- S536, TYLER VALHANNA's pilot cold open (episodes/vh01_valhanna_coldopen.md)
 * as a real, playable MODE_TYLER scripted sequence. Founder real-time: "bring it to life with the
 * shankpit engine... use the engine to tell the story like half life - the characters speak... you
 * are a floating orb like a wisp... write events for the half life style coordinated animations...
 * script it in with REFLUX... this is a demo for BIG_O" (docs2/specs/BIGO_ENGINE_MERGE_NORTHSTAR.md,
 * S536 BIG_O<->SHANKPIT engine merge track).
 *
 * Real, deliberate v0 shape, named plainly:
 *   - Beats are a fixed, ordered table (TylerBeat), not a general scripting language -- the same
 *     "batteries-included, zero-scripting default" bar docs/STORY_SYSTEM_NORTHSTAR.md's own Part 3
 *     names for a story engine's simplest case. A future PARENA-scripted version of this coordinator
 *     is real, later work, not attempted here.
 *   - Each beat calls story_ai_trigger_scripted ONCE per actor at beat start with a hand-tuned
 *     hold_ms covering the WHOLE beat (travel + hold) rather than querying story_ai.c for a real
 *     "is this scripted move actually done yet" signal -- no such query exists in story_ai.h today
 *     (checked directly before writing this), and gsync's own real multi-actor-arrival primitive
 *     (packages/goldenband/gsync.c) is not wired into story_ai_trigger_scripted's own call path
 *     either (AI_SCRIPTED_ANIMATION_NORTHSTAR.md's own still-open gap). This coordinator advances
 *     its own beat clock independently instead -- an honest, hand-authored-timing simplification,
 *     not a claim that gsync-style true simultaneous-arrival ignition is happening here.
 *   - REFLUX_ACTION_TYLER_BEAT is dispatched once per beat transition -- the real "script it in
 *     with REFLUX" mechanism the founder asked for: this coordinator never knows or cares who
 *     reads it (today: apps/lobby/src/main.c's subtitle renderer), matching REFLUX's own
 *     established dispatcher-never-knows-the-subscriber shape.
 *   - The final beat calls story_force_level_transition (apps/server/src/main.c, S536) directly --
 *     the new, scriptable "exit" primitive -- rather than requiring a player to walk into a
 *     LevelExit trigger volume, since a non-colliding STATE_SPECTATOR wisp has no reliable way to
 *     do that (and TYLER_VALHANNA_ICELAND_1986 authors no LevelExit at all, on purpose --
 *     "spawn but no exit").
 */
#ifndef SHANKPIT_TYLER_COLDOPEN_H
#define SHANKPIT_TYLER_COLDOPEN_H

#include "../common/protocol.h"

#define TYLER_COLDOPEN_MAX_BEATS 8
#define TYLER_COLDOPEN_SUBTITLE_LEN 200

/* Which actor(s) a beat moves. Tyler/Hana are whichever two player slots story_ai_spawn_enemy
 * returned when this level's own authored characters (AI_ROLE_STORY_ALLY, AI_KIT_STAN/AI_KIT_MIKE
 * -- see internal/shankpit's own Character rows for TYLER_VALHANNA_ICELAND_1986) were spawned. */
typedef enum {
    TYLER_ACTOR_NONE = 0,
    TYLER_ACTOR_TYLER,
    TYLER_ACTOR_HANA,
    TYLER_ACTOR_BOTH
} TylerActor;

typedef struct {
    TylerActor actor;
    float x, y, z;          /* marker position (ignored for TYLER_ACTOR_NONE) */
    unsigned int hold_ms;   /* how long this beat runs before advancing */
    int fires_button;       /* 1 on the one beat where Tyler "presses" the exit button */
    char subtitle[TYLER_COLDOPEN_SUBTITLE_LEN]; /* French line + bracketed English, one string --
                                                    matches this cold open's own script's own
                                                    SUBTITLED convention, episodes/vh01_valhanna_
                                                    coldopen.md */
} TylerBeat;

typedef struct {
    int active;             /* 1 once tyler_coldopen_start has real player slots to drive */
    int tyler_slot;
    int hana_slot;
    int current_beat;
    unsigned int beat_started_ms;
    int beat_triggered;      /* 1 once this beat's own story_ai_trigger_scripted call has fired --
                                 a beat only ever triggers movement once, on its own first tick */
    int done;                /* 1 once the final beat's own hold has expired and the exit fired */
} TylerColdOpenState;

extern const TylerBeat g_tyler_coldopen_beats[TYLER_COLDOPEN_MAX_BEATS];
extern const int g_tyler_coldopen_beat_count;

/* tyler_coldopen_start -- call once, right after this level's two AI_ROLE_STORY_ALLY characters
 * have been spawned (server_apply_custom_level's own existing character-spawn pass already does
 * this via story_ai_spawn_enemy for any NOCK-authored character; tyler_slot/hana_slot are
 * whichever two player ids that pass returned, in authored order -- Tyler first, Hana second,
 * matching TYLER_VALHANNA_ICELAND_1986's own authored characters array order). */
void tyler_coldopen_start(TylerColdOpenState *st, int tyler_slot, int hana_slot, unsigned int now_ms);

/* tyler_coldopen_tick -- real per-server-tick driver, called from apps/server/src/main.c's main
 * loop whenever local_state.game_mode == MODE_TYLER and st->active. Advances beats on hold-expiry,
 * dispatches REFLUX_ACTION_TYLER_BEAT on every transition, and calls story_force_level_transition
 * on the final beat via the caller-supplied exit_fn (kept as a function pointer rather than a
 * direct call so this file doesn't need apps/server/src/main.c's own static functions visible to
 * it -- main.c passes story_force_level_transition in directly). */
typedef int (*TylerExitFn)(int next_level_id, int target_spawner_id, unsigned int now_ms);
void tyler_coldopen_tick(TylerColdOpenState *st, unsigned int now_ms, int next_level_id, TylerExitFn exit_fn);

#endif /* SHANKPIT_TYLER_COLDOPEN_H */
