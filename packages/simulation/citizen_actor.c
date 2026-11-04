/* citizen_actor.c -- see citizen_actor.h. The only state is per-body timing; every decision and
 * every number comes from PARENA (citizen_actor_rules.prn). */

#include "citizen_actor.h"

#include <math.h>
#include <string.h>

#include "../reflux/reflux_runtime.h"

/* reflux_runtime.h deliberately exposes the host-side log, not the dispatcher symbol; every family
 * that dispatches (witness_ai.c, horse sync) declares this the same way. */
void reflux_dispatch(int action_type, int a, int b, int c);

static CitizenActor g_actors[MAX_CLIENTS];
static int g_actions_taken = 0;

void citizen_actor_reset(void) {
    memset(g_actors, 0, sizeof(g_actors));
    g_actions_taken = 0;
}

int citizen_actor_actions_taken(void) { return g_actions_taken; }

int citizen_actor_action(int player_id) {
    if (player_id < 0 || player_id >= MAX_CLIENTS) return CITACT_IDLE;
    return g_actors[player_id].active ? g_actors[player_id].action : CITACT_IDLE;
}

int citizen_actor_is_wielding(int player_id) {
    if (player_id < 0 || player_id >= MAX_CLIENTS) return 0;
    return g_actors[player_id].active ? g_actors[player_id].wielding : 0;
}

const char *citizen_actor_action_name(int action) {
    switch (action) {
        case CITACT_IDLE: return "idle";
        case CITACT_WALK: return "walk";
        case CITACT_RUN: return "run";
        case CITACT_JUMP: return "jump";
        case CITACT_CROUCH: return "crouch";
        case CITACT_SLIDE: return "slide";
        case CITACT_SLIDE_JUMP: return "slide-jump";
        case CITACT_SHOOT: return "shoot";
        case CITACT_RELOAD: return "reload";
        default: return "idle";
    }
}

/* Speed as a 0..1000 feature. A slider can exceed 1.0 units/tick downhill (PARENA's own cap is
 * 1.2), so it is clamped rather than allowed to skew the policy's scale. */
static int cit_speed_milli(const PlayerState *p) {
    float s = sqrtf(p->vx * p->vx + p->vz * p->vz) * 1000.0f;
    int v = (int)s;
    if (v < 0) v = 0;
    if (v > 1000) v = 1000;
    return v;
}

