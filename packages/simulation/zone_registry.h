/* zone_registry.h -- shared, non-instanced zones with a grace period, headless.
 *
 * Founder: "for now the first person to enter it it generates and then everyone else enters the same one
 * ... there should be a grace period of 15 minutes for the zone existing for after you leave it so if it
 * gets totally farmed out or whatever it wont regenerate until you re enter it ... dungeons are not
 * instanced they are generated and then they exist until everyone leaves + 15 minutes".
 *
 * That is exactly a small table and three verbs, so that is what this is:
 *
 *   ENTER  -- first entrant CREATES the zone and gets the seed the host must generate from; every later
 *             entrant JOINS it and gets the same seed, so there is one layout and not one per player.
 *             Re-entering a zone whose countdown is running cancels the countdown and keeps the layout.
 *   LEAVE  -- drops the occupancy. At zero the zone starts counting down; it is NOT deleted.
 *   TICK   -- expires a zone only when it has been empty for the grace window. Expiry calls back with the
 *             id so the host can delete that zone's generated map file, then clears the slot, which is
 *             what makes the next entrant generate a NEW dungeon from a new seed.
 *
 * No I/O, no GL, no allocation: a slot holds an id, a seed and two integers. The layout itself lives in
 * the host (a CustomLevelData, or a file on disk), which is why this can be tested against a fake clock.
 *
 * Seeds come from a fixed LCG seeded by the registry, so a session is reproducible: the same start seed
 * and the same entry order produce the same dungeons. A host that wants a different world per launch
 * passes time(NULL).
 */
#ifndef SHANKPIT_ZONE_REGISTRY_H
#define SHANKPIT_ZONE_REGISTRY_H

#include <stdio.h>
#include <string.h>

#include "zone_rules.h"

#define ZONE_ID_MAX 32
#define ZONE_MAX_ZONES_DEFAULT 8   /* == zone-max-zones in zone_rules.prn */

enum { ZONE_STATE_UNINIT = 0, ZONE_STATE_ACTIVE = 1, ZONE_STATE_IDLE = 2 };

typedef struct {
    char id[ZONE_ID_MAX];
    int state;                 /* ZONE_STATE_* */
    int players;               /* occupancy right now */
    unsigned seed;             /* the layout seed, stable for the zone's whole life */
    int idle_since_ms;         /* set when occupancy hits zero */
    int generation;            /* how many times this zone has been generated (1 on first entry) */
} ZoneSlot;

typedef struct {
    ZoneSlot slot[ZONE_MAX_ZONES_DEFAULT];
    int count;
    unsigned next_seed;
} ZoneRegistry;

typedef void (*ZoneExpireFn)(void *user, const char *id);

static inline unsigned zone_registry_next_seed(ZoneRegistry *reg) {
    /* A plain 32-bit LCG. Not a good RNG and not meant to be -- it is a REPRODUCIBLE one, which is the
       property a shared zone needs: same registry seed plus same entry order equals same dungeons. */
    reg->next_seed = reg->next_seed * 1103515245u + 12345u;
    return (reg->next_seed >> 8) & 0x7FFFFFFFu;
}

static inline void zone_registry_init(ZoneRegistry *reg, unsigned seed) {
    if (!reg) return;
    memset(reg, 0, sizeof *reg);
    reg->next_seed = seed ? seed : 1u;
}

static inline int zone_registry_find(const ZoneRegistry *reg, const char *id) {
    if (!reg || !id || !id[0]) return -1;
    for (int i = 0; i < ZONE_MAX_ZONES_DEFAULT; i++)
        if (reg->slot[i].state != ZONE_STATE_UNINIT && strcmp(reg->slot[i].id, id) == 0) return i;
    return -1;
}

/* Returns 1 = created (generate from *out_seed), 0 = joined an existing zone (*out_seed is its seed),
 * -1 = the table is full (refuse the entry rather than silently sharing a stranger's dungeon). */
