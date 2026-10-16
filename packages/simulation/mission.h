/* Act I, Mission 1 tracker ("Clean Up Your Math", brief: docs/a1m1.md). Hand-written host layer over WitnessSim: the rules
 * core (witness_rules) decides costume/zone/decorum/noticing; this owns what only a mission has -- the night-shift clock,
 * the shoulder-surfed PIN, the flash-drive transfer, and win/fail. Deterministic; every action returns 0 ok, -1 rejected
 * (with the reason logged), so scripts and the eventual game host share one rulebook. Zone mapping (interim, until the
 * real level exists): maintenance hallway + service elevator = GENERATOR, Sector-2 floor + Room 404 = LAB,
 * Sector-7 basement = reached via mission_upload (no zone). */
#ifndef MISSION_H
#define MISSION_H
#include "witness_sim.h"

#define M_WINDOW_MIN 240        /* 23:00 -> 03:00, one sim tick = one minute */
#define M_TRANSFER_PER_MIN 10   /* percent of FA_REPRESSOR_LOOPS_WGS.fa copied per minute */

typedef enum { M_INACTIVE = 0, M_ACTIVE, M_COMPLETE, M_FAILED } MissionState;

typedef struct {
    MissionState state;
    int minutes;                /* elapsed since 23:00 */
    int has_smock, in_sector2;  /* phase 1 */
    int supervisor_present;     /* 1 = logged in at the terminal, 0 = on break */
    int pin_known;              /* phase 2: 8-digit master PIN captured */
    int drive_in, transfer_pct, data_stolen;
    int escaped;                /* left via the service elevator with the data */
    char fail_reason[64];
    char last_msg[96];          /* last reject reason / success line, for a host UI (the log sink may be NULL) */
} Mission;

void mission_start(Mission *m);
int mission_phase(const Mission *m);                       /* 1..3, 0 if not active */
int mission_tick(Mission *m, WitnessSim *s, int minutes);         /* advances clock + transfer, evaluates fail conditions */
int mission_acquire_smock(Mission *m, WitnessSim *s, int pl);     /* laundry chute, maintenance hallway */
int mission_enter_sector2(Mission *m, WitnessSim *s, int pl);     /* requires the smock */
int mission_supervisor(Mission *m, WitnessSim *s, int present);   /* logs in (1) / goes on break (0) */
int mission_surf(Mission *m, WitnessSim *s, int pl);              /* shoulder-surf the PIN; conspicuous */
int mission_insert_drive(Mission *m, WitnessSim *s, int pl);
int mission_swap_drive(Mission *m, WitnessSim *s, int pl);        /* transfer must be 100% */
int mission_grab_mop(Mission *m, WitnessSim *s, int pl);          /* janitor costume in the hallway */
int mission_exit(Mission *m, WitnessSim *s, int pl);              /* service elevator, with the data */
int mission_upload(Mission *m, WitnessSim *s, int pl);            /* basement centrifuge terminal: patch the embryos, win */
const char *mission_state_name(MissionState st);
#endif
