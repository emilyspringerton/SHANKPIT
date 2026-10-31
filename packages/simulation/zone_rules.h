/* zone_rules.h -- shared zone lifecycle + procedural layout rules, with built-in defaults.
 *
 * The numbers live in PARENA (PARENA/stdlib/shankpit/zone_rules.prn, generated into zone_rules.c by
 * scripts/gen_zone_rules.sh / `make regen-zone-rules`, installed through zone_rules_host.h). This header
 * carries matching C defaults so packages/simulation/proc_dungeon.h and zone_registry.h compile and run
 * in any binary or test without linking a generated file -- the same arrangement widget_rules.h and
 * slide_rules.h use, and `make test-proc-dungeon` asserts the two copies agree sample for sample.
 *
 * Two separate concerns share this table on purpose, because both are "the zone's own numbers":
 *   - LIFECYCLE: grace seconds, max live zones, the expiry predicate.
 *   - LAYOUT: rooms, sizes, wall height, doors, spawners, material kind, difficulty tier.
 */
#ifndef SHANKPIT_ZONE_RULES_H
#define SHANKPIT_ZONE_RULES_H

typedef struct {
    int (*grace_seconds)(void);
    int (*max_zones)(void);
    int (*room_count)(int seed);
    int (*room_width)(int seed, int room);
    int (*room_depth)(int seed, int room);
    int (*wall_height)(int seed);
    int (*wall_thickness)(void);
    int (*door_width)(void);
    int (*ring_distance)(void);
    int (*ring_dir_x)(int dir);
    int (*ring_dir_z)(int dir);
    int (*spawn_count)(int seed);
    int (*mob_count)(int seed, int rooms);
    int (*material_count)(void);
    int (*material_for)(int seed, int idx);
    int (*difficulty)(int seed);
    int (*should_expire)(int players, int idle_ms);
    int (*elapsed_ms)(int now_ms, int since_ms);
} ZoneRules;

static inline int zr_clamp(int lo, int hi, int x) { return x < lo ? lo : (x > hi ? hi : x); }

static inline int zr_c_grace_seconds(void) { return 900; }
static inline int zr_c_max_zones(void) { return 8; }
static inline int zr_c_room_count(int seed) { return 3 + zr_clamp(0, 2, seed % 3); }
static inline int zr_c_room_width(int seed, int room) { return 24 + 8 * zr_clamp(0, 2, (seed + room * 37) % 3); }
static inline int zr_c_room_depth(int seed, int room) { return 24 + 8 * zr_clamp(0, 2, (seed + room * 53) % 3); }
static inline int zr_c_wall_height(int seed) { return 18 + 6 * zr_clamp(0, 2, seed % 3); }
static inline int zr_c_wall_thickness(void) { return 2; }
static inline int zr_c_door_width(void) { return 8; }
static inline int zr_c_ring_distance(void) { return 52; }
static inline int zr_c_ring_dir_x(int dir) {
    dir = zr_clamp(0, 3, dir);
    return dir == 0 ? 1 : (dir == 2 ? -1 : 0);
}
static inline int zr_c_ring_dir_z(int dir) {
    dir = zr_clamp(0, 3, dir);
    return dir == 1 ? 1 : (dir == 3 ? -1 : 0);
}
static inline int zr_c_spawn_count(int seed) { return 3 + zr_clamp(0, 2, seed % 3); }
static inline int zr_c_mob_count(int seed, int rooms) {
    return 2 * zr_clamp(1, 8, rooms) + seed % 5;
}
static inline int zr_c_material_count(void) { return 3; }
static inline int zr_c_material_for(int seed, int idx) { return (seed + idx * 7) % 3; }
static inline int zr_c_difficulty(int seed) { return 1 + zr_clamp(0, 4, seed % 5); }
static inline int zr_c_should_expire(int players, int idle_ms) {
    if (zr_clamp(0, 1000, players) > 0) return 0;
    return zr_clamp(0, 2000000000, idle_ms) >= zr_c_grace_seconds() * 1000 ? 1 : 0;
}
static inline int zr_c_elapsed_ms(int now_ms, int since_ms) { return now_ms > since_ms ? now_ms - since_ms : 0; }

static ZoneRules g_zone_rules = {
    zr_c_grace_seconds, zr_c_max_zones, zr_c_room_count, zr_c_room_width, zr_c_room_depth,
    zr_c_wall_height, zr_c_wall_thickness, zr_c_door_width, zr_c_ring_distance, zr_c_ring_dir_x,
    zr_c_ring_dir_z, zr_c_spawn_count, zr_c_mob_count, zr_c_material_count, zr_c_material_for,
    zr_c_difficulty, zr_c_should_expire, zr_c_elapsed_ms
};

static inline void zone_rules_set(const ZoneRules *r) { if (r) g_zone_rules = *r; }

#endif /* SHANKPIT_ZONE_RULES_H */
