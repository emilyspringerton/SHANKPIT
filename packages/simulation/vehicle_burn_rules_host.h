/* vehicle_burn_rules_host.h -- installs PARENA's car destruction ladder into vehicle_burn.h.
 * The rules live in PARENA/stdlib/shankpit/vehicle_burn_rules.prn, generated into
 * vehicle_burn_rules.c (do not edit it; `make regen-vehicle-burn-rules`). A host that ticks cars
 * (apps/lobby, apps/server) includes this header, links vehicle_burn_rules.c, and calls
 * vehicle_burn_rules_install() once at startup. With nothing installed vehicle_burn.h's own
 * built-in defaults are used, so a test that only includes it is unchanged. */
#ifndef SHANKPIT_VEHICLE_BURN_RULES_HOST_H
#define SHANKPIT_VEHICLE_BURN_RULES_HOST_H

#include "vehicle_burn.h"

/* PARENA-generated (vehicle_burn_rules.c) */
int vehicle_hull(int kind);
int vehicle_fire_threshold(void);
int vehicle_smoke_threshold(void);
int vehicle_fire_fx_rise_milli(void);
int vehicle_fire_fx_fall_milli(void);
int vehicle_burn_hp_centi_per_tick(void);
int vehicle_burn_detonate_ticks(void);
int vehicle_fire_light_min_milli(void);
int vehicle_fire_light_max_milli(void);
int vehicle_drama_rise_milli(void);
int vehicle_drama_fall_milli(void);
int vehicle_blast_radius_milli(void);
int vehicle_blast_damage(void);
int vehicle_impact_damage(int mass_kg, int delta_v_milli);
int vehicle_burn_state(int hp, int burning_ticks);
int vehicle_smoke_permille(int hp, int burning_ticks);

static inline void vehicle_burn_rules_install(void) {
    static VehicleBurnRules s;
    s.fire_threshold = vehicle_fire_threshold();
    s.smoke_threshold = vehicle_smoke_threshold();
    s.rise_milli = vehicle_fire_fx_rise_milli();
    s.fall_milli = vehicle_fire_fx_fall_milli();
    s.burn_hp_centi_per_tick = vehicle_burn_hp_centi_per_tick();
    s.detonate_ticks = vehicle_burn_detonate_ticks();
    s.light_min_milli = vehicle_fire_light_min_milli();
    s.light_max_milli = vehicle_fire_light_max_milli();
    s.drama_rise_milli = vehicle_drama_rise_milli();
    s.drama_fall_milli = vehicle_drama_fall_milli();
    s.blast_radius_milli = vehicle_blast_radius_milli();
    s.blast_damage = vehicle_blast_damage();
    vehicle_burn_set_rules(&s);
}

#endif /* SHANKPIT_VEHICLE_BURN_RULES_HOST_H */
