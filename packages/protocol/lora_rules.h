/* Declarations for the GENERATED LoRa rules (PARENA/stdlib/shankpit/lora_rules.prn ->
   packages/protocol/lora_rules.c). The numbers live in PARENA; this is only the C surface. */
#ifndef SHANKPIT_PROTOCOL_LORA_RULES_H
#define SHANKPIT_PROTOCOL_LORA_RULES_H

int lora_tick_ms(int preset);
int lora_grid_w(int preset);
int lora_grid_h(int preset);
int lora_hit_permille(int preset);
int lora_damage(int preset);
int lora_xp_hit(int preset);
int lora_xp_move(int preset);
int lora_resend_ms(int preset);
int lora_max_sends(int preset);

#endif /* SHANKPIT_PROTOCOL_LORA_RULES_H */
