#ifndef ICECREAM_RULES_H
#define ICECREAM_RULES_H

/* icecream_rules.h -- PARENA's ice-cream-truck song rules, installed into the host that plays them.
 *
 * The numbers live in PARENA/stdlib/shankpit/icecream_rules.prn and are generated into
 * packages/simulation/icecream_rules.c (do not edit that file; `make regen-icecream-rules`).
 *
 * Why the rules are in PARENA and the DSP is not: the SONG is the affordance -- the point of a vendor truck
 * is that you hear it before you see it -- so the tune, its tempo, how a note decays and how far a jingle
 * carries are world facts two hosts have to agree on. Turning millihertz into samples is the audio device's
 * job (packages/audio/icecream_dsp.h), which is why this module has no state, no clock and no allocation:
 * every function is total in its arguments. */

#define ICECREAM_KIND_OFF     0
#define ICECREAM_KIND_PLAYING 1

int icecream_kind_off(void);
int icecream_kind_playing(void);
int icecream_note_count(void);
int icecream_tempo_ms(void);
int icecream_carry_units(void);

/* one step of the tune, in MILLIhertz (523000 = 523 Hz); 0 for an out-of-range step */
int icecream_note_hz_milli(int i);
/* how long one step lasts, in milliseconds (the last step is held twice as long) */
int icecream_note_ms(int i);
/* accent gain of one step, 0..1000 */
int icecream_note_gain_permille(int i);
/* one full loop of the tune, in milliseconds */
int icecream_loop_ms(void);
/* the music-box decay inside one note: 1000 on the strike, down to ~the quarter by the end */
int icecream_env_permille(int pos_ms, int note_ms);
/* loudness at a distance in world units, 0..1000 (0 past icecream_carry_units) */
int icecream_gain_permille(int dist_units);
/* pan relative to the listener, -1000..1000 (0 = dead ahead, + = right) */
int icecream_pan_permille(int bearing_deg, int yaw_deg);
/* is the song playing right now: 1 for a vendor truck that is actually moving */
int icecream_trigger(int ice_cream, int speed_pm);

#endif /* ICECREAM_RULES_H */
