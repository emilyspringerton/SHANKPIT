#ifndef SHIELD_REGEN_RULES_H
#define SHIELD_REGEN_RULES_H

/* shield_regen_rules.h -- C declarations for packages/simulation/shield_regen_rules.c.
 *
 * The .c is GENERATED (from ../PARENA/stdlib/shankpit/shield_regen_rules.prn); this header is the
 * hand-written contract so a host can call the rule without including the whole generated file.
 *   make regen-shield-regen-rules   regenerate the .c from a sibling ../PARENA checkout
 *   make check-shield-regen-rules   fail if the checked-in .c is stale vs the .prn
 */

int shield_max(void);
int shield_regen_delay_ms(void);
int shield_regen_interval_ms(void);
int shield_regen_ramp_cap(void);

int sh_clamp(int lo, int hi, int x);
int sh_ceil_div(int n, int d);
int sh_regen_delay_ticks(int tick_ms);
int sh_regen_interval_ticks(int tick_ms);
int sh_regen_tick_index(int since_ms);
int sh_regen_ticks_landed(int since_ms);
int sh_regen_tick_amount(int index);
int sh_regen_total(int since_ms);
int sh_shield_at(int shield, int since_ms);
int sh_regen_in_ms(int since_ms);
/* PARENA lowers the trailing '?' to '_': sh-regen-waiting? -> sh_regen_waiting_ */
int sh_regen_waiting_(int since_ms);

#endif /* SHIELD_REGEN_RULES_H */
