/* tyler_fb01_coldopen.c -- see tyler_fb01_coldopen.h. Logic mirrors tyler_e03_coldopen.c exactly. */
#include "tyler_fb01_coldopen.h"
#include "story_ai.h"
#include "../reflux/reflux_runtime.h"

/* Flashback apartment markers (assets/tyler_levels/tyler_fb01_apartment.json): doorway (0,1,4),
 * couch (1.5,1,3), bed (-1,1,1), door_exit (0,1,4.5). */
const TylerBeat g_tyler_fb01_beats[TYLER_FB01_MAX_BEATS] = {
    { TYLER_ACTOR_BOTH,  0.0f, 1.0f,  4.0f, 3000, 0,
      "On pourrait... juste rester. Comme des gens normaux. [EN: We could just stay in. Like normal people.]" },
    { TYLER_ACTOR_HANA,  1.5f, 1.0f,  3.0f, 2500, 0,
      "Les gens normaux ne disent pas ca a voix haute. [EN: Normal people don't say that sentence out loud.]" },
    { TYLER_ACTOR_BOTH, -1.0f, 1.0f,  1.0f, 3000, 0,
      "Qu'est-ce que tu as fait. [EN: What did you do.]" },
    { TYLER_ACTOR_HANA,  0.0f, 1.0f,  2.0f, 2000, 0,
      "Chaussures. Maintenant. [EN: Shoes. Now.]" },
    { TYLER_ACTOR_TYLER, 0.5f, 1.0f,  3.5f, 3000, 0,
      "Tout ceci... c'est une illusion, tu sais. On est en route pour Mars. [EN: This is all an illusion, you know. We're on our way to Mars.]" },
    { TYLER_ACTOR_TYLER, 0.5f, 1.0f,  3.5f, 3000, 0,
      "Tu es Persephone. Tu es... ma soeur. [EN: You're Persephone. You're... my sister.]" },
    { TYLER_ACTOR_HANA,  0.0f, 1.0f,  3.0f, 2500, 0,
      "Tu as pisse dans mon lit. [EN: You pissed in my bed.]" },
    { TYLER_ACTOR_TYLER, 0.0f, 1.0f,  4.5f, 2500, 1,
      "Ca va tout avoir un sens quand on se reveillera. [EN: It's all going to make sense when we wake up.]" },
};
const int g_tyler_fb01_beat_count = TYLER_FB01_MAX_BEATS;

/* No rendered VO this session (Piper/TYLER repo unavailable) -- empty table, honestly labeled. */
const TylerVoiceLine g_tyler_fb01_voice_lines[1] = { { 0, TYLER_ACTOR_NONE, 0, 0, "" } };
const int g_tyler_fb01_voice_line_count = 0;

void tyler_fb01_start(TylerFb01State *st, int tyler_slot, int hana_slot, unsigned int now_ms) {
    st->active = 1;
    st->tyler_slot = tyler_slot;
    st->hana_slot = hana_slot;
    st->current_beat = 0;
    st->beat_started_ms = now_ms;
    st->beat_triggered = 0;
    st->done = 0;
    (void)now_ms;
}

#define TYLER_FB01_SCRIPTED_LOCK_MS 600000U

static void tyler_fb01_trigger_actor(const TylerFb01State *st, TylerActor actor,
                                      float x, float y, float z, unsigned int now_ms) {
    if (actor == TYLER_ACTOR_TYLER || actor == TYLER_ACTOR_BOTH) {
        story_ai_trigger_scripted(st->tyler_slot, x, y, z, TYLER_FB01_SCRIPTED_LOCK_MS, now_ms);
    }
    if (actor == TYLER_ACTOR_HANA || actor == TYLER_ACTOR_BOTH) {
        story_ai_trigger_scripted(st->hana_slot, x, y, z, TYLER_FB01_SCRIPTED_LOCK_MS, now_ms);
    }
}

void tyler_fb01_tick(TylerFb01State *st, unsigned int now_ms, int next_level_id, TylerExitFn exit_fn) {
    if (!st->active || st->done) return;
    if (st->current_beat >= g_tyler_fb01_beat_count) { st->done = 1; return; }

    const TylerBeat *beat = &g_tyler_fb01_beats[st->current_beat];

    if (!st->beat_triggered) {
        tyler_fb01_trigger_actor(st, beat->actor, beat->x, beat->y, beat->z, now_ms);
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

void tyler_fb01_get_view(const TylerFb01State *st, unsigned int now_ms, TylerBeatView *out) {
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
