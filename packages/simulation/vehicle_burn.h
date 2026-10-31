#ifndef SHANKPIT_VEHICLE_BURN_H
#define SHANKPIT_VEHICLE_BURN_H

/* vehicle_burn.h -- a car's destruction ladder, and the fire that announces it.
 *
 * The numbers are PARENA's (PARENA/stdlib/shankpit/vehicle_burn_rules.prn, generated into
 * vehicle_burn_rules.c, installed by vehicle_burn_rules_host.h); this header is the per-tick state
 * machine, exactly the split drone_flight.h / drone_rules.prn already use. With nothing installed
 * the built-in defaults below are used, so a test that just includes this header needs no host.
 *
 * TWO HALVES, deliberately separated:
 *   the ladder   hull -> smoke (150) -> fire (75) -> detonation (300 burning ticks = 5 s). Every car
 *                that is about to blow up says so twice before it does: smoke you can see across
 *                the map, then flame. That warning window is the whole point -- a GTA-style
 *                explosion that arrives without one is just a random death.
 *   the look     fire-fx, the renderer's flame size, and smoke, its thickness. Both come from
 *                Bedrock Racers' br_fx.prn: fire-fx SWELLS FAST (250 permille of the remaining gap
 *                per tick, br_fx's fx-fire-rise 0.25) and DIES SLOWLY (60 permille, fx-fire-fall
 *                0.06), and a burning car's light flickers between 1.4 and 2.4 "a car's worth".
 *                Those are the curves BR lights its arena with, applied to a car instead of a ball
 *                -- so a burning buggy reads the way a burning car already reads in Bedrock Racers.
 *
 * Nothing here decides WHO died: the caller owns the hull and runs the blast, so this stays a pure
 * function of (hull, ticks burned). Two real callers now, deliberately different halves: the server's
 * buggy_tick_all (local_game.h) runs the ladder on the AUTHORITATIVE hull, so fire destroys the car
 * and explode_splash damages everyone nearby; the lobby's vehicle_burn_client draws from the
 * replicated health that the authority is taking down. Before the server ticked it, a car could only
 * ever burn on the screen of the person watching it.
 */

#include <math.h>

#define VEHICLE_BURN_MAX 32

typedef struct {
    int fire_threshold;         /* hull at or below which the car is on fire */
    int smoke_threshold;        /* hull at or below which it smokes */
    int rise_milli;             /* fire-fx: permille of the gap closed per tick while lighting */
    int fall_milli;             /* fire-fx: ... and while going out (BR: 60, i.e. slow) */
    int burn_hp_centi_per_tick; /* hundredths of a hull point per burning tick (25 = 0.25) */
    int detonate_ticks;         /* burning ticks before the blast */
    int light_min_milli;        /* br_fx's 1.4 .. 2.4 flicker, in milli */
    int light_max_milli;
    int drama_rise_milli;
    int drama_fall_milli;
    int blast_radius_milli;
    int blast_damage;
} VehicleBurnRules;

static const VehicleBurnRules g_vehicle_burn_defaults = {
    75, 150, 250, 60, 25, 300, 1400, 2400, 200, 25, 8000, 250,
};
static const VehicleBurnRules *g_vehicle_burn_rules = &g_vehicle_burn_defaults;

static inline void vehicle_burn_set_rules(const VehicleBurnRules *r) {
    g_vehicle_burn_rules = r ? r : &g_vehicle_burn_defaults;
}
static inline const VehicleBurnRules *vehicle_burn_rules(void) { return g_vehicle_burn_rules; }

/* the rocket car is a buggy with a tank on its back: same hull, an explosion risk, not a tank */
static inline int vehicle_burn_hull(int kind) { (void)kind; return 300; }

typedef struct {
    int active;
    int kind;
    int state;            /* 0 fine, 1 smoking, 2 on fire, 3 gone */
    int burning_ticks;
    int exploded;         /* one-shot: the blast has already been raised (and paid for) */
    float fire_fx;        /* 0..1 -- the renderer's flame size (BR br_fx c-fire-fx) */
    float smoke;          /* 0..1 -- the renderer's smoke thickness */
    unsigned int seed;    /* the car's own identity, so its flicker is its own */
    /* The hull THIS ladder has been draining, in float. A vehicle whose authoritative hull is an int
       (BuggyState.health) cannot take a 0.25/tick drain directly -- truncating each tick would drain
       four times too fast and passing &int to a float* is undefined behaviour. The host seeds this from
       the hull and writes whole points back; a divergence means something else changed the hull. */
    float hull;
} VehicleBurn;

static inline unsigned int vehicle_burn_hash(unsigned int x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}

static inline void vehicle_burn_reset(VehicleBurn *v, int kind, unsigned int seed) {
    if (!v) return;
    v->active = 1;
    v->kind = kind;
    v->state = 0;
    v->burning_ticks = 0;
    v->exploded = 0;
    v->fire_fx = 0.0f;
    v->smoke = 0.0f;
    v->hull = 0.0f;   /* the host seeds it from the real hull on the same call that resets the burn */
    v->seed = seed ? seed : 1u;
}

