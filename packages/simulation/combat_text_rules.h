#ifndef COMBAT_TEXT_RULES_H
#define COMBAT_TEXT_RULES_H

/* combat_text_rules.h -- PARENA rules for floating combat text, generated from
 * PARENA/stdlib/shankpit/combat_text_rules.prn. */

int combat_text_max_age_ms(void);
int combat_text_float_speed(void);
int combat_text_max_distance(void);
int combat_text_scale(void);
int combat_text_fade_start(void);
int ct_clamp(int lo, int hi, int x);
int ct_format_damage(int damage);
int ct_should_show(int age_ms, int dist_units);

#endif /* COMBAT_TEXT_RULES_H */
