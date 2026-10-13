/* tyler_vh03_coldopen.c -- see tyler_vh03_coldopen.h. Logic mirrors tyler_e03_coldopen.c exactly. */
#include "tyler_vh03_coldopen.h"
#include "tyler_voice_tables.h"
#include "story_ai.h"
#include "../reflux/reflux_runtime.h"

/* Markers reuse the VH02 room's props (assets/tyler_levels/tyler_vh03_day_out_of_time.json is the same room):
 * entry (0,1,9), thermoses (0,1,4), printer front (4,1,-4.5), exit button front (4,1,-2.6), the call-sheet
 * spot (3,1,-3). TYLER slot = CAM, HANA slot = THE SOUND GUY (and, for the arithmetic, the PA). */
const TylerBeat g_tyler_vh03_beats[TYLER_VH03_MAX_BEATS] = {
    { TYLER_ACTOR_NONE,    0.0f,  0.0f,   0.0f, 3000, 0,
      "(Le Canard compte treize pages. Ce n'est pas comme ca que le mois passe.) [EN: (The Duck counts thirteen pages. That's not how the month goes.)]" },
    { TYLER_ACTOR_BOTH,    0.0f,  1.0f,   9.0f, 5000, 0,
      "L'horloge a perdu la date. / Elle ne l'a pas perdue. Il n'y en a pas. [EN: The clock lost the date. / It didn't lose it. There isn't one.]" },
    { TYLER_ACTOR_HANA,    3.0f,  1.0f,  -3.0f, 5500, 0,
      "Douze mois. Trente et un, vingt-huit, trente et un... trois cent soixante-cinq. [EN: Twelve months. Thirty-one, twenty-eight, thirty-one... three hundred sixty-five.]" },
    { TYLER_ACTOR_HANA,    3.0f,  1.0f,  -3.0f, 5000, 0,
      "Treize fois vingt-huit. Trois cent soixante-quatre. Il en reste un. [EN: Thirteen of twenty-eight. Three hundred sixty-four. There's one left over.]" },
    { TYLER_ACTOR_BOTH,    3.0f,  1.0f,  -3.0f, 4000, 0,
      "Et les annees bissextiles ? / Alors il y en a deux. [EN: And in a leap year? / Then there are two.]" },
    { TYLER_ACTOR_HANA,    4.0f,  1.0f,  -4.5f, 6500, 0,
      "Pas de repos dans une production. Continuez a tourner. / Il n'a rien prevu pour aujourd'hui. [EN: Producer (radio): No rest in a production. Keep rolling. / He has no plan for today. (DAY 365 --)]" },
    { TYLER_ACTOR_BOTH,    0.0f,  1.0f,   2.0f, 4500, 0,
      "Je capte vingt-huit virgule sept. / C'est presque vingt-huit. [EN: I'm getting twenty-eight point seven. / That's almost twenty-eight.]" },
    { TYLER_ACTOR_TYLER,   4.0f,  1.0f,  -2.6f, 3000, 1,
      "Aujourd'hui n'est pas un jour de la semaine. RENCONTRE : [non consignee] [EN: TODAY IS NOT A DAY OF THE WEEK. ENCOUNTER: [UNLOGGED]]" },
};
const int g_tyler_vh03_beat_count = TYLER_VH03_MAX_BEATS;

/* Voice lines: build-time generated (tyler_voice_tables.h); empty + silent without Piper. */
const TylerVoiceLine g_tyler_vh03_voice_lines[TYLER_VOICE_MAX_LINES] = TYLER_VH03_VOICE_LINES_INIT;
const int g_tyler_vh03_voice_line_count = TYLER_VH03_VOICE_LINE_COUNT;

/* From PARENA stdlib/tyler/voice_mod.prn: stretch a beat's hold to fit its spoken clip. */
int on_tyler_voice_stretched_hold_ms(int, int, int);
static unsigned int tyler_vh03_hold(int beat_index, const TylerBeat *beat) {
    return (unsigned int)on_tyler_voice_stretched_hold_ms((int)g_tyler_vh03_voice_clip_ms[beat_index], (int)beat->hold_ms, 500);
}

void tyler_vh03_start(TylerVh03State *st, int tyler_slot, int hana_slot, unsigned int now_ms) {
    st->active = 1;
    st->tyler_slot = tyler_slot;
    st->hana_slot = hana_slot;
    st->current_beat = 0;
    st->beat_started_ms = now_ms;
    st->beat_triggered = 0;
    st->done = 0;
    (void)now_ms;
}

#define TYLER_VH03_SCRIPTED_LOCK_MS 600000U

static void tyler_vh03_trigger_actor(const TylerVh03State *st, TylerActor actor,
                                      float x, float y, float z, unsigned int now_ms) {
    if (actor == TYLER_ACTOR_TYLER || actor == TYLER_ACTOR_BOTH) {
        story_ai_trigger_scripted(st->tyler_slot, x, y, z, TYLER_VH03_SCRIPTED_LOCK_MS, now_ms);
    }
    if (actor == TYLER_ACTOR_HANA || actor == TYLER_ACTOR_BOTH) {
        story_ai_trigger_scripted(st->hana_slot, x, y, z, TYLER_VH03_SCRIPTED_LOCK_MS, now_ms);
    }
}

void tyler_vh03_tick(TylerVh03State *st, unsigned int now_ms, int next_level_id, TylerExitFn exit_fn) {
    if (!st->active || st->done) return;
    if (st->current_beat >= g_tyler_vh03_beat_count) { st->done = 1; return; }

    const TylerBeat *beat = &g_tyler_vh03_beats[st->current_beat];

    if (!st->beat_triggered) {
        tyler_vh03_trigger_actor(st, beat->actor, beat->x, beat->y, beat->z, now_ms);
        reflux_host_dispatch(REFLUX_ACTION_TYLER_BEAT, st->current_beat, 0, 0);
        if (beat->fires_button) {
            reflux_host_dispatch(REFLUX_ACTION_BUTTON_PRESSED, 1, st->tyler_slot, 0);
        }
        st->beat_triggered = 1;
        st->beat_started_ms = now_ms;
    }

    if (now_ms - st->beat_started_ms < tyler_vh03_hold(st->current_beat, beat)) return;

    if (beat->fires_button) {
        if (exit_fn(next_level_id, 0, now_ms)) {
            st->done = 1;
        }
        return;
    }

    st->current_beat++;
    st->beat_triggered = 0;
}

void tyler_vh03_get_view(const TylerVh03State *st, unsigned int now_ms, TylerBeatView *out) {
    out->tyler_slot = st->tyler_slot;
    out->hana_slot = st->hana_slot;
    out->done = st->done;
    out->beat = -1;
    out->beat_elapsed_ms = 0;
    if (!st->active) return;
    if (st->beat_triggered) out->beat = st->current_beat;
    else if (st->current_beat > 0) out->beat = st->current_beat - 1;
    else return;
    out->beat_elapsed_ms = (now_ms >= st->beat_started_ms) ? now_ms - st->beat_started_ms : 0;
}
