#include "witness_sim.h"

#include <string.h>

/* Prototypes for the PARENA-generated decision functions (witness_rules.c, do-not-edit-by-hand --
 * see that file's own header). No shared .h between generated units in this codebase
 * (packages/simulation/cutscene_effect_mod.c/day_night_clock.c both set the precedent: the
 * generated .c is the whole contract, callers just redeclare the prototypes they need). */
int effective_witnesses(int total, int accomplices);
int witness_state(int count, int arrogance, int compromised, int zombie_event);
int npc_next_state(int prev, int count, int arrogance, int compromised, int zombie_event, int resolved);
int is_legal_transition(int from, int to);
int engage_outcome(int arrogance, int tier);
int silence_target_mask(int attributed, int seen_together, int crew_mask);
int decorum_after(int d, int action);
int decorum_band(int d);
int decorum_start(void);
int zone_access(int costume, int zone, int token);
int conspicuousness(int allowed, int gear);
int noticed(int vigilance, int conspicuous, int roll);

static const char *WS[] = {"UNAWARE", "DENIAL", "COMPROMISED", "SILENCING", "PANIC", "ENGAGE"};
static const char *BAND[] = {"OK", "SUSPICION", "HYSTERIC", "CANCELLED"};
static const char *ZONE[] = {"public", "lab", "exec", "generator", "vault"};
static const char *COST[] = {"suit", "smock", "janitor", "street"};

const char *witness_sim_ws_name(int s) { return (s >= 0 && s <= 5) ? WS[s] : "?"; }
const char *witness_sim_band_name(int b) { return (b >= 0 && b <= 3) ? BAND[b] : "?"; }
const char *witness_sim_zone_name(int z) { return (z >= 0 && z <= 4) ? ZONE[z] : "?"; }
const char *witness_sim_costume_name(int c) { return (c >= 0 && c <= 3) ? COST[c] : "?"; }
int witness_sim_decorum_band(int decorum) { return decorum_band(decorum); }

static int roll100(WitnessSim *s) { /* xorshift32 -> pre-rolled 0..99 handed to the pure rules */
    s->rng ^= s->rng << 13; s->rng ^= s->rng >> 17; s->rng ^= s->rng << 5;
    return (int)(s->rng % 100u);
}

void witness_sim_init(WitnessSim *s, uint32_t seed, int nplayers, FILE *out) {
    memset(s, 0, sizeof *s);
    s->rng = seed ? seed : 0x9E3779B9u;
    s->out = out;
    if (nplayers < 1) nplayers = 1;
    if (nplayers > WITNESS_SIM_MAX_PLAYERS) nplayers = WITNESS_SIM_MAX_PLAYERS;
    s->nplayers = nplayers;
    for (int i = 0; i < nplayers; i++) {
        s->p[i].present = 1; s->p[i].zone = ZONE_PUBLIC; s->p[i].costume = COS_SUIT; s->p[i].decorum = decorum_start();
    }
}

int witness_sim_add_npc(WitnessSim *s, int zone, int vigilance, int arrogance) {
    if (s->nnpcs >= WITNESS_SIM_MAX_NPCS || zone < 0 || zone > 4) return -1;
    WitnessNpc *n = &s->n[s->nnpcs];
    n->zone = zone; n->vigilance = vigilance; n->arrogance = arrogance; n->state = WS_UNAWARE; n->accomplice = 0; n->witnessed_event = 0;
    return s->nnpcs++;
}

static int valid_player(const WitnessSim *s, int pl) { return pl >= 0 && pl < s->nplayers && s->p[pl].present && !s->p[pl].cancelled; }

static void apply_decorum(WitnessSim *s, int pl, int action, const char *why) {
    WitnessPlayer *p = &s->p[pl];
    int before = p->decorum;
    p->decorum = decorum_after(before, action);
    if (p->decorum != before) fprintf(s->out, "  decorum p%d %d -> %d (%s)\n", pl, before, p->decorum, why);
    if (decorum_band(p->decorum) == BAND_CANCELLED && !p->cancelled) {
        p->cancelled = 1;
        fprintf(s->out, "  p%d CANCELLED\n", pl);
    }
}

/* Rolls one noticing check per non-accomplice NPC in the zone (always consumes the rolls: deterministic RNG stream). */
static int anyone_notices(WitnessSim *s, int zone, int conspicuous) {
    int seen = 0;
    for (int i = 0; i < s->nnpcs; i++) {
        if (s->n[i].zone != zone || s->n[i].accomplice) continue;
        int roll = roll100(s);
        int vig = s->n[i].vigilance;
        if (zone == 0 && s->public_sight_pct > 0) vig = vig * s->public_sight_pct / 100;
        if (noticed(vig, conspicuous, roll)) seen++;
    }
    return seen;
}

