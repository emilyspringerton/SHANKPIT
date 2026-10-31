/* widget_rules_host.h -- installs PARENA's widget circuit rules into widget_rules.h.
 * The rules live in PARENA/stdlib/shankpit/widget_rules.prn, generated into widget_rules.c (do not edit
 * it; `make regen-widget-rules`). A host that simulates widgets (apps/lobby, apps/server) includes this
 * header, links widget_rules.c, and calls widget_rules_install() once at startup. With nothing
 * installed widget_rules.h's own built-in defaults are used, so tests keep their link lines. */
#ifndef SHANKPIT_WIDGET_RULES_HOST_H
#define SHANKPIT_WIDGET_RULES_HOST_H

#include "widget_rules.h"

/* PARENA-generated (widget_rules.c) */
int widget_kind_count(void);
int widget_power_on(void);
int widget_power_off(void);
int widget_max_power(void);
int widget_delay_default_ticks(void);
int widget_delay_min_ticks(void);
int widget_delay_max_ticks(void);
int widget_output_power(int kind, int armed, int src_power, int gate_power);
int widget_delay_armed(int src_power, int armed, int timer_ticks);
int widget_delay_still_running(int timer_ticks);
int widget_lever_toggle(int power);
int widget_plate_power(int occupants);
int widget_motor_target(int power);
int widget_focus_ok(int dist_mm, int radius_mm, int busy);
int widget_focus_prompt_ms(void);
int widget_panel_key_adjust(int value, int delta, int lo, int hi);

static const WidgetRules g_widget_rules_parena = {
    widget_output_power, widget_delay_armed, widget_delay_still_running, widget_lever_toggle,
    widget_plate_power, widget_motor_target, widget_focus_ok, widget_panel_key_adjust,
    widget_delay_default_ticks, widget_delay_min_ticks, widget_delay_max_ticks
};

static inline void widget_rules_install(void) { widget_set_rules(&g_widget_rules_parena); }

#endif /* SHANKPIT_WIDGET_RULES_HOST_H */
