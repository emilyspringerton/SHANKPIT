#ifndef CHAT_BRIDGE_RULES_H
#define CHAT_BRIDGE_RULES_H
/* chat_bridge_rules.h -- prototypes for chat_bridge_rules.c, generated from PARENA/stdlib/shankpit/chat_bridge_rules.prn
 * (scripts/gen_chat_bridge_rules.sh). The generated file is the source of truth; refresh this list with it. */
int src_mud(void);
int src_battlegrounds(void);
int src_gfd(void);
int src_minecraft(void);
int src_bigo(void);
int src_shankpit_phone(void);
int src_shankpit(void);
int src_twitch(void);
int src_discord(void);
int src_slack(void);
int src_last(void);
int br_source_valid(int);
int br_multi_instance(int);
int br_shows(int, int, int);
int br_should_post(int, int, int);
int br_tag(int);
int br_poll_ms(void);
int br_max_per_poll(void);
int br_name_max(void);
int br_body_max(void);
int br_post_queue(void);
int rx_bridge_out(void);
int rx_bridge_in(void);
#endif
