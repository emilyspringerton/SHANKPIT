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

/* -- which truck sings WHICH TUNE, when it pulls over, and what the window sells --------------------
 * Two trucks in the same key are one truck as far as an ear is concerned, so each jingle is its own melody:
 * this is derived from the truck's own slot id (a fact every host already agrees on) rather than
 * hand-assigned. Jingle 0 IS the authored song, note for note; the rest are generated walks. */
int icecream_jingle_count(void);
/* a truck slot's jingle, always inside 0..icecream_jingle_count()-1, negative ids included */
int icecream_jingle_for_id(int id);
/* the pentatonic the generated motifs walk, in millihertz, 8 degrees */
int icecream_scale_hz_milli(int deg);
/* step i of jingle v as a scale degree (both ends of the loop are the tonic) */
int icecream_motif_degree(int i, int v);
/* step i of jingle v in millihertz; jingle 0 is the authored song exactly */
int icecream_note_hz_milli_for(int i, int v);

/* -- pulling over ----------------------------------------------------------------------------------
 * The window only opens while the truck is STOPPED, and an unmanned vendor has no driver to decide that,
 * so the stop is a rule on the town clock: every cycle it parks for the dwell, on its own side of the road
 * (keep-right already put it at the kerb). Clock-derived, so the server and every client agree with no
 * state to replicate and nothing to desync. */
int icecream_stop_cycle_ms(void);
int icecream_dwell_ms(void);
/* 1 while a vendor is parked (the first icecream_dwell_ms of every cycle) */
int icecream_parked(int now_ms);
/* one truck's SHIFT: the cycle cut into one shift per jingle (60000/4 = 15000 ms) */
int icecream_park_phase_ms(void);
/* 1 while the vendor in SLOT is parked. Slot 0 is icecream_parked exactly and each further slot parks one
 * shift later, so a district's trucks take turns pulling over instead of all parking together -- still pure
 * clock arithmetic, so the server and every client agree with nothing to replicate. */
int icecream_parked_for(int now_ms, int slot);

/* how close to a truck counts as at its window, in world units */
int icecream_window_units(void);
/* how long a buyer has to stand at the window */
int icecream_serve_ms(void);
/* what a cone is worth, on the engine's 0..100 player health scale */
int icecream_serve_heal(void);
/* how long until the same buyer may be served again */
int icecream_serve_cooldown_ms(void);
/* the hand-over's progress, 0..1000 */
int icecream_serve_progress(int held_ms);
/* is the window open: close enough AND stopped (1/0) */
int icecream_serve_open(int dist_units, int speed_pm);

/* -- the chime of a STOPPED truck ------------------------------------------------------------------
 * The jingle only plays while the truck moves, so a vendor is silent exactly when its window is open. A
 * stopped truck therefore chimes instead: two struck notes at its own short period, quieter than the song
 * and carrying a third as far -- a different sound, so the ear can tell "coming" from "here". */
int icecream_chime_carry_units(void);
int icecream_chime_note_count(void);
int icecream_chime_note_ms(void);
int icecream_chime_gap_ms(void);
int icecream_chime_period_ms(void);
/* how long the chime sounds, gap included */
int icecream_chime_ms(void);
/* note i of the chime in millihertz (C6, G5); 0 for a note that does not exist */
int icecream_chime_hz_milli(int i);
/* does this vendor chime right now: 1 while it is STOPPED (the mirror of icecream_trigger) */
int icecream_chime_trigger(int speed_pm);
/* chime loudness at a distance, 0..1000 (0 past icecream_chime_carry_units) */
int icecream_chime_gain_permille(int dist_units);

#endif /* ICECREAM_RULES_H */