int witness_sim_enter(WitnessSim *s, int pl, int zone) {
    if (!valid_player(s, pl) || zone < 0 || zone > 4) return -1;
    WitnessPlayer *p = &s->p[pl];
    p->zone = zone;
    fprintf(s->out, "EVENT tick=%d enter p%d %s costume=%s\n", s->tick, pl, witness_sim_zone_name(zone), witness_sim_costume_name(p->costume));
    return witness_sim_observe(s, pl);
}

int witness_sim_observe(WitnessSim *s, int pl) {
    if (!valid_player(s, pl)) return -1;
    WitnessPlayer *p = &s->p[pl];
    int allowed = zone_access(p->costume, p->zone, p->token);
    int cons = conspicuousness(allowed, p->gear);
    if (cons == 0) return 0;
    int seen = anyone_notices(s, p->zone, cons);
    fprintf(s->out, "  observe p%d allowed=%d gear=%d noticed_by=%d\n", pl, allowed, p->gear, seen);
    if (seen > 0) apply_decorum(s, pl, allowed ? DA_CARRY_GEAR : DA_WRONG_COSTUME, allowed ? "carrying field gear" : "wrong costume for zone");
    return seen;
}

int witness_sim_set_costume(WitnessSim *s, int pl, int c) { if (!valid_player(s, pl) || c < 0 || c > 3) return -1; s->p[pl].costume = c; return 0; }
int witness_sim_set_gear(WitnessSim *s, int pl, int g) { if (!valid_player(s, pl)) return -1; s->p[pl].gear = g ? 1 : 0; return 0; }
int witness_sim_set_token(WitnessSim *s, int pl, int t) { if (!valid_player(s, pl)) return -1; s->p[pl].token = t ? 1 : 0; return 0; }

int witness_sim_say_apocalypse(WitnessSim *s, int pl) {
    if (!valid_player(s, pl)) return -1;
    fprintf(s->out, "EVENT tick=%d say-apocalypse p%d zone=%s\n", s->tick, pl, witness_sim_zone_name(s->p[pl].zone));
    int seen = anyone_notices(s, s->p[pl].zone, conspicuousness(0, 0)); /* overt speech: treated as conspicuous */
    fprintf(s->out, "  noticed_by=%d\n", seen);
    if (seen > 0) apply_decorum(s, pl, DA_SAY_APOCALYPSE, "spoke of the apocalypse");
    return seen;
}

int witness_sim_talk(WitnessSim *s, int pl) {
    if (!valid_player(s, pl)) return -1;
    int listeners = 0;
    for (int i = 0; i < s->nnpcs; i++) if (s->n[i].zone == s->p[pl].zone && !s->n[i].accomplice) listeners++;
    fprintf(s->out, "EVENT tick=%d small-talk p%d listeners=%d\n", s->tick, pl, listeners);
    if (listeners > 0) apply_decorum(s, pl, DA_SMALL_TALK, "blended in");
    return listeners;
}

int witness_sim_release(WitnessSim *s, int pl, int tier) {
    if (!valid_player(s, pl)) return -1;
    int z = s->p[pl].zone, crew_mask = 0, total = 0, acc = 0;
    for (int i = 0; i < s->nplayers; i++) if (valid_player(s, i) && s->p[i].zone == z) crew_mask |= 1 << i;
    int together = (crew_mask & (crew_mask - 1)) != 0; /* >= 2 crew members present */
    for (int i = 0; i < s->nnpcs; i++) if (s->n[i].zone == z) { total++; if (s->n[i].accomplice) acc++; }
    int count = effective_witnesses(total, acc);
    s->event_id++;
    fprintf(s->out, "EVENT #%d tick=%d release p%d tier=%d zone=%s witnesses=%d accomplices=%d together=%d\n",
            s->event_id, s->tick, pl, tier, witness_sim_zone_name(z), count, acc, together);
    int silencing = 0;
    for (int i = 0; i < s->nnpcs; i++) {
        WitnessNpc *n = &s->n[i];
        if (n->zone != z || n->accomplice) continue;
        int prev = n->state, nx = npc_next_state(prev, count, n->arrogance, 0, 1, 0);
        if (!is_legal_transition(prev, nx)) { fprintf(s->out, "  BUG illegal transition npc %d %d->%d\n", i, prev, nx); return -2; }
        n->state = nx; n->witnessed_event = s->event_id;
        fprintf(s->out, "  npc%d %s -> %s\n", i, witness_sim_ws_name(prev), witness_sim_ws_name(nx));
        if (nx == WS_SILENCING) silencing++;
        if (nx == WS_ENGAGE) fprintf(s->out, "  npc%d engages: %s\n", i, engage_outcome(n->arrogance, tier) == 1 ? "citizen annihilated" : "zombie destroyed");
    }
    if (silencing > 0) {
        int mask = silence_target_mask(pl, together, crew_mask ? crew_mask : 1 << pl);
        fprintf(s->out, "  SILENCING pack hunts mask=%d (%d witnesses)\n", mask, silencing);
        for (int i = 0; i < s->nplayers; i++) if (mask & (1 << i)) {
            s->p[i].hunted = 1;
            apply_decorum(s, i, DA_ATTRIBUTED_EVENT, "attributed witnessed event");
        }
    }
    return silencing;
}

