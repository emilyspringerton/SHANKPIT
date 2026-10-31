#ifndef SHANKPIT_SLIDE_RULES_H
#define SHANKPIT_SLIDE_RULES_H

/* slide_rules.h -- the slope half of the crouch-slide, with built-in defaults.
 *
 * The numbers live in PARENA (PARENA/stdlib/shankpit/slide_rules.prn, generated into slide_rules.c by
 * `make regen-slide-rules` and installed through slide_rules_host.h). This header carries the same
 * math as plain C defaults so that physics.h keeps working in every binary and test that includes it
 * without linking a generated file -- the same arrangement drone_flight.h and vehicle_burn.h already
 * use, and the test asserts the two agree field by field.
 *
 * A host that wants PARENA's numbers calls slide_rules_install() once at startup (apps/lobby and
 * apps/server do). The defaults and the module are kept numerically identical on purpose: a slide
 * must feel the same in a test, a server tick and a client tick.
 */
#include <stddef.h>

typedef struct {
    int (*steep_permille)(int normal_y_permille);
    int (*align_permille)(int downhill_dot_permille);
    int (*accel_permille)(int normal_y_permille, int downhill_dot_permille, int speed_milli);
    int (*scrub_permille)(int normal_y_permille);
    int (*speed_cap_milli)(void);
} SlideRules;

static inline int slide_c_clamp(int lo, int hi, int x) { return x < lo ? lo : (x > hi ? hi : x); }

static inline int slide_c_steep(int ny) { return slide_c_clamp(0, 1000, (1000 - slide_c_clamp(0, 1000, ny)) * 3); }

static inline int slide_c_align(int dot) { return slide_c_clamp(0, 1000, dot); }

static inline int slide_c_cap(void) { return 1200; }

/* Constant in speed, like gravity; the host clamps the velocity to the cap. See slide_rules.prn's own
 * comment for why the tapered version was wrong. */
static inline int slide_c_accel(int ny, int dot, int speed) {
    if (slide_c_clamp(0, 1000, ny) >= 990) return 0;
    if (slide_c_clamp(0, slide_c_cap(), speed) >= slide_c_cap()) return 0;
    return slide_c_clamp(0, 1000, (slide_c_steep(ny) * slide_c_align(dot)) / 1000);
}

static inline int slide_c_scrub(int ny) {
    return slide_c_clamp(300, 1000, 1000 - (slide_c_steep(ny) * 700) / 1000);
}

static SlideRules g_slide_rules = {
    slide_c_steep, slide_c_align, slide_c_accel, slide_c_scrub, slide_c_cap
};

static inline void slide_set_rules(const SlideRules *r) { if (r) g_slide_rules = *r; }

#endif /* SHANKPIT_SLIDE_RULES_H */
