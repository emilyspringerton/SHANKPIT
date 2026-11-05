#ifndef VENDOR_SERVE_H
#define VENDOR_SERVE_H

/* vendor_serve.h -- standing at an ice cream truck's window until a cone is handed over.
 *
 * Founder (2026-11-05): "add ice cream trucks similar affordances to police vehicles ice tream truck song
 * affordances real sound audio engineering parena mods focuse", then the follow-up: let the truck sell.
 *
 * The song is the advertisement and the cone is the payoff: a jingle that never leads anywhere is set
 * dressing. So a vendor truck has a window, and the window has three rules, all of them in PARENA
 * (icecream_rules.prn): how close counts as AT the window (icecream-window-units), how long you have to
 * stand there (icecream-serve-ms), what a cone is worth (icecream-serve-heal) and how long until this truck
 * will serve you again (icecream-serve-cooldown-ms). This header is only the little state machine that turns
 * "a player is here, the truck is stopped, time passes" into one hand-over; it owns no numbers and no clock
 * of its own -- the caller passes dt_ms, so it is testable without a clock, a level or a device.
 *
 * The window is open only when the truck is STOPPED, which is the mirror image of the song's trigger: while
 * it drives you hear it, when it stops you can buy. Same rule, opposite halves of one affordance.
 *
 * WHO OWNS THE CONE: the authority on health, which is the server in a networked match (apps/server's own
 * server_vendor_tick runs this same state machine against the authoritative PlayerState and heals it, so
 * the cone reaches the owner through the ordinary snapshot and survives a reconnect) and the lobby itself
 * when there is no server (sock < 0). One rule, one implementation, one place that adds the health per
 * authority -- a client never heals itself out from under the server's snapshot.
 *
 *   make test-vendor-serve
 */

#include <string.h>

#include "../simulation/icecream_rules.h"

#define VENDOR_SERVE_NONE   (-1)

typedef struct {
    int truck_id;      /* which truck this buyer is at, VENDOR_SERVE_NONE when none */
    int held_ms;       /* how long at the window, capped at icecream_serve_ms */
    int cooldown_ms;   /* until this buyer may be served again */
    int served_count;  /* cones handed over, for a HUD or a test */
} VendorServe;

static inline void vendor_serve_init(VendorServe *v) {
    if (!v) return;
    memset(v, 0, sizeof(*v));
    v->truck_id = VENDOR_SERVE_NONE;
}

/* Where the hand-over has got to, 0..1000 -- what a prompt or a progress ring draws. 0 when nobody is at a
 * window, because a buyer who walked away has made no progress rather than kept it. */
static inline int vendor_serve_progress(const VendorServe *v) {
    if (!v || v->truck_id == VENDOR_SERVE_NONE) return 0;
    return icecream_serve_progress(v->held_ms);
}

/* One tick. truck_id is the nearest vendor (VENDOR_SERVE_NONE if the player is not near one), dist_units the
 * distance to it, speed_pm the truck's speed as permille of its top speed. Returns 1 on the tick a cone is
 * actually handed over and 0 otherwise -- so the caller has exactly one place to add the health and exactly
 * one chance to double-add it. */
static inline int vendor_serve_tick(VendorServe *v, int truck_id, int dist_units, int speed_pm, int dt_ms) {
    if (!v) return 0;
    if (dt_ms < 0) dt_ms = 0;

    if (v->cooldown_ms > 0) {
        v->cooldown_ms -= dt_ms;
        if (v->cooldown_ms < 0) v->cooldown_ms = 0;
    }

    /* Not at a window, or at one that is shut (too far, or the truck is rolling): no progress, and a buyer
     * who steps away has to start the hold over rather than bank it. */
    if (truck_id == VENDOR_SERVE_NONE || !icecream_serve_open(dist_units, speed_pm)) {
        v->truck_id = VENDOR_SERVE_NONE;
        v->held_ms = 0;
        return 0;
    }

    /* A different truck restarts the hold: a queue at a second window is not the first window's queue. */
    if (truck_id != v->truck_id) {
        v->truck_id = truck_id;
        v->held_ms = 0;
    }

    /* In cooldown: you can stand there, you just are not being served yet. */
    if (v->cooldown_ms > 0) {
        v->held_ms = 0;
        return 0;
    }

    v->held_ms += dt_ms;
    if (v->held_ms > icecream_serve_ms()) v->held_ms = icecream_serve_ms();

    if (icecream_serve_progress(v->held_ms) >= 1000) {
        v->held_ms = 0;
        v->cooldown_ms = icecream_serve_cooldown_ms();
        v->served_count++;
        return 1;
    }
    return 0;
}

/* Health a cone is worth, clamped to the engine's 0..100 player scale -- a rule can be edited to something
 * silly, and a vendor must not be the thing that puts a health bar over its own ceiling. */
static inline int vendor_serve_heal(int health, int max_health) {
    int heal = icecream_serve_heal();
    int cap = (max_health > 0) ? max_health : 100;
    if (heal < 0) heal = 0;
    if (health < 0) health = 0;
    int out = health + heal;
    if (out > cap) out = cap;
    return out;
}

#endif /* VENDOR_SERVE_H */