int witness_sim_force_witness(WitnessSim *s, int npc, int pl) {
    if (!valid_player(s, pl) || npc < 0 || npc >= s->nnpcs) return -1;
    WitnessNpc *n = &s->n[npc];
    if (n->zone != s->p[pl].zone) { fprintf(s->out, "  force npc%d refused: different zone\n", npc); return -1; }
    int prev = n->state, nx = npc_next_state(prev, 1, n->arrogance, 1, 1, 0);
    if (!is_legal_transition(prev, nx)) return -2;
    n->state = nx; n->accomplice = (nx == WS_COMPROMISED);
    fprintf(s->out, "EVENT tick=%d force-witness npc%d by p%d: %s -> %s (accomplice=%d)\n", s->tick, npc, pl, witness_sim_ws_name(prev), witness_sim_ws_name(nx), n->accomplice);
    return 0;
}

static int resolve_hunters(WitnessSim *s, int zone, int resolved, const char *why) {
    int changed = 0;
    for (int i = 0; i < s->nnpcs; i++) {
        WitnessNpc *n = &s->n[i];
        if (zone >= 0 && n->zone != zone) continue;
        int prev = n->state;
        if (prev != WS_SILENCING && prev != WS_ENGAGE) continue;
        int nx = npc_next_state(prev, 0, n->arrogance, 0, 1, resolved);
        if (!is_legal_transition(prev, nx)) { fprintf(s->out, "  BUG illegal transition npc %d %d->%d\n", i, prev, nx); return -2; }
        n->state = nx; changed++;
        fprintf(s->out, "  npc%d %s -> %s (%s)\n", i, witness_sim_ws_name(prev), witness_sim_ws_name(nx), why);
    }
    return changed;
}

int witness_sim_los_lost(WitnessSim *s) {
    fprintf(s->out, "EVENT tick=%d los-lost\n", s->tick);
    for (int i = 0; i < s->nnpcs; i++) {
        WitnessNpc *n = &s->n[i];
        if (n->accomplice) continue;
        int prev = n->state, nx = npc_next_state(prev, 0, n->arrogance, 0, 1, 0);
        if (nx != prev) { n->state = nx; fprintf(s->out, "  npc%d %s -> %s (no witnesses in sight)\n", i, witness_sim_ws_name(prev), witness_sim_ws_name(nx)); }
        else if (prev == WS_SILENCING || prev == WS_ENGAGE) fprintf(s->out, "  npc%d stays %s (hunt persists)\n", i, witness_sim_ws_name(prev));
    }
    return 0;
}

int witness_sim_memory_wipe(WitnessSim *s, int zone) {
    fprintf(s->out, "EVENT tick=%d memory-wipe zone=%s\n", s->tick, zone < 0 ? "all" : witness_sim_zone_name(zone));
    return resolve_hunters(s, zone, 1, "memory methylation wipe");
}

int witness_sim_eliminate(WitnessSim *s, int pl) {
    if (pl < 0 || pl >= s->nplayers) return -1;
    fprintf(s->out, "EVENT tick=%d target-eliminated p%d\n", s->tick, pl);
    s->p[pl].hunted = 0;
    return resolve_hunters(s, -1, 2, "target eliminated");
}

void witness_sim_tick(WitnessSim *s, int n) {
    for (int t = 0; t < n; t++) {
        s->tick++;
        for (int i = 0; i < s->nplayers; i++) if (valid_player(s, i)) apply_decorum(s, i, DA_QUIET_TICK, "quiet tick");
    }
    fprintf(s->out, "TICK -> %d\n", s->tick);
}

void witness_sim_print_state(const WitnessSim *s, FILE *out) {
    fprintf(out, "STATE tick=%d events=%d\n", s->tick, s->event_id);
    for (int i = 0; i < s->nplayers; i++) {
        const WitnessPlayer *p = &s->p[i];
        fprintf(out, "  p%d zone=%s costume=%s token=%d gear=%d decorum=%d band=%s hunted=%d cancelled=%d\n", i,
                witness_sim_zone_name(p->zone), witness_sim_costume_name(p->costume), p->token, p->gear, p->decorum,
                witness_sim_band_name(decorum_band(p->decorum)), p->hunted, p->cancelled);
    }
    for (int i = 0; i < s->nnpcs; i++) {
        const WitnessNpc *n = &s->n[i];
        fprintf(out, "  npc%d zone=%s vig=%d arr=%d state=%s accomplice=%d\n", i, witness_sim_zone_name(n->zone), n->vigilance, n->arrogance, witness_sim_ws_name(n->state), n->accomplice);
    }
}
