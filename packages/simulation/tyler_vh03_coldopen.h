/* tyler_vh03_coldopen.h -- VALHANNA VH03 "Jour Hors du Temps" MODE_TYLER scripted sequence. Mirrors
 * tyler_coldopen.h/tyler_e03_coldopen.h exactly; see those for the design rationale.
 *
 * ACTOR MAPPING DIFFERS: VH03 has no Tyler or Hana in it. The two scripted slots are the crew's
 * two moving bodies -- TYLER_ACTOR_TYLER = CAM, TYLER_ACTOR_HANA = THE SOUND GUY (who also carries the PA's
 * arithmetic and the Producer's radio line: one body, one voice, a named stopgap). The Duck's bedroom
 * and the Producer are subtitle/V.O. only. The slots still render with the level's two story-ally kits
 * (no crew models exist), a named stopgap. See docs2/specs/TYLER_VH03_DAY_OUT_OF_TIME.md.
 */
#ifndef SHANKPIT_TYLER_VH03_COLDOPEN_H
#define SHANKPIT_TYLER_VH03_COLDOPEN_H

#include "tyler_coldopen.h" /* reuses TylerActor, TylerBeat, TylerBeatView, TylerVoiceLine, TylerExitFn */

#define TYLER_VH03_MAX_BEATS 8

extern const TylerBeat g_tyler_vh03_beats[TYLER_VH03_MAX_BEATS];
extern const int g_tyler_vh03_beat_count;

extern const TylerVoiceLine g_tyler_vh03_voice_lines[TYLER_VOICE_MAX_LINES]; /* build-time generated; count 0 without Piper */
extern const int g_tyler_vh03_voice_line_count;

typedef TylerColdOpenState TylerVh03State; /* identical shape; reused as-is */

void tyler_vh03_start(TylerVh03State *st, int tyler_slot, int hana_slot, unsigned int now_ms);
void tyler_vh03_tick(TylerVh03State *st, unsigned int now_ms, int next_level_id, TylerExitFn exit_fn);
void tyler_vh03_get_view(const TylerVh03State *st, unsigned int now_ms, TylerBeatView *out);

#endif /* SHANKPIT_TYLER_VH03_COLDOPEN_H */