/* vehicle_burn_tick -- one tick of the ladder. *hp is the vehicle's hull, ticked in place.
 * Returns 1 on the single tick the car detonates (so the caller runs the blast once), else 0. */
static inline int vehicle_burn_tick(VehicleBurn *v, float *hp) {
    const VehicleBurnRules *r = g_vehicle_burn_rules;
    if (!v || !hp || !v->active) return 0;

    int dead = (*hp <= 0.0f);
    /* the fire is a function of the hull, NOT latched on burning_ticks: a car repaired above the
       fire line must go out (and its fuse start over). Latching on burning_ticks made a repaired
       car burn forever, which the test caught. */
    int onfire = !dead && (*hp <= (float)r->fire_threshold);
    int smoking = !dead && !onfire && (*hp <= (float)r->smoke_threshold);

    if (onfire) {
        v->burning_ticks++;
        *hp -= (float)r->burn_hp_centi_per_tick / 100.0f;
        if (v->burning_ticks >= r->detonate_ticks) *hp = 0.0f;   /* the burn-down reaches the tank */
        if (*hp <= 0.0f) { *hp = 0.0f; dead = 1; }
    } else if (!smoking) {
        v->burning_ticks = 0;   /* repaired above the fire line: the fuse starts over */
    }

    /* BR's fire curve: ease toward 1 fast while alight, toward 0 slowly once out */
    {
        float target = (dead || onfire) ? 1.0f : 0.0f;
        int rate = (target > v->fire_fx) ? r->rise_milli : r->fall_milli;
        v->fire_fx += (target - v->fire_fx) * ((float)rate / 1000.0f);
        if (v->fire_fx < 0.002f) v->fire_fx = 0.0f;
        if (v->fire_fx > 1.0f) v->fire_fx = 1.0f;
    }
    /* smoke is the first tell and the last thing to clear: rises faster than fire, clears slower */
    {
        float target = (dead || onfire) ? 1.0f : (smoking ? 0.42f : 0.0f);
        float rate = (target > v->smoke) ? 0.35f : 0.04f;
        v->smoke += (target - v->smoke) * rate;
        if (v->smoke < 0.002f) v->smoke = 0.0f;
    }

    v->state = dead ? 3 : (onfire ? 2 : (smoking ? 1 : 0));

    if (v->state == 3) {
        if (!v->exploded) { v->exploded = 1; v->burning_ticks = 0; return 1; }
        return 0;
    }
    return 0;
}

/* vehicle_burn_light -- the light a burning car throws, 0 when it is not burning. BR flickers a
 * burning car between light_min and light_max; the flicker is deterministic from the car's seed and
 * the tick, so two clients watching the same car see the same flame. */
static inline float vehicle_burn_light(const VehicleBurn *v, unsigned int tick) {
    const VehicleBurnRules *r = g_vehicle_burn_rules;
    if (!v || v->fire_fx <= 0.03f) return 0.0f;
    unsigned int h = vehicle_burn_hash((v->seed ? v->seed : 1u) + tick * 2654435761u);
    float f = (float)(h & 0xFFFFu) / 65535.0f;
    float lo = (float)r->light_min_milli / 1000.0f;
    float hi = (float)r->light_max_milli / 1000.0f;
    return v->fire_fx * (lo + (hi - lo) * f);
}

/* vehicle_burn_drama -- BR's drama scalar for a burning car (0..1): it jumps up when the car lights
 * and ebbs away slowly after, so the renderer can darken the ambient around a fire. */
static inline float vehicle_burn_drama(float drama, float fire_fx) {
    const VehicleBurnRules *r = g_vehicle_burn_rules;
    float target = fire_fx > 0.25f ? fire_fx : 0.0f;
    int rate = (target > drama) ? r->drama_rise_milli : r->drama_fall_milli;
    drama += (target - drama) * ((float)rate / 1000.0f);
    if (drama < 0.0f) drama = 0.0f;
    if (drama > 1.0f) drama = 1.0f;
    return drama;
}

/* vehicle_burn_impact_damage -- crash damage, mass x delta-v^2, staged to stay inside I32 exactly
 * like PARENA's vehicle-impact-damage. A 900 kg buggy at full speed into a wall is ~22 of 300 hull:
 * a crash is a warning, not a death. */
static inline int vehicle_burn_impact_damage(int mass_kg, int delta_v_milli) {
    int dv = delta_v_milli < 0 ? -delta_v_milli : delta_v_milli;
    int m = mass_kg < 0 ? 0 : mass_kg;
    if (dv <= 500) return 0;
    return ((m * dv) / 1000) * dv / 1000000;
}

#endif /* SHANKPIT_VEHICLE_BURN_H */
