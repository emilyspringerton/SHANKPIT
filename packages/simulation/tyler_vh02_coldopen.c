/* tyler_vh02_coldopen.c -- see tyler_vh02_coldopen.h. Logic mirrors tyler_e03_coldopen.c exactly. */
#include "tyler_vh02_coldopen.h"
#include "tyler_voice_tables.h"
#include "story_ai.h"
#include "../reflux/reflux_runtime.h"

/* Markers reuse TYLER_VALHANNA_ICELAND_1986's props (assets/tyler_levels/tyler_vh02_tree_sha.json
 * is the same room): entry (0,1,9), thermoses (0,1,4), printer front (4,1,-4.5), exit button
 * front (4,1,-2.6), west wall (-8,1,-2). TYLER slot = CAM, HANA slot = SOUND GUY. */
const TylerBeat g_tyler_vh02_beats[TYLER_VH02_MAX_BEATS] = {
    { TYLER_ACTOR_BOTH,   0.0f, 1.0f,  9.0f, 3000, 0,
      "L'ambiance sonore est etrange. Ce n'est pas du silence. C'est retenu. [EN: Room tone's weird. It's not quiet. It's held.]" },
    { TYLER_ACTOR_BOTH,   0.0f, 1.0f,  5.0f, 3000, 0,
      "Ne touchez pas aux thermos. Ne provoquez personne. Si quelqu'un arrive, laissez-le parler. [EN: Producer (radio): Don't touch the thermoses. Don't prompt anyone. Let them talk.]" },
    { TYLER_ACTOR_TYLER,  4.0f, 1.0f, -4.5f, 3000, 0,
      "Construction du depot. [EN: Repo construct. tree_sha: 9b847859e0399e1d29e96f080aae849aeecf3bb4]" },
    { TYLER_ACTOR_HANA,   3.0f, 1.0f, -3.0f, 2500, 0,
      "C'est quoi, un tree sha ? / Une empreinte pour tout un dossier. Meme sha, meme dossier. [EN: What's a tree sha? / A fingerprint for a whole folder. Same sha, same folder.]" },
    { TYLER_ACTOR_TYLER,  4.0f, 1.0f, -4.5f, 3000, 0,
      "Le documentaire n'est jamais appele un... [EN: The documentary is never called a -- (audio: 1 word removed in post)]" },
    { TYLER_ACTOR_TYLER,  4.0f, 1.0f, -4.5f, 3000, 0,
      "Elle trouverait ca approprie. Il a raison. [EN: She would find this appropriate. It's right. (sealed log, entry 23, not released)]" },
    { TYLER_ACTOR_NONE,   0.0f, 0.0f,  0.0f, 3000, 0,
      "(28,7 Hz sous le plancher. La porte tourne, de l'autre cote.) [EN: (28.7 Hz under the floor. The door turns from the other side.)]" },
    { TYLER_ACTOR_TYLER,  4.0f, 1.0f, -2.6f, 2500, 1,
      "RENCONTRE : l'equipe rencontre [EN: ENCOUNTER: THE CREW MEETS]" },
};
const int g_tyler_vh02_beat_count = TYLER_VH02_MAX_BEATS;

/* Voice lines: build-time generated (tyler_voice_tables.h); empty + silent without Piper. */
const TylerVoiceLine g_tyler_vh02_voice_lines[TYLER_VOICE_MAX_LINES] = TYLER_VH02_VOICE_LINES_INIT;
const int g_tyler_vh02_voice_line_count = TYLER_VH02_VOICE_LINE_COUNT;

/* From PARENA stdlib/tyler/voice_mod.prn: stretch a beat's hold to fit its spoken clip. */
int on_tyler_voice_stretched_hold_ms(int, int, int);
static unsigned int tyler_vh02_hold(int beat_index, const TylerBeat *beat) {
    return (unsigned int)on_tyler_voice_stretched_hold_ms((int)g_tyler_vh02_voice_clip_ms[beat_index], (int)beat->hold_ms, 500);
}

void tyler_vh02_start(TylerVh02State *st, int tyler_slot, int hana_slot, unsigned int now_ms) {
    st->active = 1;
    st->tyler_slot = tyler_slot;
    st->hana_slot = hana_slot;
    st->current_beat = 0;
    st->beat_started_ms = now_ms;
    st->beat_triggered = 0;
    st->done = 0;
    (void)now_ms;
}

#define TYLER_VH02_SCRIPTED_LOCK_MS 600000U

static void tyler_vh02_trigger_actor(const TylerVh02State *st, TylerActor actor,
                                      float x, float y, float z, unsigned int now_ms) {
    if (actor == TYLER_ACTOR_TYLER || actor == TYLER_ACTOR_BOTH) {
        story_ai_trigger_scripted(st->tyler_slot, x, y, z, TYLER_VH02_SCRIPTED_LOCK_MS, now_ms);
    }
    if (actor == TYLER_ACTOR_HANA || actor == TYLER_ACTOR_BOTH) {
        story_ai_trigger_scripted(st->hana_slot, x, y, z, TYLER_VH02_SCRIPTED_LOCK_MS, now_ms);
    }
}

void tyler_vh02_tick(TylerVh02State *st, unsigned int now_ms, int next_level_id, TylerExitFn exit_fn) {
    if (!st->active || st->done) return;
    if (st->current_beat >= g_tyler_vh02_beat_count) { st->done = 1; return; }

    const TylerBeat *beat = &g_tyler_vh02_beats[st->current_beat];

    if (!st->beat_triggered) {
        tyler_vh02_trigger_actor(st, beat->actor, beat->x, beat->y, beat->z, now_ms);
        reflux_host_dispatch(REFLUX_ACTION_TYLER_BEAT, st->current_beat, 0, 0);
        if (beat->fires_button) {
            reflux_host_dispatch(REFLUX_ACTION_BUTTON_PRESSED, 1, st->tyler_slot, 0);
        }
        st->beat_triggered = 1;
        st->beat_started_ms = now_ms;
    }

    if (now_ms - st->beat_started_ms < tyler_vh02_hold(st->current_beat, beat)) return;

    if (beat->fires_button) {
        if (exit_fn(next_level_id, 0, now_ms)) {
            st->done = 1;
        }
        return;
    }

    st->current_beat++;
    st->beat_triggered = 0;
}

void tyler_vh02_get_view(const TylerVh02State *st, unsigned int now_ms, TylerBeatView *out) {
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
