/* tyler_e03_coldopen.c -- see tyler_e03_coldopen.h. Logic mirrors tyler_coldopen.c exactly, minus
 * the voice-driver stretch (no clips, so hold_ms is used as-authored, un-stretched). */
#include "tyler_e03_coldopen.h"
#include "story_ai.h"
#include "../reflux/reflux_runtime.h"

/* Episode 3 level markers (assets/tyler_levels/tyler_e03_dont_check.json): apartment_dawn (0,1,6),
 * hana_approach (2,1,5), street_mid (4,1,2), street_far (6,1,-2), case_hold (6,1,-5),
 * archivist_door (6,1,-8), exit_mark (6,1,-9). */
const TylerBeat g_tyler_e03_beats[TYLER_E03_MAX_BEATS] = {
    { TYLER_ACTOR_BOTH,  0.0f, 1.0f,  6.0f, 3000, 0,
      "Petit. C'est comme ca que ca se repand. / Ne verifie rien aujourd'hui. [EN: Small is how it spreads. / Don't check anything today.]" },
    { TYLER_ACTOR_HANA,  2.0f, 1.0f,  5.0f, 2500, 0,
      "La porte a laquelle tu penses. [EN: The door you're thinking about.]" },
    { TYLER_ACTOR_TYLER, 4.0f, 1.0f,  2.0f, 2500, 0,
      "Rien n'est \"juste\" une histoire, maintenant. [EN: Nothing is \"just\" a story anymore.]" },
    { TYLER_ACTOR_BOTH,  6.0f, 1.0f, -2.0f, 3000, 0,
      "Reste droit devant. Se retourner est plus dangereux. [EN: Stay looking straight ahead. Turning is more dangerous.]" },
    { TYLER_ACTOR_TYLER, 6.0f, 1.0f, -5.0f, 3000, 0,
      "Je veux l'ouvrir. L'univers ne peut pas me commander. [EN: I want to open it. The universe can't boss me around.]" },
    { TYLER_ACTOR_BOTH,  6.0f, 1.0f, -8.0f, 2500, 0,
      "ARCHIVISTE. Ce n'etait pas dans les recherches. [EN: ARCHIVIST. That wasn't in the background research.]" },
    { TYLER_ACTOR_NONE,  0.0f, 0.0f,  0.0f, 2000, 0,
      "(Une main gantee prend la mallette. Aucun visage.) [EN: (A gloved hand takes the case. No face.)]" },
    { TYLER_ACTOR_TYLER, 6.0f, 1.0f, -9.0f, 2000, 1,
      "Ne verifie pas. [EN: Don't check.]" },
};
const int g_tyler_e03_beat_count = TYLER_E03_MAX_BEATS;

/* No rendered VO this session (Piper/TYLER repo unavailable) -- empty table, honestly labeled. */
const TylerVoiceLine g_tyler_e03_voice_lines[1] = { { 0, TYLER_ACTOR_NONE, 0, 0, "" } };
const int g_tyler_e03_voice_line_count = 0;

void tyler_e03_start(TylerE03State *st, int tyler_slot, int hana_slot, unsigned int now_ms) {
    st->active = 1;
    st->tyler_slot = tyler_slot;
    st->hana_slot = hana_slot;
    st->current_beat = 0;
    st->beat_started_ms = now_ms;
    st->beat_triggered = 0;
    st->done = 0;
    (void)now_ms;
}

#define TYLER_E03_SCRIPTED_LOCK_MS 600000U

static void tyler_e03_trigger_actor(const TylerE03State *st, TylerActor actor,
                                     float x, float y, float z, unsigned int now_ms) {
    if (actor == TYLER_ACTOR_TYLER || actor == TYLER_ACTOR_BOTH) {
        story_ai_trigger_scripted(st->tyler_slot, x, y, z, TYLER_E03_SCRIPTED_LOCK_MS, now_ms);
    }
    if (actor == TYLER_ACTOR_HANA || actor == TYLER_ACTOR_BOTH) {
        story_ai_trigger_scripted(st->hana_slot, x, y, z, TYLER_E03_SCRIPTED_LOCK_MS, now_ms);
    }
    /* TYLER_ACTOR_NONE (beat 6, the silent Archivist-door action beat): nobody moves -- both
     * actors hold their previous beat's marker exactly, same convention VH01's own non-named
     * actors use (see tyler_coldopen.c's TYLER_SCRIPTED_LOCK_MS comment). */
}

void tyler_e03_tick(TylerE03State *st, unsigned int now_ms, int next_level_id, TylerExitFn exit_fn) {
    if (!st->active || st->done) return;
    if (st->current_beat >= g_tyler_e03_beat_count) { st->done = 1; return; }

    const TylerBeat *beat = &g_tyler_e03_beats[st->current_beat];

    if (!st->beat_triggered) {
        tyler_e03_trigger_actor(st, beat->actor, beat->x, beat->y, beat->z, now_ms);
        reflux_host_dispatch(REFLUX_ACTION_TYLER_BEAT, st->current_beat, 0, 0);
        if (beat->fires_button) {
            reflux_host_dispatch(REFLUX_ACTION_BUTTON_PRESSED, 1, st->tyler_slot, 0);
        }
        st->beat_triggered = 1;
        st->beat_started_ms = now_ms;
    }

    if (now_ms - st->beat_started_ms < beat->hold_ms) return;

    if (beat->fires_button) {
        if (exit_fn(next_level_id, 0, now_ms)) {
            st->done = 1;
        }
        return;
    }

    st->current_beat++;
    st->beat_triggered = 0;
}

void tyler_e03_get_view(const TylerE03State *st, unsigned int now_ms, TylerBeatView *out) {
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
