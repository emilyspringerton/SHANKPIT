/* Declarations for the generated Wow-wire rules (packages/protocol/wow_wire_rules.c).
 * The NUMBERS are PARENA/stdlib/shankpit/wow_wire.prn; this header only names them. */
#ifndef SHANKPIT_PROTOCOL_WOW_WIRE_RULES_H
#define SHANKPIT_PROTOCOL_WOW_WIRE_RULES_H

int wow_target_slots(int preset);
int wow_cast_queue(int preset);
int wow_macro_max_clauses(int preset);
int wow_ack_ms(int preset);
int wow_target_resend_ms(int preset);
int wow_retry_max(int preset);

int wow_entity_lo(int e);
int wow_entity_hi(int e);
int wow_entity_valid(int e);
int wow_def_bytes(int clauses, int argbytes);
int wow_seq_next(int seq);

#endif
