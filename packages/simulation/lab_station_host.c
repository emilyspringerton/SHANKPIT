#include "lab_station_host.h"
#include "shadow_war_host.h"
#include <stdio.h>
#include <string.h>

int labst_can_use(int, int, int, int);
int labst_sample_delta(int);
int labst_target_delta(int);
int labst_clone_delta(int);
int labst_refine_base(int);
int labst_heal(int);
int labst_opens_phone(int);
int labst_clamp(int, int, int);
int labst_max_samples(void);
int labst_max_clones(void);

int lab_station_pick_base(const Phone *p) {
    int best = 0;
    for (int b = 1; b < 3; b++) if (p->samples[b] > p->samples[best]) best = b;
    return best;
}

void lab_station_use(Phone *p, int kind, LabStationResult *r) {
    static const char *const NAMES[7] = { "SPLICE", "CENTRIFUGE", "PCR", "VAT", "FRIDGE", "CONSOLE", "WAR" };
    r->used = 0; r->heal = 0; r->open_phone = 0; r->base = -1; r->msg[0] = 0;
    if (kind < 0 || kind > 6) return;
    int base = lab_station_pick_base(p);
    r->base = base;
    if (kind == LABST_FRIDGE) {
        snprintf(r->msg, sizeof r->msg, "FRIDGE S%d/%d/%d", p->samples[0], p->samples[1], p->samples[2]);
        r->used = 1;
        return;
    }
    if (kind == LABST_WAR) {
        /* its own system (shadow_war_host.h), not lab_station_rules.prn -- see this file's own
         * enum doc comment in lab_station_host.h. */
        ShadowWarDeployResult wr;
        shadow_war_deploy(p, &p->shadow_war_elo, 0 /* local sandbox hero is always slot 0 */, &wr);
        strncpy(r->msg, wr.msg, sizeof(r->msg) - 1);
        r->msg[sizeof(r->msg) - 1] = 0;
        r->used = wr.ok;
        return;
    }
    if (!labst_can_use(kind, base, p->samples[base], p->clone_count)) {
        snprintf(r->msg, sizeof r->msg, "%s: NOT READY", NAMES[kind]);
        return;
    }
    p->samples[base] = labst_clamp(p->samples[base] + labst_sample_delta(kind), 0, labst_max_samples());
    int td = labst_target_delta(kind);
    if (td) { int t = labst_refine_base(base); p->samples[t] = labst_clamp(p->samples[t] + td, 0, labst_max_samples()); }
    int cd = labst_clone_delta(kind);
    if (cd > 0 && p->clone_count < BP_CLONES) {
        p->clones[p->clone_count] = base;
        p->clone_traits[p->clone_count] = p->lab_trait;
        p->clone_count++;
    } else if (cd < 0 && p->clone_count > 0) {
        p->clone_count--;
    }
    r->heal = labst_heal(kind);
    r->open_phone = labst_opens_phone(kind);
    r->used = 1;
    snprintf(r->msg, sizeof r->msg, "%s OK  S%d/%d/%d  CLONES %d", NAMES[kind], p->samples[0], p->samples[1], p->samples[2], p->clone_count);
}
