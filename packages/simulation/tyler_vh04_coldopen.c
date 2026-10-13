/* tyler_vh04_coldopen.c -- see tyler_vh04_coldopen.h. Logic mirrors tyler_e03_coldopen.c exactly. */
#include "tyler_vh04_coldopen.h"
#include "tyler_voice_tables.h"
#include "story_ai.h"
#include "../reflux/reflux_runtime.h"

/* Markers reuse the VH02 room's props (assets/tyler_levels/tyler_vh04_chaises_pliantes.json is the same room):
 * entry (0,1,9), the chair arc (0,1,5), the meter/rig spot (3,1,-3), thermoses (0,1,4), the radio (1,1,2),
 * the sealed-notebook corner (-2,1,1), exit button front (4,1,-2.6). TYLER slot = CAM, HANA slot = THE AUDITOR. */
const TylerBeat g_tyler_vh04_beats[TYLER_VH04_MAX_BEATS] = {
    { TYLER_ACTOR_NONE,    0.0f,  0.0f,   0.0f, 3000, 0,
      "(Une chaise pliante au pied du lit. Le Canard : Ce n'est pas mon tour.) [EN: (A folding chair at the foot of the bed. The Duck: It isn't my turn.)]" },
    { TYLER_ACTOR_HANA,    0.0f,  1.0f,   9.0f, 6000, 0,
      "Bonjour ! Ne vous levez pas. / J'apporte toujours une chaise de plus que la liste. [EN: Good morning! Don't get up. / I always bring one more chair than the list.]" },
    { TYLER_ACTOR_BOTH,    0.0f,  1.0f,   5.0f, 5500, 0,
      "Six. Il y en a sept. Il y a toujours quelqu'un qui arrive. / Soixante-trois. [EN: Six. There are seven. Somebody always turns up. / Sixty-three. (Cam, measuring a chair)]" },
    { TYLER_ACTOR_HANA,    3.0f,  1.0f,  -3.0f, 6000, 0,
      "Vingt-huit virgule sept. Pas un bruit. Un reste. Il faut deux corps dans la piece. [EN: Twenty-eight point seven. Not a noise. A remainder. It takes two bodies in the room.]" },
    { TYLER_ACTOR_HANA,    0.0f,  1.0f,   4.0f, 4500, 0,
      "C'est ma recette. Neuf sur dix. Quelqu'un a ajoute le dixieme. [EN: That's my recipe. Nine of ten. Someone added the tenth.]" },
    { TYLER_ACTOR_HANA,    1.0f,  1.0f,   2.0f, 2500, 0,
      "Vous avez une chaise aussi. / (La radio se tait.) [EN: You have a chair too. / (The radio goes quiet.)]" },
    { TYLER_ACTOR_BOTH,   -2.0f,  1.0f,   1.0f, 6500, 0,
      "Je ne copie pas ce que vous avez scelle. / Supprimer, c'est voler. Sceller, c'est choisir. [EN: I don't mirror what you sealed. / To suppress is to steal. To seal is to choose.]" },
    { TYLER_ACTOR_TYLER,   4.0f,  1.0f,  -2.6f, 3500, 1,
      "Il me la faut jeudi. Alors ce n'est pas du. / RENCONTRE : l'equipe rencontre un inspecteur [EN: I need it Thursday. Then it isn't due. / ENCOUNTER: THE CREW MEETS AN INSPECTOR]" },
};
const int g_tyler_vh04_beat_count = TYLER_VH04_MAX_BEATS;

/* Voice lines: build-time generated (tyler_voice_tables.h); empty + silent without Piper. */
const TylerVoiceLine g_tyler_vh04_voice_lines[TYLER_VOICE_MAX_LINES] = TYLER_VH04_VOICE_LINES_INIT;
const int g_tyler_vh04_voice_line_count = TYLER_VH04_VOICE_LINE_COUNT;

/* From PARENA stdlib/tyler/voice_mod.prn: stretch a beat's hold to fit its spoken clip. */
int on_tyler_voice_stretched_hold_ms(int, int, int);
static unsigned int tyler_vh04_hold(int beat_index, const TylerBeat *beat) {
    return (unsigned int)on_tyler_voice_stretched_hold_ms((int)g_tyler_vh04_voice_clip_ms[beat_index], (int)beat->hold_ms, 500);
}

void tyler_vh04_start(TylerVh04State *st, int tyler_slot, int hana_slot, unsigned int now_ms) {
    st->active = 1;
    st->tyler_slot = tyler_slot;
    st->hana_slot = hana_slot;
    st->current_beat = 0;
    st->beat_started_ms = now_ms;
    st->beat_triggered = 0;
    st->done = 0;
    (void)now_ms;
}

#define TYLER_VH04_SCRIPTED_LOCK_MS 600000U

static void tyler_vh04_trigger_actor(const TylerVh04State *st, TylerActor actor,
                                      float x, float y, float z, unsigned int now_ms) {
    if (actor == TYLER_ACTOR_TYLER || actor == TYLER_ACTOR_BOTH) {
        story_ai_trigger_scripted(st->tyler_slot, x, y, z, TYLER_VH04_SCRIPTED_LOCK_MS, now_ms);
    }
    if (actor == TYLER_ACTOR_HANA || actor == TYLER_ACTOR_BOTH) {
        story_ai_trigger_scripted(st->hana_slot, x, y, z, TYLER_VH04_SCRIPTED_LOCK_MS, now_ms);
    }
}

void tyler_vh04_tick(TylerVh04State *st, unsigned int now_ms, int next_level_id, TylerExitFn exit_fn) {
    if (!st->active || st->done) return;
    if (st->current_beat >= g_tyler_vh04_beat_count) { st->done = 1; return; }

    const TylerBeat *beat = &g_tyler_vh04_beats[st->current_beat];

    if (!st->beat_triggered) {
        tyler_vh04_trigger_actor(st, beat->actor, beat->x, beat->y, beat->z, now_ms);
        reflux_host_dispatch(REFLUX_ACTION_TYLER_BEAT, st->current_beat, 0, 0);
        if (beat->fires_button) {
            reflux_host_dispatch(REFLUX_ACTION_BUTTON_PRESSED, 1, st->tyler_slot, 0);
        }
        st->beat_triggered = 1;
        st->beat_started_ms = now_ms;
    }

    if (now_ms - st->beat_started_ms < tyler_vh04_hold(st->current_beat, beat)) return;

    if (beat->fires_button) {
        if (exit_fn(next_level_id, 0, now_ms)) {
            st->done = 1;
        }
        return;
    }

    st->current_beat++;
    st->beat_triggered = 0;
}

void tyler_vh04_get_view(const TylerVh04State *st, unsigned int now_ms, TylerBeatView *out) {
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
