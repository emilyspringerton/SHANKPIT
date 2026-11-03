/* garage_rules_host.h -- installs PARENA's garage table into garage.h.
 * PARENA/stdlib/shankpit/garage_rules.prn -> garage_rules.c (scripts/gen_garage_rules.sh,
 * `make regen-garage-rules`); a host that owns a garage calls garage_rules_install() once at startup.
 * A test that only includes garage.h keeps the built-in defaults. */
#ifndef SHANKPIT_GARAGE_RULES_HOST_H
#define SHANKPIT_GARAGE_RULES_HOST_H

#include "garage.h"

int garage_accepts(int origin, int objective);   /* PARENA-generated (garage_rules.c) */
int garage_pays(int origin);

static inline void garage_rules_install(void) {
    static GarageRules r;
    r.accepts = garage_accepts;
    r.pays = garage_pays;
    garage_set_rules(&r);
}

#endif
