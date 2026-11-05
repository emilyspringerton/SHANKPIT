#ifndef SIREN_RULES_H
#define SIREN_RULES_H

/* siren_rules.h -- PARENA's emergency-siren rules, installed into the host that hears them.
 *
 * The numbers live in PARENA/stdlib/shankpit/siren_rules.prn and are generated into
 * packages/simulation/siren_rules.c (do not edit that file; `make regen-siren-rules`).
 *
 * Why the rules are in PARENA and the DSP is not: what an EMS two-tone IS, how wide a police wail
 * sweeps, how far a siren carries and when one goes off are world facts two hosts have to agree on.
 * Turning millihertz into samples is the audio device's job (packages/audio/audio.c), which is why this
 * module has no state, no clock and no allocation: every function is total in its arguments. */

/* siren kinds, in the numbering the level/vehicle code and the wire use */
#define SIREN_KIND_SILENT 0
#define SIREN_KIND_EMS    1
#define SIREN_KIND_POLICE 2

int siren_kind_silent(void);
int siren_kind_ems(void);
int siren_kind_police(void);
int siren_ems_low_milli(void);
int siren_ems_high_milli(void);
int siren_police_low_milli(void);
int siren_police_high_milli(void);

/* instantaneous frequency of one siren kind at a moment, in MILLIhertz (690000 = 690 Hz) */
int siren_hz_milli(int kind, int now_ms);
/* loudness at a distance (world units) for a car moving at speed_pm (0..1000) */
int siren_gain_permille(int dist_units, int speed_pm);
/* pan relative to the listener, -1000..1000 (0 = dead ahead, + = right) */
int siren_pan_permille(int bearing_deg, int yaw_deg);
/* which siren (a SIREN_KIND_*) this car should be sounding right now, 0 for none */
int siren_trigger(int police, int boost, int lift, int speed_pm);

#endif /* SIREN_RULES_H */
