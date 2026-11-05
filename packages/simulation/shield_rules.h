#ifndef SHIELD_RULES_H
#define SHIELD_RULES_H

/* shield_rules.h -- PARENA's shield-recovery rules, installed into the hosts that run combat.
 *
 * The numbers live in packages/sim/shield_rules.prn and are generated into
 * packages/simulation/shield_rules.c (do not edit that file; `make regen-shield-rules`).
 *
 * Why the rules are in PARENA and the tick is not: how long a broken shield stays down, how often a
 * tick lands and how much each tick is worth are world facts every host has to agree on (the offline
 * match, the dedicated server, a later browser host). When a body's clock advances, what a HUD draws,
 * and which fields hold the two numbers are the host's job, which is packages/simulation/shield_regen.h.
 *
 * Every function here is total over int and stores nothing: a replay and a live tick that pass the
 * same elapsed time get the same shield. */

/* the bar, and the curve in milliseconds: 4 s of nothing, then a tick every 1 s */
int shield_max(void);
int shield_regen_delay_ms(void);
int shield_regen_interval_ms(void);
int shield_regen_ramp_cap(void);

/* the same two numbers as TICK COUNTS, rounded UP -- a host that schedules rather than accumulating
 * ms can never fire a tick early (at 16 ms: 250 and 63) */
int sh_regen_delay_ticks(int tick_ms);
int sh_regen_interval_ticks(int tick_ms);

/* how many ticks have landed `since_ms` after the hit: 0 during the four seconds, 1 at exactly 4000 */
int sh_regen_tick_index(int since_ms);
int sh_regen_ticks_landed(int since_ms);

/* one tick's worth by index: 1, 2, 3, ... capped at shield_regen_ramp_cap */
int sh_regen_tick_amount(int index);

/* every landed tick's points together (closed form; agrees with sh_regen_tick_amount by test) */
int sh_regen_total(int since_ms);

/* the shield held `since_ms` after the hit, given the shield held THEN; clamped to 0..shield_max */
int sh_shield_at(int shield, int since_ms);

/* the 4-second counter a HUD draws (0 once regen has begun) and the vulnerable-window test */
int sh_regen_in_ms(int since_ms);
int sh_regen_waiting_(int since_ms);

/* the clamp and the rounding helper, exported because the emitter makes every defn global */
int sh_clamp(int lo, int hi, int x);
int sh_ceil_div(int n, int d);

#endif /* SHIELD_RULES_H */
