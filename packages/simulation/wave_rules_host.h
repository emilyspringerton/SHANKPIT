/* wave_rules_host.h -- declarations for the PARENA-generated wave numbers (wave_rules.c).
 *
 * Same shape as empire_rules_host.h / zone_rules_host.h: this header is what a test or host includes to
 * call the generated copy. A binary that only needs the numbers includes wave_rules.h instead, which
 * carries matching defaults and links nothing. `make test-wave-spawner` is what keeps the two honest.
 */
#ifndef SHANKPIT_WAVE_RULES_HOST_H
#define SHANKPIT_WAVE_RULES_HOST_H

/* PARENA-generated (wave_rules.c) */
int wave_first_delay_ms(void);
int wave_interval_ms(void);
int wave_count(void);
int wave_size(void);
int wave_size_for(int wave);
int wave_live_cap(void);
int wave_spawn_radius_milli(void);
int wave_archetype(void);
int wave_interval_for(int wave);
int wave_should_spawn(int wave, int live, int now_ms, int due_ms);

#endif /* SHANKPIT_WAVE_RULES_HOST_H */
