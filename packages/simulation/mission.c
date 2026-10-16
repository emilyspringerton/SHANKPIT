#include <stdio.h>
#include <string.h>
#include "mission.h"
#include <stdarg.h>

/* NULL-safe log sink (WitnessSim.out may be NULL in a headless host). */
static void sim_log(FILE *out, const char *fmt, ...) {
    if (!out) return;
    va_list ap; va_start(ap, fmt); vfprintf(out, fmt, ap); va_end(ap);
}
#define SIM_LOG(s, ...) sim_log((s)->out, __VA_ARGS__)

static int reject(Mission *m, WitnessSim *s, const char *what, const char *why) {
    SIM_LOG(s, "MISSION rejected %s: %s\n", what, why);
    (void)m;
    return -1;
}
static void fail(Mission *m, WitnessSim *s, const char *why) {
    if (m->state != M_ACTIVE) return;
    m->state = M_FAILED;
    snprintf(m->fail_reason, sizeof(m->fail_reason), "%s", why);
    SIM_LOG(s, "MISSION FAILED: %s\n", why);
}
static int live(Mission *m, WitnessSim *s, const char *what) {
    if (m->state != M_ACTIVE) return reject(m, s, what, "mission not active");
    return 0;
}
static int at(WitnessSim *s, int pl, int zone) { return pl >= 0 && pl < s->nplayers && s->p[pl].present && s->p[pl].zone == zone; }

void mission_start(Mission *m) { memset(m, 0, sizeof(*m)); m->state = M_ACTIVE; m->supervisor_present = 1; }

const char *mission_state_name(MissionState st) {
    static const char *n[] = { "INACTIVE", "ACTIVE", "COMPLETE", "FAILED" };
    return (st >= 0 && st <= 3) ? n[st] : "?";
}

int mission_phase(const Mission *m) {
    if (m->state != M_ACTIVE) return 0;
    if (!(m->has_smock && m->in_sector2)) return 1;
    if (!m->data_stolen) return 2;
    return 3;
}

int mission_tick(Mission *m, WitnessSim *s, int minutes) {
    if (m->state != M_ACTIVE) return 0;
    for (int i = 0; i < minutes && m->state == M_ACTIVE; i++) {
        m->minutes++;
        witness_sim_tick(s, 1);
        if (m->drive_in && !m->supervisor_present && m->transfer_pct < 100) {
            m->transfer_pct += M_TRANSFER_PER_MIN;
            if (m->transfer_pct > 100) m->transfer_pct = 100;
        }
        if (m->drive_in && m->supervisor_present) fail(m, s, "supervisor returned to a foreign drive in the terminal");
        for (int p = 0; p < s->nplayers; p++) {
            if (!s->p[p].present) continue;
            if (s->p[p].cancelled) fail(m, s, "cancelled: decorum collapsed");
            else if (s->p[p].hunted) fail(m, s, "hunted: a silencing pack is on you");
        }
        if (m->state == M_ACTIVE && m->minutes >= M_WINDOW_MIN && !m->escaped) fail(m, s, "night-shift window closed (03:00)");
    }
    return 0;
}

int mission_acquire_smock(Mission *m, WitnessSim *s, int pl) {
    if (live(m, s, "acquire_smock")) return -1;
    if (!at(s, pl, ZONE_GENERATOR)) return reject(m, s, "acquire_smock", "the laundry chute is in the maintenance hallway");
    m->has_smock = 1;
    SIM_LOG(s, "MISSION acquired Level-2 Lab Tech Smock\n");
    return witness_sim_set_costume(s, pl, COS_LAB_SMOCK);
}

int mission_enter_sector2(Mission *m, WitnessSim *s, int pl) {
    if (live(m, s, "enter_sector2")) return -1;
    if (!m->has_smock) return reject(m, s, "enter_sector2", "no smock: Sector-2 is smock-only");
    m->in_sector2 = 1;
    return witness_sim_enter(s, pl, ZONE_LAB);
}

