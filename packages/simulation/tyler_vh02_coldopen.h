/* tyler_vh02_coldopen.h -- VALHANNA VH02 "Tree SHA" MODE_TYLER scripted sequence. Mirrors
 * tyler_coldopen.h/tyler_e03_coldopen.h exactly; see those for the design rationale.
 *
 * ACTOR MAPPING DIFFERS: VH02 has no Tyler or Hana in it. The two scripted slots are the crew's
 * two moving bodies -- TYLER_ACTOR_TYLER = CAM, TYLER_ACTOR_HANA = THE SOUND GUY. The PA and the
 * Producer are subtitle/V.O. only. The slots still render with the level's two story-ally kits
 * (no crew models exist), a named stopgap. No rendered VO (g_tyler_vh02_voice_line_count = 0).
 * See docs2/specs/TYLER_VH02_TREE_SHA.md.
 */
#ifndef SHANKPIT_TYLER_VH02_COLDOPEN_H
#define SHANKPIT_TYLER_VH02_COLDOPEN_H

#include "tyler_coldopen.h" /* reuses TylerActor, TylerBeat, TylerBeatView, TylerVoiceLine, TylerExitFn */

#define TYLER_VH02_MAX_BEATS 8

extern const TylerBeat g_tyler_vh02_beats[TYLER_VH02_MAX_BEATS];
extern const int g_tyler_vh02_beat_count;

extern const TylerVoiceLine g_tyler_vh02_voice_lines[TYLER_VOICE_MAX_LINES]; /* build-time generated; count 0 without Piper */
extern const int g_tyler_vh02_voice_line_count;

typedef TylerColdOpenState TylerVh02State; /* identical shape; reused as-is */

void tyler_vh02_start(TylerVh02State *st, int tyler_slot, int hana_slot, unsigned int now_ms);
void tyler_vh02_tick(TylerVh02State *st, unsigned int now_ms, int next_level_id, TylerExitFn exit_fn);
void tyler_vh02_get_view(const TylerVh02State *st, unsigned int now_ms, TylerBeatView *out);

#endif /* SHANKPIT_TYLER_VH02_COLDOPEN_H */
