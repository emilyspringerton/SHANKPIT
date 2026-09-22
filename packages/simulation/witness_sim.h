#ifndef WITNESS_SIM_H
#define WITNESS_SIM_H

/* witness_sim.h -- SHANKPIT's own witness/attention/decorum simulation (BIG_O engine merge, phase
 * 2). Ported in from BIG_O (core/sim.h + core/witness_rules.h's own enums), renamed
 * Sim/SimPlayer/SimNpc -> WitnessSim/WitnessPlayer/WitnessNpc and sim_* -> witness_sim_* to avoid
 * colliding with any other "Sim"-named thing in this codebase. Logic unchanged.
 *
 * This is a genuinely DIFFERENT NPC concept from story_ai.c's AIController roster -- story_ai's
 * AIRole enum (Rift Hound, Shambler Trooper, Guard, ...) is combat-AI for hostile enemies; this
 * module is BIG_O's social-stealth "who witnessed what" simulation for ambient citizens/The Men
 * reacting to being witnesses of an event (deny/panic/silence/engage), plus a per-player Decorum
 * meter and costume/zone trespass system. Checked before wiring: mapping this onto story_ai's
 * AIMode/AIRole state machine (as an earlier draft of EMILY/BACKLOG.md SECTION 536 phase 2
 * speculated) would be a real category error -- there is no existing "citizen" NPC concept in
 * story_ai.c to attach it to. This lands as a standalone, tested primitive instead (same "prove it
 * in isolation first" discipline BIG_O's own NORTHSTAR already used throughout), ready for a
 * future citizen-NPC spawn/render layer (SECTION 536 phase 7, MODE_STORY content cutover) to
 * actually drive gameplay from.
 *
 * Hand-written C around the PARENA-generated rules (witness_rules.c, from
 * PARENA/stdlib/shankpit/witness_rules.prn). Owns the things the rules module deliberately does
 * not: who is in which zone, who witnesses what, the seeded RNG (rolls are drawn here and passed
 * into the pure rules), and the deterministic event log. Spec: BIG_O/docs/B1_WITNESS_RULES.md. */

#include <stdint.h>
#include <stdio.h>

#define WITNESS_SIM_MAX_PLAYERS 3
#define WITNESS_SIM_MAX_NPCS 16

enum { WS_UNAWARE = 0, WS_DENIAL, WS_COMPROMISED, WS_SILENCING, WS_PANIC, WS_ENGAGE };
enum { DA_SMALL_TALK = 0, DA_SAY_APOCALYPSE, DA_CARRY_GEAR, DA_WRONG_COSTUME, DA_ATTRIBUTED_EVENT, DA_QUIET_TICK };
enum { BAND_OK = 0, BAND_SUSPICION, BAND_HYSTERIC, BAND_CANCELLED };
enum { COS_SUIT = 0, COS_LAB_SMOCK, COS_JANITOR, COS_STREET };
enum { ZONE_PUBLIC = 0, ZONE_LAB, ZONE_EXEC, ZONE_GENERATOR, ZONE_VAULT };

typedef struct { int present, zone, costume, token, gear, decorum, hunted, cancelled; } WitnessPlayer;
typedef struct { int zone, vigilance, arrogance, state, accomplice, witnessed_event; } WitnessNpc;

typedef struct {
    uint32_t rng;
    int tick, event_id, nplayers, nnpcs;
    WitnessPlayer p[WITNESS_SIM_MAX_PLAYERS];
    WitnessNpc n[WITNESS_SIM_MAX_NPCS];
    int public_sight_pct; /* weather: how well NPCs in the open (ZONE_PUBLIC) see; 100 = clear day */
    FILE *out; /* deterministic event log sink */
} WitnessSim;

void witness_sim_init(WitnessSim *s, uint32_t seed, int nplayers, FILE *out);
int witness_sim_add_npc(WitnessSim *s, int zone, int vigilance, int arrogance);   /* returns npc id or -1 */
int witness_sim_enter(WitnessSim *s, int pl, int zone);
int witness_sim_set_costume(WitnessSim *s, int pl, int costume);
int witness_sim_set_gear(WitnessSim *s, int pl, int gear);
int witness_sim_set_token(WitnessSim *s, int pl, int token);
int witness_sim_observe(WitnessSim *s, int pl);           /* noticing check for the current costume/zone/gear */
int witness_sim_say_apocalypse(WitnessSim *s, int pl);
int witness_sim_talk(WitnessSim *s, int pl);
int witness_sim_release(WitnessSim *s, int pl, int tier); /* loud zombie event, attributed to pl or the crew */
int witness_sim_force_witness(WitnessSim *s, int npc, int pl);
int witness_sim_los_lost(WitnessSim *s);       /* re-evaluate every NPC with no witnesses left in sight */
int witness_sim_memory_wipe(WitnessSim *s, int zone); /* cleanup crew (The Men) spray: hunting NPCs in zone -> DENIAL */
int witness_sim_eliminate(WitnessSim *s, int pl);      /* attributed target gone: hunting NPCs -> UNAWARE, hunt cleared */
void witness_sim_tick(WitnessSim *s, int n);
void witness_sim_print_state(const WitnessSim *s, FILE *out);

const char *witness_sim_ws_name(int state);
const char *witness_sim_band_name(int band);
const char *witness_sim_zone_name(int zone);
const char *witness_sim_costume_name(int costume);

/* Thin wrapper over the PARENA-generated decorum_band() (witness_rules.c has no public header --
 * see witness_sim.c's own prototype block -- so callers outside this file go through here). */
int witness_sim_decorum_band(int decorum);

#endif
