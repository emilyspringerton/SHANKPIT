/* bioslurry.c -- see bioslurry.h. */

#include "bioslurry.h"
#include "zombie_taxonomy.h"   /* the body a canister came off: class, level, DNA grade */

#include <math.h>
#include <string.h>

static BioslurryDrop g_drops[BIOSLURRY_MAX_DROPS];
static BioslurryStrand g_dna[BIOSLURRY_MAX_DNA];
static int g_dna_count;
static int g_carried[MAX_CLIENTS];
/* The LAST body each player actually picked up. A carry is a count (the canisters themselves are
 * consumed on pickup), so a delivery reads the source it can honestly stand behind: the most recent
 * thing that player collected. Not a history, and named as such -- a player who grabs a shambler
 * after an Alpha reads the shambler, which is the behaviour that makes the order of pickups matter. */
static int g_last_quality[MAX_CLIENTS];
static int g_last_trait[MAX_CLIENTS];
static int g_last_cls[MAX_CLIENTS];
static int g_last_level[MAX_CLIENTS];
static int g_delivered_total;
static int g_harvested_total;

void bioslurry_reset(void) {
    memset(g_drops, 0, sizeof(g_drops));
    memset(g_dna, 0, sizeof(g_dna));
    memset(g_carried, 0, sizeof(g_carried));
    memset(g_last_quality, 0, sizeof(g_last_quality));
    memset(g_last_trait, 0, sizeof(g_last_trait));
    memset(g_last_cls, 0, sizeof(g_last_cls));
    memset(g_last_level, 0, sizeof(g_last_level));
    g_dna_count = 0;
    g_delivered_total = 0;
    g_harvested_total = 0;
    for (int i = 0; i < BIOSLURRY_MAX_DROPS; i++) g_drops[i].collected_by = -1;
}

int bioslurry_drop(int scene_id, float x, float y, float z, int cls, int level, unsigned int now_ms) {
    for (int i = 0; i < BIOSLURRY_MAX_DROPS; i++) {
        BioslurryDrop *d = &g_drops[i];
        if (d->active) continue;
        ZombieProfile p;
        zombie_taxonomy_profile(&p, cls, level);
        memset(d, 0, sizeof(*d));
        d->active = 1;
        d->scene_id = scene_id;
        d->x = x; d->y = y; d->z = z;
        d->cls = p.cls;
        d->level = p.level;
        d->dna_quality = p.dna_quality;
        d->dna_trait = p.dna_trait;
        d->dropped_ms = now_ms;
        d->collected_by = -1;
        return i;
    }
    return -1;
}

void bioslurry_tick(ServerState *s, unsigned int now_ms) {
    if (!s) return;
    for (int i = 0; i < BIOSLURRY_MAX_DROPS; i++) {
        BioslurryDrop *d = &g_drops[i];
        if (!d->active) continue;
        /* Rots where it lies, and a canister from another scene is not collectable from this one
           -- so a drop is scoped to the place it happened, like the tower and the tower's reason. */
        if (d->scene_id != s->scene_id || now_ms - d->dropped_ms >= BIOSLURRY_ROT_MS) {
            d->active = 0;
            d->collected_by = -1;
        }
    }
}

int bioslurry_collect(ServerState *s, unsigned int now_ms) {
    if (!s) return 0;
    int picked = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        PlayerState *p = &s->players[i];
        if (!p->active || p->state == STATE_DEAD) continue;
        if (g_carried[i] >= BIOSLURRY_MAX_CARRIED) continue;
        for (int k = 0; k < BIOSLURRY_MAX_DROPS; k++) {
            BioslurryDrop *d = &g_drops[k];
            if (!d->active || d->collected_by >= 0) continue;
            if (d->scene_id != p->scene_id) continue;
            float dx = d->x - p->x, dz = d->z - p->z;
            if (dx * dx + dz * dz > BIOSLURRY_PICKUP_RADIUS * BIOSLURRY_PICKUP_RADIUS) continue;
            d->collected_by = i;
            d->active = 0;
            g_last_quality[i] = d->dna_quality;
            g_last_trait[i] = d->dna_trait;
            g_last_cls[i] = d->cls;
            g_last_level[i] = d->level;
            g_carried[i]++;
            picked++;
            if (g_carried[i] >= BIOSLURRY_MAX_CARRIED) break;
        }
    }
    bioslurry_tick(s, now_ms);
    return picked;
}

int bioslurry_carried(int player_id) {
    if (player_id < 0 || player_id >= MAX_CLIENTS) return 0;
    return g_carried[player_id];
}

/* A delivery is a roll, not a guarantee: the strand's own grade is the chance it reads, so a
 * shambler is usually a wasted trip and an Alpha is usually a result. Deterministic per delivery
 * (the counter seeds it) so a test can reproduce it and a replay can too. */
static unsigned int slurry_hash(unsigned int x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

int bioslurry_deliver(int player_id, unsigned int now_ms) {
    (void)now_ms;
    if (player_id < 0 || player_id >= MAX_CLIENTS) return -1;
    if (g_carried[player_id] <= 0) return -1;
    g_carried[player_id]--;
    g_delivered_total++;

    /* The source is the last body this player collected (see the g_last_* arrays' own comment),
       and the read itself is a roll against that body's grade. */
    int quality = g_last_quality[player_id];
    int trait = g_last_trait[player_id];
    int cls = g_last_cls[player_id];
    int level = g_last_level[player_id];
    if (quality <= 0) { quality = 20; trait = 0; cls = 0; level = 1; }

    unsigned int roll = slurry_hash((unsigned int)(g_delivered_total * 2654435761u) ^ (unsigned int)player_id);
    if (quality < BIOSLURRY_DNA_FAIL_BELOW || (int)(roll % 100u) >= quality) {
        return 0;   /* spent, and nothing usable came off it */
    }

    if (g_dna_count < BIOSLURRY_MAX_DNA) {
        BioslurryStrand *s = &g_dna[g_dna_count++];
        s->quality = quality;
        s->trait = trait;
        s->cls = cls;
        s->level = level;
    }
    g_harvested_total++;
    return 1;
}

int bioslurry_drop_count(void) {
    int n = 0;
    for (int i = 0; i < BIOSLURRY_MAX_DROPS; i++) if (g_drops[i].active) n++;
    return n;
}

const BioslurryDrop *bioslurry_drop_at(int index) {
    if (index < 0 || index >= BIOSLURRY_MAX_DROPS) return NULL;
    if (!g_drops[index].active) return NULL;
    return &g_drops[index];
}

int bioslurry_dna_count(void) { return g_dna_count; }

const BioslurryStrand *bioslurry_dna_at(int index) {
    if (index < 0 || index >= g_dna_count) return NULL;
    return &g_dna[index];
}

int bioslurry_delivered_total(void) { return g_delivered_total; }
int bioslurry_harvested_total(void) { return g_harvested_total; }
