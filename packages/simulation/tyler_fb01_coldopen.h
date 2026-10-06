/* tyler_fb01_coldopen.h -- flashback "The First Time They Play House" MODE_TYLER scripted
 * sequence. Mirrors tyler_coldopen.h/tyler_e03_coldopen.h exactly; see those files for the full
 * design rationale. No rendered VO (Piper/TYLER repo not present in this sandbox) -- see
 * docs2/specs/TYLER_FLASHBACK_PLAY_HOUSE.md for the full script + named gaps.
 */
#ifndef SHANKPIT_TYLER_FB01_COLDOPEN_H
#define SHANKPIT_TYLER_FB01_COLDOPEN_H

#include "tyler_coldopen.h" /* reuses TylerActor, TylerBeat, TylerBeatView, TylerVoiceLine, TylerExitFn */

#define TYLER_FB01_MAX_BEATS 8

extern const TylerBeat g_tyler_fb01_beats[TYLER_FB01_MAX_BEATS];
extern const int g_tyler_fb01_beat_count;

extern const TylerVoiceLine g_tyler_fb01_voice_lines[1]; /* unused; count is 0 */
extern const int g_tyler_fb01_voice_line_count;

typedef TylerColdOpenState TylerFb01State; /* identical shape; reused as-is */

void tyler_fb01_start(TylerFb01State *st, int tyler_slot, int hana_slot, unsigned int now_ms);
void tyler_fb01_tick(TylerFb01State *st, unsigned int now_ms, int next_level_id, TylerExitFn exit_fn);
void tyler_fb01_get_view(const TylerFb01State *st, unsigned int now_ms, TylerBeatView *out);

#endif /* SHANKPIT_TYLER_FB01_COLDOPEN_H */
