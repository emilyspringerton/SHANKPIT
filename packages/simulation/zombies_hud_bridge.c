#include "zombies_hud_bridge.h"

#include <string.h>

#include "../common/awareness_compass.h"

/* Prototypes for the PARENA-generated glue this bridge composes (reflux_mod.c,
 * zombies_awareness_rules.c, do-not-edit-by-hand). No shared .h between generated units in this
 * codebase, same convention world_alert_bridge.c's own identical declarations already
 * established. */
int reflux_log_size(void);
int reflux_action_type_at(int);
int reflux_action_a_at(int);
int reflux_action_b_at(int);
int zombies_awareness_should_ping(int);
int zombies_awareness_severity(int, int);

typedef struct {
    int have_alert;
    unsigned int alert_until_ms;
    int compass;
    int intensity;
    int action_type;
} ZombiesHudAlert;

static int g_last_index;
static int g_have_prev;
static ZombiesHudAlert g_alert;

void zombies_hud_bridge_reset(void) {
    g_last_index = 0;
    g_have_prev = 0;
    memset(&g_alert, 0, sizeof(g_alert));
}

/* Real, known limitation, same one world_alert_bridge.c's own poll_and_notify already names:
 * reflux_log_size/reflux_action_*_at give no total-dispatched counter, only a count of currently-
 * retained entries (REFLUX_LOG_CAPACITY=256, shared across every REFLUX dispatcher in the whole
 * game) -- g_last_index tracking here can drift if 256+ OTHER events land between two of this
 * bridge's own ticks. Calling this every real tick (as intended) makes that practically
 * unreachable for the handful of events this bridge itself cares about. */
void zombies_hud_bridge_tick(const ServerState *s, unsigned int now_ms) {
    if (!g_have_prev) {
        g_have_prev = 1;
        /* First observation establishes a baseline -- same "no backlog replay on first tick"
           precedent world_alert_bridge_tick's own have_prev branch already established. */
        g_last_index = reflux_log_size();
        return;
    }

    int n = reflux_log_size();
    const PlayerState *hero = &s->players[0]; /* player_id 0 is always the hero, witness_ai.c's
                                                  own established convention */
    int hero_live = hero->active && hero->state != STATE_DEAD;

    for (int i = g_last_index; i < n; i++) {
        int type = reflux_action_type_at(i);
        if (!zombies_awareness_should_ping(type)) continue;
        if (!hero_live) continue;

        int a = reflux_action_a_at(i);
        int b = reflux_action_b_at(i);
        if (a < 0 || a >= MAX_CLIENTS) continue; /* defensive: every event this bridge reacts to
                                                     documents a=player_id, but never trust a
                                                     cross-module payload blindly */
        const PlayerState *subject = &s->players[a];
        if (!subject->active || subject->scene_id != hero->scene_id) continue;

        float dx = subject->x - hero->x;
        float dz = subject->z - hero->z;
        float ndx, ndz;
        awareness_direction(dx, dz, &ndx, &ndz);

        g_alert.have_alert = 1;
        g_alert.alert_until_ms = now_ms + ZOMBIES_HUD_BRIDGE_DISPLAY_MS;
        g_alert.compass = awareness_compass(ndx, ndz);
        g_alert.intensity = awareness_intensity(zombies_awareness_severity(type, b));
        g_alert.action_type = type;
    }
    g_last_index = n;
}

int zombies_hud_bridge_current(unsigned int now_ms, int *compass_out, int *intensity_out, int *action_type_out) {
    if (!g_alert.have_alert) return 0;
    if ((int)(now_ms - g_alert.alert_until_ms) >= 0) return 0; /* window elapsed -- same
        wraparound-safe "now minus deadline" signed-subtraction idiom this file's zombie
        lock_until_ms checks already use, just inverted (elapsed, not still-pending) */
    *compass_out = g_alert.compass;
    *intensity_out = g_alert.intensity;
    *action_type_out = g_alert.action_type;
    return 1;
}
