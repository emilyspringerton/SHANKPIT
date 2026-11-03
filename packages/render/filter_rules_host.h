/* filter_rules_host.h -- installs PARENA's program-feed look into stream_filters.h.
 * PARENA/stdlib/shankpit/filter_rules.prn -> filter_rules.c (scripts/gen_filter_rules.sh,
 * `make regen-filter-rules`). A host that streams calls filter_rules_install() once at startup; a test
 * that only includes stream_filters.h keeps the built-in defaults. */
#ifndef SHANKPIT_FILTER_RULES_HOST_H
#define SHANKPIT_FILTER_RULES_HOST_H

#include "stream_filters.h"

int filter_preset_count(void);                  /* PARENA-generated (filter_rules.c) */
int filter_kernel(int preset, int index);
int filter_divisor(int preset);
int filter_offset(int preset);
int filter_black(int preset);
int filter_white(int preset);
int filter_gamma(int preset);
int filter_saturation(int preset);
int filter_vignette(int preset);
int filter_grain(int preset);

static inline void filter_rules_install(void) {
    static FilterRules r;
    r.preset_count = filter_preset_count;
    r.kernel = filter_kernel;
    r.divisor = filter_divisor;
    r.offset = filter_offset;
    r.black = filter_black;
    r.white = filter_white;
    r.gamma_permille = filter_gamma;
    r.saturation_permille = filter_saturation;
    r.vignette_permille = filter_vignette;
    r.grain_permille = filter_grain;
    filter_set_rules(&r);
}

#endif