int mission_supervisor(Mission *m, WitnessSim *s, int present) {
    if (live(m, s, "supervisor")) return -1;
    m->supervisor_present = present ? 1 : 0;
    SIM_LOG(s, "MISSION supervisor %s\n", present ? "logs in at the terminal" : "leaves for break");
    return 0;
}

int mission_surf(Mission *m, WitnessSim *s, int pl) {
    if (live(m, s, "surf")) return -1;
    if (!at(s, pl, ZONE_LAB) || !m->in_sector2) return reject(m, s, "surf", "must be at the Room 404 terminal");
    if (!m->supervisor_present) return reject(m, s, "surf", "supervisor is not logged in; nothing to watch");
    if (witness_sim_decorum_band(s->p[pl].decorum) >= BAND_HYSTERIC) return reject(m, s, "surf", "too suspicious to loiter behind the station");
    /* NORTHSTAR.md §39: bystanders may catch the lean-in. Caught = decorum hit and no PIN; straighten up and retry. */
    if (witness_sim_snoop(s, pl) > 0) return reject(m, s, "surf", "a bystander saw you reading over the supervisor's shoulder");
    m->pin_known = 1;
    SIM_LOG(s, "MISSION captured the 8-digit Master PIN\n");
    return 0;
}

int mission_insert_drive(Mission *m, WitnessSim *s, int pl) {
    if (live(m, s, "insert_drive")) return -1;
    if (!at(s, pl, ZONE_LAB) || !m->in_sector2) return reject(m, s, "insert_drive", "must be at the Room 404 terminal");
    if (!m->pin_known) return reject(m, s, "insert_drive", "air-gapped terminal is PIN-locked");
    if (m->supervisor_present) return reject(m, s, "insert_drive", "supervisor is at the terminal");
    m->drive_in = 1;
    SIM_LOG(s, "MISSION flash drive inserted, copying FA_REPRESSOR_LOOPS_WGS.fa\n");
    return 0;
}

int mission_swap_drive(Mission *m, WitnessSim *s, int pl) {
    if (live(m, s, "swap_drive")) return -1;
    if (!at(s, pl, ZONE_LAB) || !m->drive_in) return reject(m, s, "swap_drive", "no drive in the terminal");
    if (m->transfer_pct < 100) return reject(m, s, "swap_drive", "transfer not finished");
    m->drive_in = 0; m->data_stolen = 1;
    SIM_LOG(s, "MISSION transfer complete, drive swapped\n");
    return 0;
}

int mission_grab_mop(Mission *m, WitnessSim *s, int pl) {
    if (live(m, s, "grab_mop")) return -1;
    if (!at(s, pl, ZONE_GENERATOR) && !at(s, pl, ZONE_LAB)) return reject(m, s, "grab_mop", "no mop nearby");
    SIM_LOG(s, "MISSION grabbed a janitorial mop\n");
    return witness_sim_set_costume(s, pl, COS_JANITOR);
}

int mission_exit(Mission *m, WitnessSim *s, int pl) {
    if (live(m, s, "exit")) return -1;
    if (!m->data_stolen) return reject(m, s, "exit", "leaving without the data");
    if (s->p[pl].costume != COS_JANITOR) return reject(m, s, "exit", "the service elevator is janitor-only");
    m->escaped = 1;
    SIM_LOG(s, "MISSION exited via the service elevator\n");
    return witness_sim_enter(s, pl, ZONE_GENERATOR);
}

int mission_upload(Mission *m, WitnessSim *s, int pl) {
    if (live(m, s, "upload")) return -1;
    (void)pl;
    if (!m->escaped) return reject(m, s, "upload", "not out of the building yet");
    m->state = M_COMPLETE;
    SIM_LOG(s, "MISSION COMPLETE: data uploaded to the basement centrifuge terminal, clone embryos patched\n");
    return 0;
}
