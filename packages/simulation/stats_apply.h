/* stats_apply.h -- the stats bridge APPLIED: a GFD job and level become the numbers a live match uses.
 *
 * This is the piece docs/GFD_UNIFICATION.md's phase 4 named as missing: the table and the mapping existed,
 * were tested, and no call site read them -- a player was the same 100 HP and the same weapon ROF whatever
 * job a save said.
 *
 * Two decisions, both deliberate:
 *
 *   - NO FIELD ON PlayerState. A job is not a combat stat the wire carries; it is a character choice, and
 *     adding an int to PlayerState would be copied into every snapshot path and every memset-zeroed
 *     struct in the engine (0 is WAR, so every default-constructed player would silently become a
 *     Warrior). The roster lives here, keyed by the player's own slot id, and a slot with no job keeps
 *     today's exact numbers.
 *   - NO CALL INTO PHYSICS. The authority's spawn and fire paths live in packages/common/physics.h, which
 *     must not depend on packages/simulation. physics.h already exposes hooks for exactly this shape
 *     (g_phys_map_surface_hook, g_phys_map_hitscan_hook); this installs two more, so a build that never
 *     calls stats_apply_install() behaves exactly as it did before this file existed.
 *
 * What a job changes today: the health a respawn grants, and the cooldown a shot leaves behind (haste as a
 * multiplier on the weapon's own ROF, never a rewrite of it -- a knife stays a knife). What it does NOT
 * change, named rather than implied: movement speed and a damage scalar are exposed as accessors but no
 * call site multiplies by them yet, and nothing about crit/evade touches the hit path (that needs the
 * multi-target perception and the dice model GFD has and this engine does not).
 */
#ifndef SHANKPIT_STATS_APPLY_H
#define SHANKPIT_STATS_APPLY_H

#include <stddef.h>

#include "protocol.h"
#include "stats_bridge.h"
/* NOTE: this header is included AFTER packages/common/physics.h by every host, because it installs into
 * physics' own player-stats hooks (the same ordering phys_set_map_damage_hooks already requires). */

/* -1 = no job: the slot keeps the engine's own numbers. Level is clamped by the bridge, not here. */
typedef struct {
    int job;
    int level;
} StatsJob;

static StatsJob g_stats_jobs[MAX_CLIENTS];

static inline void stats_apply_clear(void) {
    for (int i = 0; i < MAX_CLIENTS; i++) { g_stats_jobs[i].job = -1; g_stats_jobs[i].level = 1; }
}

static inline void stats_apply_set(int slot, int job, int level) {
    if (slot < 0 || slot >= MAX_CLIENTS) return;
    g_stats_jobs[slot].job = (job >= 0 && job < SB_JOB_COUNT) ? job : -1;
    g_stats_jobs[slot].level = level < 1 ? 1 : level;
}

static inline void stats_apply_set_all(int job, int level) {
    for (int i = 0; i < MAX_CLIENTS; i++) stats_apply_set(i, job, level);
}

static inline const StatsJob *stats_apply_get(int slot) {
    if (slot < 0 || slot >= MAX_CLIENTS) return NULL;
    return &g_stats_jobs[slot];
}

/* The health a respawn grants this slot. No job -> 100, the engine's own number, unchanged. */
static inline int stats_apply_health_for(int slot) {
    const StatsJob *j = stats_apply_get(slot);
    if (!j || j->job < 0) return 100;
    return sb_clamp(10, 255, g_stats_bridge.health(j->job, j->level));
}

/* The cooldown a shot leaves behind: the weapon's own ROF shortened or lengthened by haste. A slot with no
 * job gets its ROF back untouched, and the bridge's own 1..999 clamp keeps a weapon from becoming free. */
static inline int stats_apply_rof_for(int slot, int rof) {
    const StatsJob *j = stats_apply_get(slot);
    if (!j || j->job < 0) return rof;
    return g_stats_bridge.cooldown(rof, g_stats_bridge.haste_permille(j->job, j->level));
}

/* Exposed for a host that wants to scale movement or damage. NOT wired to a call site yet (see above). */
static inline int stats_apply_move_permille(int slot) {
    const StatsJob *j = stats_apply_get(slot);
    if (!j || j->job < 0) return 1000;
    return g_stats_bridge.move_permille(j->job);
}

static inline int stats_apply_damage_permille(int slot) {
    const StatsJob *j = stats_apply_get(slot);
    if (!j || j->job < 0) return 1000;
    return g_stats_bridge.damage_permille(j->job, j->level);
}

/* The hooks physics.h calls. Keyed off the player's own id, which is the slot the server and the local sim
 * both use -- no extra plumbing, and a body whose id is out of range simply reads as "no job". */
static inline int stats_apply_health_hook(const PlayerState *p, int base_health) {
    if (!p) return base_health;
    const StatsJob *j = stats_apply_get(p->id);
    if (!j || j->job < 0) return base_health;
    return stats_apply_health_for(p->id);
}

static inline int stats_apply_rof_hook(const PlayerState *p, int rof) {
    if (!p) return rof;
    return stats_apply_rof_for(p->id, rof);
}

/* stats_apply_install -- hand the two hooks to physics. Called once at startup by a host that wants jobs to
 * matter (lobby, server); a host that does not call it is exactly the old engine. */
static inline void stats_apply_install(void) { phys_set_player_stats_hooks(stats_apply_health_hook, stats_apply_rof_hook); }
static inline void stats_apply_uninstall(void) { phys_set_player_stats_hooks(NULL, NULL); }

/* "NIN:75" / "NIN" / "nin" -> a job id (exact names only, like the table) and a level. Returns 0 on an
 * unknown job so a bad flag is reported rather than silently becoming a Warrior. */
static inline int stats_apply_parse(const char *text, int *out_job, int *out_level) {
    if (!text || !text[0] || !out_job || !out_level) return 0;
    char name[16];
    int n = 0, level = 1;
    const char *p = text;
    while (*p && *p != ':' && n < (int)sizeof name - 1) name[n++] = *p++;
    name[n] = '\0';
    if (*p == ':') {
        int lv = 0, digits = 0;
        p++;
        while (*p >= '0' && *p <= '9') { lv = lv * 10 + (*p - '0'); p++; digits++; }
        if (digits == 0 || *p != '\0') return 0;
        level = lv;
    } else if (*p != '\0') {
        return 0;
    }
    int job = stats_bridge_job_id(name);
    if (job < 0) return 0;
    *out_job = job;
    *out_level = level < 1 ? 1 : level;
    return 1;
}

#endif /* SHANKPIT_STATS_APPLY_H */
