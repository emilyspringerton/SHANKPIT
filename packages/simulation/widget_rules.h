/* widget_rules.h -- composable widget circuits, with built-in defaults.
 *
 * The numbers and the boolean rules live in PARENA (PARENA/stdlib/shankpit/widget_rules.prn, generated
 * into widget_rules.c by `make regen-widget-rules`, installed through widget_rules_host.h). This header
 * carries matching C defaults so packages/simulation/widget_circuit.h keeps working in every binary
 * and test that includes it without linking a generated file -- the same arrangement slide_rules.h and
 * drone_flight.h use, and the circuit test asserts the two curves agree.
 */
#ifndef SHANKPIT_WIDGET_RULES_H
#define SHANKPIT_WIDGET_RULES_H

typedef struct {
    int (*output_power)(int kind, int armed, int src_power, int gate_power);
    int (*delay_armed)(int src_power, int armed, int timer_ticks);   /* == PARENA's arg order */
    int (*delay_still_running)(int timer_ticks);
    int (*lever_toggle)(int power);
    int (*plate_power)(int occupants);
    int (*motor_target)(int power);
    int (*focus_ok)(int dist_mm, int radius_mm, int busy);
    int (*panel_key_adjust)(int value, int delta, int lo, int hi);
    int (*delay_default_ticks)(void);
    int (*delay_min_ticks)(void);
    int (*delay_max_ticks)(void);
} WidgetRules;

static inline int wg_clamp(int lo, int hi, int x) { return x < lo ? lo : (x > hi ? hi : x); }
static inline int wg_on(void) { return 15; }
static inline int wg_off(void) { return 0; }

static inline int wg_c_output_power(int kind, int armed, int src, int gate) {
    if (kind == 0) return wg_clamp(0, 15, armed);          /* lever */
    if (kind == 1) return wg_clamp(0, 15, armed);          /* plate */
    if (kind == 2) return wg_clamp(0, 15, armed);          /* delay: holds its own pulse */
    if (kind == 3) return wg_clamp(0, 15, gate) > 0 ? wg_clamp(0, 15, src) : 0;  /* transistor */
    return wg_clamp(0, 15, src);                            /* motor follows its input */
}
/* A relay: a CHANGE in the input starts the timer, both edges. (The first draft delayed only the
 * rising edge and took its arguments in a different order from PARENA -- both were bugs, and the
 * host calling this with (src, armed, timer) is what exposed the second one.) */
static inline int wg_c_delay_armed(int src, int armed, int timer) {
    if (wg_clamp(0, 15, src) == wg_clamp(0, 15, armed)) return 0;
    return timer <= 0 ? 1 : 0;
}
static inline int wg_c_delay_running(int timer) { return timer > 0 ? 1 : 0; }
static inline int wg_c_lever_toggle(int power) { return wg_clamp(0, 15, power) > 0 ? wg_off() : wg_on(); }
static inline int wg_c_plate_power(int occupants) { return wg_clamp(0, 8, occupants) > 0 ? wg_on() : wg_off(); }
static inline int wg_c_motor_target(int power) { return wg_clamp(0, 15, power) > 0 ? 1 : 0; }
static inline int wg_c_focus_ok(int dist_mm, int radius_mm, int busy) {
    if (busy == 1) return 0;
    if (wg_clamp(0, 1000000, radius_mm) <= 0) return 0;
    return wg_clamp(0, 1000000, dist_mm) <= wg_clamp(0, 1000000, radius_mm) ? 1 : 0;
}
static inline int wg_c_panel_adjust(int value, int delta, int lo, int hi) {
    return wg_clamp(wg_clamp(-1000000, 1000000, lo), wg_clamp(-1000000, 1000000, hi), value + delta);
}
static inline int wg_c_delay_default(void) { return 30; }
static inline int wg_c_delay_min(void) { return 1; }
static inline int wg_c_delay_max(void) { return 600; }

static WidgetRules g_widget_rules = {
    wg_c_output_power, wg_c_delay_armed, wg_c_delay_running, wg_c_lever_toggle, wg_c_plate_power,
    wg_c_motor_target, wg_c_focus_ok, wg_c_panel_adjust, wg_c_delay_default, wg_c_delay_min, wg_c_delay_max
};

static inline void widget_set_rules(const WidgetRules *r) { if (r) g_widget_rules = *r; }

#endif /* SHANKPIT_WIDGET_RULES_H */
