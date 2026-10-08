#ifndef LAB_STATION_HOST_H
#define LAB_STATION_HOST_H
/* lab_station_host.h -- SECTION 592 (card #508): applies a LAB station interaction to the BIG_O phone's lab state.
 * Every rule (can it be used, what it costs, what it yields) is PARENA: lab_station_rules.c is GENERATED from
 * PARENA/stdlib/big_o/lab_station_rules.prn (make regen-lab-station / check-lab-station). This file is only the glue. */
#include "../common/phone.h"

/* kinds, matching the PARENA numbering and LEVEL_LAB_STATION_NAMES. LABST_WAR (SHANKPIT-native,
 * "continue full game" follow-up) is the one addition beyond the original SECTION 592 set --
 * its own rules live in PARENA/stdlib/big_o/shadow_war.prn / shadow_war_host.h, not
 * lab_station_rules.prn, so that already-shipped, already-tested file stays untouched. See
 * lab_station_use's own WAR branch below. */
enum { LABST_SPLICE = 0, LABST_CENTRIFUGE, LABST_PCR, LABST_VAT, LABST_FRIDGE, LABST_CONSOLE, LABST_WAR };

typedef struct {
    int used;         /* 1 if the interaction changed state (or opened the phone) */
    int heal;         /* hit points to heal the player (vat) */
    int open_phone;   /* console: host should open the phone's LAB app */
    int base;         /* the sample type that was worked on, -1 if none */
    char msg[48];     /* one-line HUD feedback */
} LabStationResult;

/* The working base is the sample type the player has most of (ties -> lowest). */
int lab_station_pick_base(const Phone *p);
/* Use the station `kind` against phone p (mutates samples / clones). Always fills *r. */
void lab_station_use(Phone *p, int kind, LabStationResult *r);
#endif
