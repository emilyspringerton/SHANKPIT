/* slide_rules_host.h -- installs PARENA's slope-slide curve into slide_rules.h.
 * The rules live in PARENA/stdlib/shankpit/slide_rules.prn, generated into slide_rules.c (do not edit
 * it; `make regen-slide-rules`). A host that simulates players (apps/lobby, apps/server) includes this
 * header, links slide_rules.c, and calls slide_rules_install() once at startup. With nothing installed
 * slide_rules.h's own built-in defaults are used, so tests that only include physics.h are unchanged. */
#ifndef SHANKPIT_SLIDE_RULES_HOST_H
#define SHANKPIT_SLIDE_RULES_HOST_H

#include "slide_rules.h"

/* PARENA-generated (slide_rules.c) */
int slide_slope_flat_threshold_permille(void);
int slide_slope_steep_permille(int normal_y_permille);
int slide_slope_align_permille(int downhill_dot_permille);
int slide_slope_accel_permille(int normal_y_permille, int downhill_dot_permille, int speed_milli);
int slide_slope_scrub_permille(int normal_y_permille);
int slide_slope_speed_cap_milli(void);

static const SlideRules g_slide_rules_parena = {
    slide_slope_steep_permille, slide_slope_align_permille, slide_slope_accel_permille,
    slide_slope_scrub_permille, slide_slope_speed_cap_milli
};

static inline void slide_rules_install(void) { slide_set_rules(&g_slide_rules_parena); }

#endif /* SHANKPIT_SLIDE_RULES_HOST_H */
