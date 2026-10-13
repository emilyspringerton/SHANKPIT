/* tyler_vh04_coldopen.h -- VALHANNA VH04 "Les Chaises Pliantes" MODE_TYLER scripted sequence. Mirrors
 * tyler_coldopen.h/tyler_e03_coldopen.h exactly; see those for the design rationale.
 *
 * ACTOR MAPPING DIFFERS: VH04 has no Tyler or Hana in it. The two scripted slots are CAM
 * (TYLER_ACTOR_TYLER) and THE AUDITOR (TYLER_ACTOR_HANA). The PA, the sound guy, the Duck and the Producer
 * are subtitle/V.O. only. The slots still render with the level's two story-ally kits (no crew models
 * exist), a named stopgap. See docs2/specs/TYLER_VH04_LES_CHAISES_PLIANTES.md.
 */
#ifndef SHANKPIT_TYLER_VH04_COLDOPEN_H
#define SHANKPIT_TYLER_VH04_COLDOPEN_H

#include "tyler_coldopen.h" /* reuses TylerActor, TylerBeat, TylerBeatView, TylerVoiceLine, TylerExitFn */

#define TYLER_VH04_MAX_BEATS 8

extern const TylerBeat g_tyler_vh04_beats[TYLER_VH04_MAX_BEATS];
extern const int g_tyler_vh04_beat_count;

extern const TylerVoiceLine g_tyler_vh04_voice_lines[TYLER_VOICE_MAX_LINES]; /* build-time generated; count 0 without Piper */
extern const int g_tyler_vh04_voice_line_count;

typedef TylerColdOpenState TylerVh04State; /* identical shape; reused as-is */

void tyler_vh04_start(TylerVh04State *st, int tyler_slot, int hana_slot, unsigned int now_ms);
void tyler_vh04_tick(TylerVh04State *st, unsigned int now_ms, int next_level_id, TylerExitFn exit_fn);
void tyler_vh04_get_view(const TylerVh04State *st, unsigned int now_ms, TylerBeatView *out);

#endif /* SHANKPIT_TYLER_VH04_COLDOPEN_H */
