/* tyler_coldopen.c -- see tyler_coldopen.h for the full design rationale. */
#include "tyler_coldopen.h"
#include "story_ai.h"
#include "../reflux/reflux_runtime.h"
#include "tyler_voice_clips.h"

/* From PARENA stdlib/tyler/voice_mod.prn (generated, tyler_voice_mod.c). */
int on_tyler_voice_stretched_hold_ms(int, int, int);
#define TYLER_VOICE_PAD_MS 500

/* Beat hold actually used: the authored hold_ms, stretched when the Piper-rendered line for this
 * beat (tyler_voice_clips.h) would otherwise be cut off by the next beat. */
static unsigned int tyler_effective_hold(int beat_index, const TylerBeat *beat) {
    return (unsigned int)on_tyler_voice_stretched_hold_ms((int)g_tyler_voice_clip_ms[beat_index],
                                                          (int)beat->hold_ms, TYLER_VOICE_PAD_MS);
}

/* Marker positions match TYLER_VALHANNA_ICELAND_1986's own authored props exactly (see
 * cmd/nock_gen_tyler_levels/main.go, IDUNA): printer box at (4, 0.5, -4), ecs_screen box at
 * (6, 1.2, -6), tyler_exit_button box at (4, 0.9, -3.3), spawn/thermoses at (0, 1, 4). Beats
 * stand actors a real half-step in front of each prop rather than inside it. */
const TylerBeat g_tyler_coldopen_beats[TYLER_COLDOPEN_MAX_BEATS] = {
    { TYLER_ACTOR_BOTH,  0.0f, 1.0f, 4.0f, 3000,  0,
      "Bon. Ca, c'est nouveau. / Ne bouge pas. [EN: Okay. This is new. / Don't move.]" },
    { TYLER_ACTOR_HANA, -8.0f, 1.0f, -10.0f, 2500, 0,
      "On n'est pas au Michigan. [EN: We're not in Michigan.]" },
    { TYLER_ACTOR_TYLER, 4.0f, 1.0f, -4.5f, 3000, 0,
      "Le serveur est mort. Mais l'imprimante travaille encore. [EN: The server's dead. But the printer's still working.]" },
    { TYLER_ACTOR_HANA,  0.0f, 1.0f, 3.0f, 2500, 0,
      "Il y a quelqu'un ici. Ou il y avait quelqu'un ici il y a tres peu de temps. [EN: Someone's here. Or was, very recently.]" },
    { TYLER_ACTOR_TYLER, 4.0f, 1.0f, -4.5f, 3000, 0,
      "Je ne connais pas ce langage. Et pourtant je le lis. Le code source du Cube Mecanique. [EN: I don't know this language. And yet I'm reading it. The source code for the Mecha Cube.]" },
    { TYLER_ACTOR_HANA,  3.0f, 1.0f, -4.5f, 3000, 0,
      "Non. S'il te plait. Ne le lis pas. [EN: No. Please. Don't read it.]" },
    { TYLER_ACTOR_TYLER, 4.0f, 1.0f, -4.0f, 2500, 0,
      "(Tyler feeds the printout back into the printer, unread.)" },
    { TYLER_ACTOR_TYLER, 4.0f, 1.0f, -2.6f, 2000, 1,
      "Je crois qu'on nous regarde. [EN: I think we're being watched.]" },
};
const int g_tyler_coldopen_beat_count = TYLER_COLDOPEN_MAX_BEATS;

void tyler_coldopen_start(TylerColdOpenState *st, int tyler_slot, int hana_slot, unsigned int now_ms) {
    st->active = 1;
    st->tyler_slot = tyler_slot;
    st->hana_slot = hana_slot;
    st->current_beat = 0;
    st->beat_started_ms = now_ms;
    st->beat_triggered = 0;
    st->done = 0;
    (void)now_ms;
}

static void tyler_coldopen_trigger_actor(const TylerColdOpenState *st, TylerActor actor,
                                         float x, float y, float z, unsigned int hold_ms,
                                         unsigned int now_ms) {
    if (actor == TYLER_ACTOR_TYLER || actor == TYLER_ACTOR_BOTH) {
        story_ai_trigger_scripted(st->tyler_slot, x, y, z, hold_ms, now_ms);
    }
    if (actor == TYLER_ACTOR_HANA || actor == TYLER_ACTOR_BOTH) {
        story_ai_trigger_scripted(st->hana_slot, x, y, z, hold_ms, now_ms);
    }
}

void tyler_coldopen_tick(TylerColdOpenState *st, unsigned int now_ms, int next_level_id, TylerExitFn exit_fn) {
    if (!st->active || st->done) return;
    if (st->current_beat >= g_tyler_coldopen_beat_count) { st->done = 1; return; }

    const TylerBeat *beat = &g_tyler_coldopen_beats[st->current_beat];

    if (!st->beat_triggered) {
        tyler_coldopen_trigger_actor(st, beat->actor, beat->x, beat->y, beat->z,
                                     tyler_effective_hold(st->current_beat, beat), now_ms);
        /* REFLUX_ACTION_TYLER_BEAT -- the real "script it in with REFLUX" dispatch, once per
         * beat transition. Payload a = beat index; apps/lobby/src/main.c's subtitle renderer
         * polls this same log independently, with zero reference back to this file, matching
         * every other REFLUX dispatcher/subscriber pair's own established shape. */
        reflux_host_dispatch(REFLUX_ACTION_TYLER_BEAT, st->current_beat, 0, 0);
        if (beat->fires_button) {
            /* Tyler "presses" the button programmatically -- no player BTN_USE input involved,
             * matching story_buttons.h's own real, already-established REFLUX payload shape
             * (a = button author id [1, this level's only button], b = "presser" id, c unused).
             * Any future subscriber (a door, a bridge, this file's own exit call right below)
             * reacts to this exactly the way it would a real player press -- REFLUX doesn't
             * distinguish the two, by design. */
            reflux_host_dispatch(REFLUX_ACTION_BUTTON_PRESSED, 1, st->tyler_slot, 0);
        }
        st->beat_triggered = 1;
        st->beat_started_ms = now_ms;
    }

    if (now_ms - st->beat_started_ms < tyler_effective_hold(st->current_beat, beat)) return;

    if (beat->fires_button) {
        /* The new, scriptable "exit" primitive (S536) -- called directly the instant this final
         * beat's own hold expires, no LevelExit trigger volume involved at all. */
        if (exit_fn(next_level_id, 0, now_ms)) {
            st->done = 1;
        }
        /* A failed transition (fetch error) leaves st->done=0 and beat_triggered=1 -- the next
         * tick will keep retrying exit_fn (which has its own real internal debounce, see
         * story_force_level_transition) rather than getting stuck re-dispatching the button press
         * every tick. */
        return;
    }

    st->current_beat++;
    st->beat_triggered = 0;
}
