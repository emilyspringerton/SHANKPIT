#ifndef SHANKPIT_TELECRYSTAL_H
#define SHANKPIT_TELECRYSTAL_H
/* telecrystal.h -- the telecrystal cast, as a pure state machine over the shared WORLDMAP rules.
 *
 * GoblinFoxDragon's lobby has had telecrystals for a while (apps/lobby/src/main.c telecrystal_tick); here the same
 * behaviour is written once against wm_rules_gen.h (compiled from PARENA/stdlib/worldmap/wm_rules.prn by BURROW),
 * so the SHANKPIT client and the GFD server share the ring test, the 1.5 slack and the commit/interrupt rule.
 * No SDL, no globals, no clock of its own: the host passes the player position, the G edge and now_ms.
 *
 * The client PREDICTS: TC_JUMP means "the cast committed"; the host sends the request to the GFD server (which owns
 * the registry, the cost and the destination) and only moves the player when the server agrees.
 */
#include "wm_rules_gen.h"

typedef struct {            /* a crystal as the client needs it (a subset of WmapCrystal) */
    float x, z, r;
    int total_ms, commit_ms;
} TcCrystal;

typedef enum { TC_IDLE = 0, TC_CASTING = 1 } TcPhase;
typedef enum { TC_NONE = 0, TC_STARTED, TC_INTERRUPTED, TC_JUMP, TC_FINISHED } TcEvent;

typedef struct {
    TcPhase phase;
    int crystal;            /* index being cast */
    unsigned started_ms;
    int committed;
    unsigned last_commit_ms;
} TcState;

static inline void tc_init(TcState *s) { s->phase = TC_IDLE; s->crystal = -1; s->started_ms = 0; s->committed = 0; s->last_commit_ms = 0; }

/* index of the crystal whose ring the player stands in, or -1 */
static inline int tc_near(const TcCrystal *c, int n, float px, float pz) {
    for (int i = 0; i < n; i++) if (wm_crystal_in_ring(px, pz, c[i].x, c[i].z, c[i].r)) return i;
    return -1;
}

/* One tick. Returns what happened this tick (TC_NONE most frames); *jump_to gets the crystal index on TC_JUMP. */
static inline TcEvent tc_tick(TcState *s, const TcCrystal *c, int n, float px, float pz, int g_edge, unsigned now_ms, int *jump_to) {
    if (s->phase == TC_CASTING) {
        const TcCrystal *cd = &c[s->crystal];
        unsigned elapsed = now_ms - s->started_ms;
        int still = wm_crystal_still_in_ring(px, pz, cd->x, cd->z, cd->r);
        if (!s->committed && wm_cast_interrupted((int)elapsed, cd->commit_ms, still)) {
            s->phase = TC_IDLE; s->crystal = -1;
            return TC_INTERRUPTED;
        }
        if (!s->committed && wm_cast_commits((int)elapsed, cd->commit_ms)) {
            s->committed = 1; s->last_commit_ms = now_ms;
            if (jump_to) *jump_to = s->crystal;
            return TC_JUMP;
        }
        if ((int)elapsed >= cd->total_ms) { s->phase = TC_IDLE; s->crystal = -1; return TC_FINISHED; }
        return TC_NONE;
    }
    if (g_edge && now_ms - s->last_commit_ms >= 250) {       /* 250 ms after a jump: the G that fired it is not a new cast */
        int i = tc_near(c, n, px, pz);
        if (i >= 0) { s->phase = TC_CASTING; s->crystal = i; s->started_ms = now_ms; s->committed = 0; return TC_STARTED; }
    }
    return TC_NONE;
}
#endif
