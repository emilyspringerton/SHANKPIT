#ifndef WALKIE_TALKIE_H
#define WALKIE_TALKIE_H

/* walkie_talkie.h -- real host half of the walkie-talkie feature (BIG_O engine merge SECTION 536
 * follow-up, founder real-time, 2026-09-23: "add walkie talkie voice coms asterisk based (we have
 * a real asterisk server) parena powered"). Wraps PARENA-generated walkie_rules.c's own two pure
 * decision functions (channel assignment, hearing gate) with real, live per-player state.
 *
 * REAL, HONEST, NAMED GAP: this is channel/permission logic only, NOT audio transport. SHANKPIT's
 * C client (packages/audio/audio.c) has zero microphone capture, codec, or SIP/RTP client code --
 * building real voice (registering a PJSIP endpoint per player, encoding mic input, streaming
 * RTP into the ConfBridge extension walkie_talkie_channel_for_team resolves) is a genuinely large,
 * separate lift (a new SIP/RTP client dependency this build doesn't have), not attempted here.
 * The real, live Asterisk server this targets is CarePyre's own production PBX -- see
 * PARENA/ops/asterisk/pjsip_shankpit_walkie.conf/extensions_shankpit_walkie.conf for the real,
 * additive (never touches CarePyre's own config), NOT-YET-DEPLOYED dialplan this maps onto. */

#define WALKIE_MAX_PLAYERS 70
#define WALKIE_OVERHEAR_RADIUS_M 8.0f /* matches walkie_rules.prn's own 800cm distance-cm gate */
#define WALKIE_NO_TEAM (-1)

typedef struct {
    int active;
    int team_id;     /* WALKIE_NO_TEAM for spectators/unassigned */
    int ptt_active;  /* push-to-talk key currently held */
} WalkiePlayerState;

void walkie_talkie_reset(void);

/* Real, live per-player state -- team assignment (WALKIE_NO_TEAM allowed) and push-to-talk. */
void walkie_talkie_set_team(int player_id, int team_id);
void walkie_talkie_set_ptt(int player_id, int active);
int walkie_talkie_ptt_active(int player_id);

/* PARENA-computed (walkie_rules.c): the Asterisk ConfBridge extension this player's team
 * transmits/listens on (2999 for WALKIE_NO_TEAM, 3000+team_id otherwise). */
int walkie_talkie_channel_for_player(int player_id);

/* PARENA-computed (walkie_rules.c): would listener_id hear a live transmission from speaker_id
 * at the given real, positive distance in meters? Same team -> always. Different team -> only
 * within WALKIE_OVERHEAR_RADIUS_M (a real walkie-talkie speaker leaking sound into the world). */
int walkie_talkie_can_hear(int listener_id, int speaker_id, float distance_m);

#endif