static inline int zone_registry_enter(ZoneRegistry *reg, const char *id, int now_ms, unsigned *out_seed) {
    int idx, free_slot = -1;
    (void)now_ms;   /* creation itself does not need the clock; the countdown starts on LEAVE */
    if (!reg || !id || !id[0]) return -1;
    idx = zone_registry_find(reg, id);
    if (idx >= 0) {
        ZoneSlot *s = &reg->slot[idx];
        s->players++;
        s->state = ZONE_STATE_ACTIVE;
        s->idle_since_ms = 0;   /* re-entry cancels the countdown */
        if (out_seed) *out_seed = s->seed;
        return 0;
    }
    for (int i = 0; i < ZONE_MAX_ZONES_DEFAULT; i++) {
        if (reg->slot[i].state == ZONE_STATE_UNINIT) { free_slot = i; break; }
    }
    if (free_slot < 0) return -1;
    {
        ZoneSlot *s = &reg->slot[free_slot];
        memset(s, 0, sizeof *s);
        snprintf(s->id, sizeof s->id, "%s", id);
        s->state = ZONE_STATE_ACTIVE;
        s->players = 1;
        s->seed = zone_registry_next_seed(reg);
        s->generation = 1;
        reg->count++;
        if (out_seed) *out_seed = s->seed;
    }
    return 1;
}

/* Returns 1 when this leave emptied the zone (so the caller can log/announce the countdown). */
static inline int zone_registry_leave(ZoneRegistry *reg, const char *id, int now_ms) {
    int idx = zone_registry_find(reg, id);
    if (idx < 0) return 0;
    {
        ZoneSlot *s = &reg->slot[idx];
        if (s->players > 0) s->players--;
        if (s->players == 0) {
            s->state = ZONE_STATE_IDLE;
            s->idle_since_ms = now_ms;
            return 1;
        }
    }
    return 0;
}

/* Expires idle zones past the grace window. Returns how many expired this tick. */
static inline int zone_registry_tick(ZoneRegistry *reg, int now_ms, ZoneExpireFn on_expire, void *user) {
    int expired = 0;
    if (!reg) return 0;
    for (int i = 0; i < ZONE_MAX_ZONES_DEFAULT; i++) {
        ZoneSlot *s = &reg->slot[i];
        if (s->state == ZONE_STATE_UNINIT) continue;
        if (s->players > 0) {
            /* An occupied zone is never expired, and an ACTIVE one that was just re-entered has already
               had its countdown cleared -- this is the invariant, not a special case. */
            s->state = ZONE_STATE_ACTIVE;
            s->idle_since_ms = 0;
            continue;
        }
        if (s->state == ZONE_STATE_ACTIVE) {
            s->state = ZONE_STATE_IDLE;
            s->idle_since_ms = now_ms;
        }
        {
            int idle_ms = g_zone_rules.elapsed_ms(now_ms, s->idle_since_ms);
            if (g_zone_rules.should_expire(s->players, idle_ms)) {
                if (on_expire) on_expire(user, s->idle_since_ms ? s->id : s->id);
                memset(s, 0, sizeof *s);   /* UNINIT: the next entrant generates a new seed */
                if (reg->count > 0) reg->count--;
                expired++;
            }
        }
    }
    return expired;
}

static inline int zone_registry_players(const ZoneRegistry *reg, const char *id) {
    int idx = zone_registry_find(reg, id);
    return idx < 0 ? -1 : reg->slot[idx].players;
}

static inline int zone_registry_state(const ZoneRegistry *reg, const char *id) {
    int idx = zone_registry_find(reg, id);
    return idx < 0 ? ZONE_STATE_UNINIT : reg->slot[idx].state;
}

static inline unsigned zone_registry_seed(const ZoneRegistry *reg, const char *id) {
    int idx = zone_registry_find(reg, id);
    return idx < 0 ? 0u : reg->slot[idx].seed;
}

#endif /* SHANKPIT_ZONE_REGISTRY_H */