void citizen_actor_tick(int player_id, PlayerState *p, int threat, int dist_milli, unsigned int now_ms) {
    if (!p || player_id <= 0 || player_id >= MAX_CLIENTS) return;
    CitizenActor *a = &g_actors[player_id];

    if (!p->active || p->state != STATE_ALIVE || p->in_vehicle) {
        a->active = 0;
        return;
    }
    const CitizenActorRules *r = citizen_actor_rules();
    if (!a->active) {
        memset(a, 0, sizeof(*a));
        a->active = 1;
        a->action = CITACT_IDLE;
        a->last_action = CITACT_IDLE;
        a->weapon = WPN_AR;
    }

    int health_pm = p->health * 10;
    if (health_pm < 0) health_pm = 0;
    if (health_pm > 1000) health_pm = 1000;
    int speed = cit_speed_milli(p);
    int on_ground = p->on_ground ? 1 : 0;
    int ready = (on_ground && now_ms >= a->jump_ready_ms) ? 1 : 0;

    /* The two inputs the policy does not take raw: "how close" as 0..1000 inside the fire range,
     * and "how hurt" as its complement. Both computed here so the mod stays arithmetic-only. */
    int proximity = 0;
    if (threat) {
        int range = r->fire_range_milli();
        proximity = (range > 0) ? 1000 - (dist_milli * 1000) / range : 1000;
        if (proximity < 0) proximity = 0;
        if (proximity > 1000) proximity = 1000;
    }
    int hurt = 1000 - health_pm;

    int aggression = r->policy_aggression(proximity, hurt, threat, ready, speed, on_ground);
    int action = r->pick(dist_milli, threat, health_pm, speed, ready, aggression);

    /* A slide is a commitment: it holds for its own minimum duration or it is not a slide at all.
     * Anything else is re-decided every tick, which is what makes a chase read as alive. */
    if (a->action == CITACT_SLIDE && now_ms < a->action_until_ms) action = CITACT_SLIDE;

    a->last_action = a->action;
    if (action != a->action) a->boosted = 0;
    a->action = action;

    /* A jump the body cannot make is not a jump: fall through to running instead of standing
     * there with in_jump set and no impulse behind it. */
    if (action == CITACT_JUMP || action == CITACT_SLIDE_JUMP) {
        if (!ready) {
            a->action = CITACT_RUN;
            a->boosted = 1; /* nothing to apply */
        } else {
            unsigned int cooldown = (unsigned int)r->jump_cooldown_ms();
            a->jump_ready_ms = now_ms + cooldown;
            a->action_until_ms = now_ms + 120;
            if (action == CITACT_SLIDE_JUMP) {
                a->slide_jump_until_ms = now_ms + (unsigned int)r->slide_jump_window_ms();
                reflux_dispatch(REFLUX_ACTION_CITIZEN_SLIDE_JUMPED, player_id, dist_milli, speed);
            } else {
                reflux_dispatch(REFLUX_ACTION_CITIZEN_JUMPED, player_id, dist_milli, speed);
            }
            g_actions_taken++;
        }
    } else if (action == CITACT_SLIDE) {
        if (a->last_action != CITACT_SLIDE) {
            reflux_dispatch(REFLUX_ACTION_CITIZEN_SLID, player_id, speed, 0);
            g_actions_taken++;
        }
        a->action_until_ms = now_ms + (unsigned int)r->slide_min_ms();
        a->slide_jump_until_ms = a->action_until_ms + (unsigned int)r->slide_jump_window_ms();
    } else if (action == CITACT_SHOOT) {
        if (now_ms >= a->next_fire_ms) {
            a->next_fire_ms = now_ms + (unsigned int)r->fire_cooldown_ms();
            reflux_dispatch(REFLUX_ACTION_CITIZEN_FIRED, player_id, dist_milli, a->weapon);
            g_actions_taken++;
        }
    } else if (action == CITACT_RELOAD) {
        p->in_reload = 1; /* one tick of the shared reload path; update_weapons owns the timer */
    }

    /* Drawing a weapon is independent of the current action: a citizen that decided to walk still
     * holds the gun it drew. Inside the wield range it gets one, and ammo to go with it, once. */
    int want_wield = (threat && dist_milli <= r->wield_range_milli()) ? 1 : 0;
    if (want_wield && !a->wielding) {
        a->wielding = 1;
        a->weapon = WPN_AR;
        p->current_weapon = a->weapon;
        if (p->ammo[a->weapon] <= 0) p->ammo[a->weapon] = 30;
        reflux_dispatch(REFLUX_ACTION_CITIZEN_WIELDED, player_id, a->weapon, dist_milli);
        g_actions_taken++;
    } else if (!want_wield && a->wielding) {
        a->wielding = 0;
        a->weapon = WPN_AR;
        if (p->current_weapon == WPN_AR) p->current_weapon = WPN_KNIFE;
    }
}

void citizen_actor_apply(int player_id, PlayerState *p, unsigned int now_ms) {
    (void)now_ms;
    if (!p || player_id <= 0 || player_id >= MAX_CLIENTS) return;
    CitizenActor *a = &g_actors[player_id];
    if (!a->active || p->state != STATE_ALIVE) return;
    const CitizenActorRules *r = citizen_actor_rules();

    /* The actor owns these two inputs for its body, so they are cleared here and only an action that
       wants them sets them back. Without this a finished slide left the body permanently crouched and
       a single shoot decision never released the trigger -- found while writing the test, not by
       watching it, and exactly the kind of latch that would have made every citizen look broken. */
    p->crouching = 0;
    p->in_shoot = 0;

    switch (a->action) {
        case CITACT_JUMP:
            if (!a->boosted) {
                a->boosted = 1;
                p->y += 0.1f;
                p->vy += (float)r->jump_vy_milli() / 1000.0f;
            }
            break;
        case CITACT_SLIDE_JUMP:
            if (!a->boosted) {
                a->boosted = 1;
                p->y += 0.1f;
                p->vy += (float)(r->jump_vy_milli() + r->slide_jump_boost_milli()) / 1000.0f;
            }
            break;
        case CITACT_SLIDE:
            p->crouching = 1; /* the engine's own crouch-slide (SLIDE_FRICTION) does the rest */
            break;
        case CITACT_CROUCH:
            p->crouching = 1; /* accelerate() caps a crouching body at CROUCH_SPEED */
            break;
        case CITACT_SHOOT:
            p->in_shoot = 1; /* update_weapons owns rate of fire and ammo */
            break;
        default:
            break;
    }
}
