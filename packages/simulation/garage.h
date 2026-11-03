#ifndef SHANKPIT_GARAGE_H
#define SHANKPIT_GARAGE_H

/* garage.h -- what a garage bay will take, and what it pays for it.
 *
 * The founder's premise for the whole salvage line was a garage that accepts "vehicles of questionable
 * origin". Provenance now exists (LevelVehicleSpawn.origin -> BuggyState.origin); this is the half that
 * reads it. Two decisions, both of them RULES rather than gameplay code, so a modmaker can rewrite them
 * by editing PARENA/stdlib/shankpit/garage_rules.prn:
 *
 *   garage_accepts(origin, objective) -- will this bay take this car for this job?
 *   garage_pays(origin)               -- permille of the car's salvage value the bay will pay.
 *
 * Objectives are the same integers the mission layer would name (ANY / REBUILD / DISAPPEAR / FENCE).
 * Note what is deliberately NOT here: no vehicle-aware objective system exists in mission.h yet, so
 * this layer takes the objective as an argument and never invents one. The bay-side trigger (a car
 * standing on a garage PLATE) is the host's, because the plate already knows who is standing on it
 * (widget_runtime.h) and the garage is not a second sensor.
 *
 * Built-in defaults MUST equal the PARENA table; garage_rules_test asserts that.
 */
#include <string.h>

#define GARAGE_OBJECTIVE_ANY       0
#define GARAGE_OBJECTIVE_REBUILD   1
#define GARAGE_OBJECTIVE_DISAPPEAR 2
#define GARAGE_OBJECTIVE_FENCE     3
#define GARAGE_OBJECTIVE_COUNT     4

#define GARAGE_ORIGIN_CIVILIAN 0
#define GARAGE_ORIGIN_STOLEN   1
#define GARAGE_ORIGIN_MILITARY 2
#define GARAGE_ORIGIN_COUNT    3

typedef struct {
    int (*accepts)(int origin, int objective);
    int (*pays)(int origin);
} GarageRules;

static inline int garage_def_accepts(int origin, int objective) {
    if (objective == GARAGE_OBJECTIVE_ANY) return 1;
    if (objective == GARAGE_OBJECTIVE_REBUILD) return origin == GARAGE_ORIGIN_CIVILIAN ? 1 : 0;
    if (objective == GARAGE_OBJECTIVE_DISAPPEAR) return origin == GARAGE_ORIGIN_STOLEN ? 1 : 0;
    if (objective == GARAGE_OBJECTIVE_FENCE) return origin >= GARAGE_ORIGIN_STOLEN ? 1 : 0;
    return 0;
}

static inline int garage_def_pays(int origin) {
    if (origin == GARAGE_ORIGIN_CIVILIAN) return 1000;
    if (origin == GARAGE_ORIGIN_STOLEN) return 700;
    if (origin == GARAGE_ORIGIN_MILITARY) return 1300;
    return 0;   /* an origin nobody defined is worthless, never generous */
}

static const GarageRules g_garage_defaults = { garage_def_accepts, garage_def_pays };
static const GarageRules *g_garage_rules = &g_garage_defaults;

static inline void garage_set_rules(const GarageRules *r) { g_garage_rules = r ? r : &g_garage_defaults; }
static inline const GarageRules *garage_rules(void) { return g_garage_rules; }

static inline int garage_accepts_for(int origin, int objective) {
    return g_garage_rules->accepts(origin, objective);
}

static inline int garage_pays_for(int origin) { return g_garage_rules->pays(origin); }

/* What one car is worth to the bay: its remaining hull points times the origin's payout. Deliberately
 * hull and not the salvage table -- a garage buys the CAR, the salvage table is what comes off a wreck
 * that nobody bought. */
static inline int garage_value(int hull, int origin) {
    if (hull < 0) hull = 0;
    int p = garage_pays_for(origin);
    if (p <= 0) return 0;
    return (hull * p) / 1000;
}

#endif /* SHANKPIT_GARAGE_H */
