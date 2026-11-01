/* citizen_life.c -- see citizen_life.h. Deterministic per body: the same slot and seed is the same
 * person, which is what lets a spy be recognised again later in the session. */

#include "citizen_life.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t cl_hash(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static int cl_range(uint32_t *rng, int lo, int hi) {
    if (hi <= lo) return lo;
    *rng = cl_hash(*rng + 0x9e3779b9u);
    return lo + (int)(*rng % (uint32_t)(hi - lo + 1));
}

static const char *const CL_HANDLES[10] = {
    "kyoto_bus", "lamp_42", "old_man_yen", "night_shift", "port_authority_7",
    "tea_and_cigs", "the_quiet_one", "back_alley_kid", "grey_market", "third_party"
};

/* The path a persona chooses, from its own traits -- the founder's list of ways to play an MMO.
 * Ambition + bravery picks a fight, commerce picks a route, industry picks a bench, sociability
 * picks the group content, and anything left over is the AFK one. */
static int cl_path_for(const CitizenLife *l) {
    int duel = l->ambition * 2 + l->bravery * 2;
    int trade = l->commerce * 3 + l->sociability;
    int craft = l->industry * 3 + (100 - l->sociability);
    int endgame = l->ambition + l->sociability * 2 + l->bravery;
    int idle = l->sociability + (100 - l->ambition) + (100 - l->industry);
    int best = duel, path = CILIFE_DUEL;
    if (trade > best)  { best = trade;  path = CILIFE_TRADE; }
    if (craft > best)  { best = craft;  path = CILIFE_CRAFT; }
    if (endgame > best) { best = endgame; path = CILIFE_ENDGAME; }
    if (idle > best)   { path = CILIFE_IDLE; }
    return path;
}

static int cl_path_speed(int path) {
    switch (path) {
        case CILIFE_DUEL: return 500;
        case CILIFE_TRADE: return 350;
        case CILIFE_CRAFT: return 300;
        case CILIFE_ENDGAME: return 450;
        default: return 200;
    }
}

static int cl_path_range(int path) {
    switch (path) {
        case CILIFE_CRAFT: return 10;     /* works where it lives */
        case CILIFE_TRADE: return 28;     /* between posts */
        case CILIFE_DUEL: return 34;      /* looking for something to happen */
        case CILIFE_ENDGAME: return 55;   /* a long trip to the one place worth being */
        default: return 14;
    }
}

void citizen_life_roll(CitizenLife *out, uint32_t seed, int slot, int arrogance, int vigilance,
                       float home_x, float home_z) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->rng = cl_hash(seed ^ (uint32_t)(slot + 1) * 2654435761u);
    /* Traits: an independent pull each, nudged by the archetype's own alertness so The Men (high
     * vigilance) are not the same personality as a careless bystander. */
    int alert = vigilance / 4 + arrogance / 5;
    out->industry   = cl_range(&out->rng, 10, 95);
    out->commerce   = cl_range(&out->rng, 10, 95);
    out->ambition   = cl_range(&out->rng, 10, 90) + alert / 2;
    out->bravery    = cl_range(&out->rng, 5, 90) + alert;
    out->sociability = cl_range(&out->rng, 10, 95) - alert / 2;
    if (out->ambition > 100) out->ambition = 100;
    if (out->bravery > 100) out->bravery = 100;
    if (out->sociability < 0) out->sociability = 0;

    out->path = cl_path_for(out);
    /* A spy is somebody playing the game through a body: roughly one citizen in twelve. */
    out->spy = (int)(cl_hash(out->rng ^ 0x5bd1e995u) % 12u) == 0;
    snprintf(out->handle, sizeof(out->handle), "%s", out->spy
             ? CL_HANDLES[cl_hash(out->rng ^ 0x1234u) % 10u]
             : CL_HANDLES[cl_hash(out->rng ^ 0x7777u) % 10u]);
    out->home_x = home_x;
    out->home_z = home_z;
    out->goal = CILIFE_GOAL_IDLE;
    out->goal_x = home_x;
    out->goal_z = home_z;
    out->goal_until_ms = 0;   /* a fresh citizen picks its first goal on its first tick */
}

/* A new goal: where this persona is trying to be, and for how long. Spies watch more than they
 * live -- they pick a vantage point and hold it, which is what makes one worth noticing. */
static void cl_pick_goal(CitizenLife *l, unsigned int now_ms) {
    int range = cl_path_range(l->path);
    int angle = cl_range(&l->rng, 0, 359);
    float rad = (float)angle * 0.0174533f;
    float dist = (float)cl_range(&l->rng, range / 2 > 4 ? range / 2 : 4, range);
    int stand = (cl_range(&l->rng, 0, 99) < 20);

    if (l->spy && cl_range(&l->rng, 0, 99) < 55) {
        l->goal = CILIFE_GOAL_WATCH;
        dist = (float)range;             /* as far out as its own persona travels */
        if (dist < 12.0f) dist = 12.0f;
    } else {
        l->goal = stand ? CILIFE_GOAL_IDLE : CILIFE_GOAL_POST;
    }
    l->goal_x = l->home_x + sinf(rad) * dist;
    l->goal_z = l->home_z + cosf(rad) * dist;
    /* A watch holds longer than a post, and a craft post longer than a trade post. */
    unsigned int hold;
    switch (l->path) {
        case CILIFE_CRAFT:   hold = 14000u; break;
        case CILIFE_TRADE:   hold = 7000u;  break;
        case CILIFE_DUEL:    hold = 9000u;  break;
        case CILIFE_ENDGAME: hold = 20000u; break;
        default:            hold = 6000u;  break;
    }
    if (l->goal == CILIFE_GOAL_WATCH) hold += 8000u;
    if (l->goal == CILIFE_GOAL_IDLE) hold /= 2;
    l->goal_until_ms = now_ms + hold;
}

void citizen_life_tick(CitizenLife *l, PlayerState *p, unsigned int now_ms) {
    if (!l || !p || p->state == STATE_DEAD) return;
    if (l->goal_until_ms == 0 || (int)(now_ms - l->goal_until_ms) >= 0) cl_pick_goal(l, now_ms);

    if (l->goal == CILIFE_GOAL_IDLE) {
        p->in_fwd = 0.0f;
        return;
    }
    float dx = l->goal_x - p->x;
    float dz = l->goal_z - p->z;
    float d2 = dx * dx + dz * dz;
    if (d2 <= 4.0f) {           /* arrived: stand and be where it meant to be */
        p->in_fwd = 0.0f;
        return;
    }
    p->yaw = atan2f(dx, dz) * 57.29578f;
    p->in_fwd = (float)cl_path_speed(l->path) / 1000.0f;
}

int citizen_life_is_spy(const CitizenLife *l) { return l ? l->spy : 0; }

const char *citizen_life_handle(const CitizenLife *l) { return l ? l->handle : ""; }

const char *citizen_life_path_name(int path) {
    switch (path) {
        case CILIFE_CRAFT: return "CRAFT";
        case CILIFE_TRADE: return "TRADE";
        case CILIFE_DUEL: return "DUEL";
        case CILIFE_ENDGAME: return "ENDGAME";
        case CILIFE_IDLE: return "IDLE";
        default: return "IDLE";
    }
}
