/* packages/simulation/trapx.h -- TRAPX VS0, the C host: two Detroit scenes, a city that remembers both.
 *
 * The decisions are PARENA's (`PARENA/stdlib/trapx/trapx_rules.prn`, emitted here to `trapx_rules.c` by
 * scripts/gen_trapx_rules.sh and to JavaScript for the WOTAN sandbox by scripts/build-trapx.sh there). This file
 * holds only what a host must hold: the state, the clock, the bank, and the event bus.
 *
 * The BIG_O loop is the whole point, and it closes here: something happens in public -> the witnesses see it
 * (their STATE is big_o/witness_rules.prn's business; this host is handed the count) -> the city reacts with heat
 * and trust -> enforcement climbs -> the street stiffens -> the ledger records it. Witnesses and the city are two
 * observers and they are allowed to disagree; that disagreement is the fiction.
 *
 * VS0 is two scenes and no more (200 Detroit Apartment, 201 Detroit School). City memory is one struct shared by
 * both, so crossing the portal does not reset anything -- which is acceptance criterion "city memory persists
 * across the 2-scene cluster", made structural instead of remembered.
 */
#ifndef SHANKPIT_TRAPX_H
#define SHANKPIT_TRAPX_H

#include "trapx_events.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TRAPX_VS0_SCENES 2
#define TRAPX_START_FLOW 100
#define TRAPX_FLOW_CAP 9999
#define TRAPX_LEDGER_MOD 1000000007

/* Scenes. Named here because a host has to name them somewhere; the RULES are what say they exist. */
enum { TRAPX_SCENE_APARTMENT = 200, TRAPX_SCENE_SCHOOL = 201 };

/* Actions, mirroring the .prn's documented vocabulary. */
enum { TRAPX_ACTION_NONE = 0, TRAPX_ACTION_CLAIM, TRAPX_ACTION_CONTEST, TRAPX_ACTION_DEPLOY_K9, TRAPX_ACTION_RECEIPT };

/* Receipt kinds, likewise. */
enum { TRAPX_RECEIPT_CLAIM = 0, TRAPX_RECEIPT_FLOW, TRAPX_RECEIPT_CONTEST, TRAPX_RECEIPT_K9, TRAPX_RECEIPT_WITNESS };

/* Return codes. 0 is always success; everything else is a named refusal, because "it did not work" is not a
 * message a player or a test can act on. */
enum {
    TRAPX_OK = 0,
    TRAPX_ERR_SCENE = 1,      /* not a VS0 scene */
    TRAPX_ERR_NOT_HERE = 2,   /* the player is not in that scene */
    TRAPX_ERR_FULL = 3,       /* every Field Office in that scene is already held */
    TRAPX_ERR_POOR = 4,       /* not enough Flow */
    TRAPX_ERR_NONE_HELD = 5,  /* nothing of yours to contest */
    TRAPX_ERR_NO_PORTAL = 6,  /* no link from here to there */
    TRAPX_ERR_ARG = 7         /* out of range (units, witnesses, ...) */
};

typedef struct {
    int scene;
    int alertness;      /* 0..100 */
    int trust;          /* -100..100 */
    int heat;           /* 0..100 */
    int enforcement;    /* 0..4 */
    int fos_total;      /* what the scene holds, from the rules */
    int fos_held;
    int attention;      /* 0..100 */
    int integrity;      /* 0..100 */
    int pressure;       /* 0..100 */
    int k9;             /* units deployed, 0..5 */
    int kills;
    int clean_receipts;
    int dirty_receipts;
    int rogue;          /* 1 once a swarm has been triggered here */
} TrapxDistrict;

typedef struct {
    TrapxDistrict districts[TRAPX_VS0_SCENES];
    int scene;          /* where the player is right now */
    int tick;
    int flow;           /* the bank: one ledger for the whole city, not one per scene */
    int receipts;
    int ledger;         /* chained receipt digest, mod TRAPX_LEDGER_MOD */
    TrapxEvents events;
    TrapxMods mods;
} TrapxCity;

/* Build a city and stand in `scene` (an unknown scene falls back to the Apartment and is refused by the callers
 * that care -- init cannot fail, a city always exists). Registers the VS0 default mods. */
void trapx_city_init(TrapxCity *city, int scene);
void trapx_city_register_default_mods(TrapxCity *city);

int trapx_city_index(const TrapxCity *city, int scene);      /* 0..1, or -1 */
TrapxDistrict *trapx_city_here(TrapxCity *city);
int trapx_city_enforcement(const TrapxCity *city, int scene);

/* One step for BOTH scenes. `quiet` (0/1) is the host's read on whether the player laid low this tick. Returns the
 * current scene's enforcement level after the step. */
int trapx_city_tick(TrapxCity *city, int quiet);

/* Claim the Field Office in `scene` (must be where you are). Costs trapx_claim_cost(scene, held). */
int trapx_city_claim(TrapxCity *city, int scene);
/* Contest one you already hold: pressure rises, the window is live, the ledger records it. */
int trapx_city_contest(TrapxCity *city, int scene);
/* Put dogs on the street (0..5). Attention moves immediately; the worth is K9 Doctrine's own curve. */
int trapx_city_deploy_k9(TrapxCity *city, int scene, int units);
/* A killing in public, seen by `witnesses` (>= 0). The city's reaction only -- the witnesses' own state machine is
 * big_o/witness_rules.prn, called by the game host, not by this one. */
int trapx_city_public_kill(TrapxCity *city, int scene, int witnesses);
/* Cross a portal. Refused unless the rules say the two scenes are linked. */
int trapx_city_portal(TrapxCity *city, int to_scene);

/* Append a receipt: chains the digest and announces it. Returns the digest, or -1 for an unknown kind. */
int trapx_city_receipt(TrapxCity *city, int scene, int kind, int amount);
/* Announce an event and run one delivery pass. Writes the interested mods' indices into `out`, returns the count. */
int trapx_city_announce(TrapxCity *city, int kind, int scene, int a, int b, int *out, int out_cap);

#ifdef __cplusplus
}
#endif

#endif /* SHANKPIT_TRAPX_H */
