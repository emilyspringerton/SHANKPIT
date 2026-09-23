#include "walkie_talkie.h"

/* Prototypes for the PARENA-generated decision functions (walkie_rules.c, do-not-edit-by-hand --
 * see that file's own header). No shared .h between generated units in this codebase, same real
 * precedent every other PARENA-generated module here already establishes. */
int walkie_team_channel_ext(int team_id);
int walkie_can_hear(int listener_team, int speaker_team, int distance_cm);

static WalkiePlayerState g_players[WALKIE_MAX_PLAYERS];

void walkie_talkie_reset(void) {
    for (int i = 0; i < WALKIE_MAX_PLAYERS; i++) {
        g_players[i].active = 0;
        g_players[i].team_id = WALKIE_NO_TEAM;
        g_players[i].ptt_active = 0;
    }
}

void walkie_talkie_set_team(int player_id, int team_id) {
    if (player_id < 0 || player_id >= WALKIE_MAX_PLAYERS) return;
    g_players[player_id].active = 1;
    g_players[player_id].team_id = team_id;
}

void walkie_talkie_set_ptt(int player_id, int active) {
    if (player_id < 0 || player_id >= WALKIE_MAX_PLAYERS) return;
    g_players[player_id].active = 1;
    g_players[player_id].ptt_active = active ? 1 : 0;
}

int walkie_talkie_ptt_active(int player_id) {
    if (player_id < 0 || player_id >= WALKIE_MAX_PLAYERS) return 0;
    return g_players[player_id].ptt_active;
}

int walkie_talkie_channel_for_player(int player_id) {
    if (player_id < 0 || player_id >= WALKIE_MAX_PLAYERS) return -1;
    return walkie_team_channel_ext(g_players[player_id].team_id);
}

int walkie_talkie_can_hear(int listener_id, int speaker_id, float distance_m) {
    if (listener_id < 0 || listener_id >= WALKIE_MAX_PLAYERS) return 0;
    if (speaker_id < 0 || speaker_id >= WALKIE_MAX_PLAYERS) return 0;
    int distance_cm = (int)(distance_m * 100.0f);
    if (distance_cm < 0) distance_cm = 0;
    return walkie_can_hear(g_players[listener_id].team_id, g_players[speaker_id].team_id, distance_cm);
}
