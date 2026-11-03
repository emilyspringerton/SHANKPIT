/* vehicle_salvage_rules_host.h -- installs PARENA's component/salvage tables into vehicle_salvage.h.
 * The rules live in PARENA/stdlib/shankpit/vehicle_salvage_rules.prn, generated into
 * vehicle_salvage_rules.c (do not edit it; `make regen-vehicle-salvage-rules`). A host that parts a
 * car out (apps/server, apps/lobby) includes this header, links vehicle_salvage_rules.c, and calls
 * vehicle_salvage_rules_install() once at startup. With nothing installed, vehicle_salvage.h's own
 * built-in defaults are used, so a test that only includes that header is unchanged. */
#ifndef SHANKPIT_VEHICLE_SALVAGE_RULES_HOST_H
#define SHANKPIT_VEHICLE_SALVAGE_RULES_HOST_H

#include "vehicle_salvage.h"

/* PARENA-generated (vehicle_salvage_rules.c). Note the generated names are the .prn's own:
 * salvage_component_count, salvage_component_hp, ... (hyphens become underscores). */
int salvage_component_count(int kind);
int salvage_component_hp(int kind, int comp);
int salvage_component_material(int comp);
int salvage_component_offset_centi(int kind, int comp, int axis);
int salvage_handling_permille(int comp, int channel);
int salvage_tier(int kind, int comp, int roll);
int salvage_quantity(int hp_permille);
int salvage_hull(int kind);

static inline void vehicle_salvage_rules_install(void) {
    static VehicleSalvageRules s;
    s.component_count = salvage_component_count;
    s.component_hp = salvage_component_hp;
    s.component_material = salvage_component_material;
    s.component_offset_centi = salvage_component_offset_centi;
    s.handling_permille = salvage_handling_permille;
    s.tier = salvage_tier;
    s.quantity = salvage_quantity;
    vehicle_salvage_set_rules(&s);
}

#endif /* SHANKPIT_VEHICLE_SALVAGE_RULES_HOST_H */
