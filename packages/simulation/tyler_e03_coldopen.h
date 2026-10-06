/* tyler_e03_coldopen.h -- Episode 3 ("Don't Check" / "Ne vérifie pas") MODE_TYLER scripted
 * sequence. Mirrors tyler_coldopen.h's design exactly (see that file for full rationale); this
 * header only documents what differs.
 *
 * DIFFERENCE FROM VH01: voice lines table is EMPTY (g_tyler_e03_voice_line_count = 0). Piper and
 * the TYLER repo (tts/render_vh01.py, the beats TSV, the .onnx voice models) are not present in
 * this sandbox, so no audio could be rendered this session. Subtitles still work -- they live in
 * the TylerBeat table itself, independent of the voice lines table, same separation VH01 already
 * has. See docs2/specs/TYLER_EPISODE_3_DONT_CHECK.md for the full script + named gaps.
 */
#ifndef SHANKPIT_TYLER_E03_COLDOPEN_H
#define SHANKPIT_TYLER_E03_COLDOPEN_H

#include "tyler_coldopen.h" /* reuses TylerActor, TylerBeat, TylerBeatView, TylerVoiceLine, TylerExitFn */

#define TYLER_E03_MAX_BEATS 8

extern const TylerBeat g_tyler_e03_beats[TYLER_E03_MAX_BEATS];
extern const int g_tyler_e03_beat_count;

extern const TylerVoiceLine g_tyler_e03_voice_lines[1]; /* unused; count is 0 */
extern const int g_tyler_e03_voice_line_count;

typedef TylerColdOpenState TylerE03State; /* identical shape; reused as-is */

void tyler_e03_start(TylerE03State *st, int tyler_slot, int hana_slot, unsigned int now_ms);
void tyler_e03_tick(TylerE03State *st, unsigned int now_ms, int next_level_id, TylerExitFn exit_fn);
void tyler_e03_get_view(const TylerE03State *st, unsigned int now_ms, TylerBeatView *out);

#endif /* SHANKPIT_TYLER_E03_COLDOPEN_H */
