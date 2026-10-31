/* zone_rules_host.h -- installs PARENA's zone rules into zone_rules.h.
 * The rules live in PARENA/stdlib/shankpit/zone_rules.prn, generated into zone_rules.c (do not edit it;
 * `make regen-zone-rules`). A host that runs zones (apps/lobby, apps/server) includes this header, links
 * zone_rules.c, and calls zone_rules_install() once at startup. With nothing installed zone_rules.h's own
 * defaults are used, so tests keep their link lines. */
#ifndef SHANKPIT_ZONE_RULES_HOST_H
#define SHANKPIT_ZONE_RULES_HOST_H

#include "zone_rules.h"

/* PARENA-generated (zone_rules.c) */
int zone_grace_seconds(void);
int zone_max_zones(void);
int zone_room_count(int seed);
int zone_room_width(int seed, int room);
int zone_room_depth(int seed, int room);
int zone_wall_height(int seed);
int zone_wall_thickness(void);
int zone_door_width(void);
int zone_ring_distance(void);
int zone_ring_dir_x(int dir);
int zone_ring_dir_z(int dir);
int zone_spawn_count(int seed);
int zone_mob_count(int seed, int rooms);
int zone_material_count(void);
int zone_material_for(int seed, int idx);
int zone_difficulty(int seed);
int zone_should_expire(int players, int idle_ms);
int zone_elapsed_ms(int now_ms, int since_ms);

static const ZoneRules g_zone_rules_parena = {
    zone_grace_seconds, zone_max_zones, zone_room_count, zone_room_width, zone_room_depth,
    zone_wall_height, zone_wall_thickness, zone_door_width, zone_ring_distance, zone_ring_dir_x,
    zone_ring_dir_z, zone_spawn_count, zone_mob_count, zone_material_count, zone_material_for,
    zone_difficulty, zone_should_expire, zone_elapsed_ms
};

static inline void zone_rules_install(void) { zone_rules_set(&g_zone_rules_parena); }

#endif /* SHANKPIT_ZONE_RULES_HOST_H */
