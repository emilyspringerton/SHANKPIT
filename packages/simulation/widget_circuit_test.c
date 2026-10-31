/* widget_circuit_test.c -- composable widget circuits, the F interface and panels, headless.
 *
 * The founder's ask is a composition system, so the checks are compositions, not single behaviours:
 * a lever into a delay into a machine, a plate into a transistor into a machine, an elevator as a
 * motor driven by a lever, and the register mirror a script would read. The rule curve is also checked
 * against the compiled-in defaults sample for sample, so the game and the module cannot disagree.
 */
#include <stdio.h>
#include <string.h>

#include "widget_circuit.h"
#include "widget_rules_host.h"

static int g_fail = 0, g_checks = 0;
static void ok(int cond, const char *what) {
    g_checks++;
    if (!cond) { g_fail++; printf("FAIL  %s\n", what); }
    else printf("ok    %s\n", what);
}

/* lever(0) -> delay(1) -> motor(2), plus plate(3) -> transistor(4, gate = lever) -> motor(5) */
static const char *LEVEL_JSON =
    "{\"name\":\"LAB\",\"width\":20,\"height\":10,\"depth\":20,\"ground_plane_enabled\":true,"
    "\"ground_plane_squares\":1,\"walls\":[],"
    "\"edu_widgets\":["
    "{\"entity_id\":\"lever_a\",\"machine_type\":6,\"memory_address\":10,\"x\":0,\"y\":0,\"z\":0,\"radius\":3},"
    "{\"entity_id\":\"relay_b\",\"machine_type\":7,\"memory_address\":11,\"x\":1,\"y\":0,\"z\":0,"
    "\"input_pin\":0,\"delay_ticks\":4},"
    "{\"entity_id\":\"lift_c\",\"machine_type\":1,\"memory_address\":12,\"x\":2,\"y\":0,\"z\":0,\"input_pin\":1},"
    "{\"entity_id\":\"plate_d\",\"machine_type\":5,\"memory_address\":13,\"x\":3,\"y\":0,\"z\":0,\"radius\":2},"
    "{\"entity_id\":\"gate_e\",\"machine_type\":8,\"memory_address\":14,\"x\":4,\"y\":0,\"z\":0,"
    "\"input_pin\":3,\"gate_pin\":0},"
    "{\"entity_id\":\"door_f\",\"machine_type\":2,\"memory_address\":15,\"x\":5,\"y\":0,\"z\":0,\"input_pin\":4}"
    "]}";

int main(void) {
    static CustomLevelData lvl;
    static WidgetCircuit c;
    int loaded;

    ok(level_boxes_parse_json(LEVEL_JSON, &lvl), "the level with wired widgets parses");
    ok(lvl.edu_widget_count == 6, "all six widgets are authored (lever, delay, motor, plate, gate, door)");
    ok(lvl.edu_widgets[1].input_pin == 0 && lvl.edu_widgets[1].delay_ticks == 4,
       "a widget keeps its input pin and its own delay length");
    ok(lvl.edu_widgets[0].input_pin == -1, "an unwired widget's pin is -1, not zero (which would be widget 0)");

    loaded = widget_circuit_load(&c, &lvl);
    ok(loaded == 6, "the circuit loads all six nodes");
    ok(c.n[0].kind == WIDGET_KIND_LEVER && c.n[1].kind == WIDGET_KIND_DELAY &&
       c.n[2].kind == WIDGET_KIND_MOTOR && c.n[3].kind == WIDGET_KIND_PLATE &&
       c.n[4].kind == WIDGET_KIND_TRANSISTOR && c.n[5].kind == WIDGET_KIND_MOTOR,
       "machine types map to the right circuit kinds");

    /* ---- a lever drives a delay which drives a machine --------------------------------------- */
    ok(c.n[2].power == 0, "the lift starts down");
    widget_circuit_tick(&c);
    ok(c.n[2].power == 0, "and stays down with the lever off");

    ok(widget_circuit_lever_toggle(&c, 0) == wg_on(), "flipping the lever puts it on 15");
    widget_circuit_tick(&c);
    ok(c.n[1].power == 0, "the delay does not pass the lever through instantly");
    ok(c.n[1].timer_ticks > 0, "the delay starts counting instead");
    for (int t = 0; t < 6; t++) widget_circuit_tick(&c);
    printf("      after the delay: relay %d, lift %d\n", c.n[1].power, c.n[2].power);
    ok(c.n[1].power == wg_on(), "the relay closes after its own delay");
    ok(c.n[2].power == wg_on(), "and the machine downstream runs (lever -> delay -> motor)");

    /* the falling edge is delayed too: a relay, not a one-shot */
    widget_circuit_lever_toggle(&c, 0);
    widget_circuit_tick(&c);
    ok(c.n[1].power == wg_on(), "switching the lever off does not drop the relay instantly");
    for (int t = 0; t < 6; t++) widget_circuit_tick(&c);
    printf("      after the falling delay: lever %d, relay %d, lift %d\n",
           c.n[0].power, c.n[1].power, c.n[2].power);
    ok(c.n[1].power == 0 && c.n[2].power == 0, "both edges are delayed, and the machine stops");
    ok(c.n[1].timer_ticks == 0, "the delay's timer is spent, not left running");

    /* ---- a plate through a transistor: a real AND ------------------------------------------- */
    widget_circuit_lever_toggle(&c, 0);          /* lever back on: the gate's enable */
    widget_circuit_tick(&c);
    widget_circuit_set_plate(&c, 3, 0);
    widget_circuit_tick(&c);
    ok(c.n[3].power == 0, "an empty plate reads 0");
    ok(c.n[5].power == 0, "so nothing reaches the door");

    widget_circuit_set_plate(&c, 3, 1);
    widget_circuit_tick(&c);
    ok(c.n[3].power == wg_on(), "standing on the plate powers it");
    ok(c.n[4].power == wg_on(), "the transistor passes it (gate is on)");
    ok(c.n[5].power == wg_on(), "and the door is powered: plate -> transistor -> door");

    widget_circuit_lever_toggle(&c, 0);          /* gate off, plate still pressed */
    widget_circuit_tick(&c);
    ok(c.n[3].power == wg_on() && c.n[4].power == 0,
       "with the gate off the transistor blocks it -- this is an AND, not a wire");
    ok(c.n[5].power == 0, "so the door closes with someone still standing on the plate");

    /* ---- the register mirror a script reads -------------------------------------------------- */
    printf("      registers: lever %d, relay %d, lift %d, gate %d, door %d\n",
           widget_circuit_reg_value(&c, 10), widget_circuit_reg_value(&c, 11),
           widget_circuit_reg_value(&c, 12), widget_circuit_reg_value(&c, 14),
           widget_circuit_reg_value(&c, 15));
    ok(widget_circuit_reg_value(&c, 10) == c.n[0].power, "a register mirrors the node's live power");
    ok(widget_circuit_reg_value(&c, 15) == 0, "a closed door's register reads 0");
    ok(widget_circuit_reg_value(&c, 99) == -1, "an unowned register reads -1, not a fake zero");

    /* ---- the F interface -------------------------------------------------------------------- */
    {
        WidgetCircuit f;
        widget_circuit_load(&f, &lvl);
        /* far away FIRST: hysteresis means "no focus" is only meaningful before anything was focused */
        ok(widget_circuit_focus(&f, 900.0f, 0.0f, 0.0f, 0, 16) == -1, "far from everything there is no focus");
        /* the camera-side busy flag suppresses the prompt entirely */
        ok(widget_circuit_focus(&f, 0.0f, 0.0f, 0.0f, 1, 16) == -1, "no prompt while busy");
        ok(widget_circuit_focus(&f, 0.0f, 0.0f, 0.0f, 0, 16) == 0, "standing on the lever focuses it");
        /* nearest wins: 1.0 away from the lever (reach 3) and 0.2 from the relay (default reach 3) */
        {
            int idx = widget_circuit_focus(&f, 0.8f, 0.0f, 0.0f, 0, 16);
            printf("      focus at x=0.8 is widget %d (%s)\n", idx, idx >= 0 ? f.n[idx].id : "-");
            ok(idx == 1, "the closest widget in reach wins");
        }
        /* hysteresis: stepping just out of reach keeps the prompt for a moment... */
        f.n[1].radius = 1.0f;
        widget_circuit_focus(&f, 0.0f, 0.0f, 0.0f, 0, 16);
        widget_circuit_focus(&f, 500.0f, 0.0f, 0.0f, 0, 16);
        ok(f.focused == 0 || f.focused == 1, "the prompt holds its last focus instead of blinking off");
        /* ...and releases after the hold window */
        for (int t = 0; t < 20; t++) widget_circuit_focus(&f, 500.0f, 0.0f, 0.0f, 0, 16);
        ok(f.focused == -1, "and it does go away once the player is really gone");
    }

    /* ---- panels: USE on a lever stays in the world, USE on a machine opens a panel ----------- */
    {
        WidgetCircuit p;
        widget_circuit_load(&p, &lvl);
        widget_circuit_focus(&p, 0.0f, 0.0f, 0.0f, 0, 16);
        ok(widget_circuit_use(&p) == WIDGET_MODE_WORLD, "a lever is used in the world, no panel");
        ok(p.n[0].armed == wg_on(), "and using it flipped it");

        widget_circuit_focus(&p, 2.0f, 0.0f, 0.0f, 0, 16);
        ok(widget_circuit_use(&p) == WIDGET_MODE_PANEL, "a machine with a register opens a panel");
        ok(p.pressed == 2, "the panel is the machine that was used");

        p.n[2].reg_value = 3000;
        ok(widget_circuit_panel_key(&p, WIDGET_KEY_UP, 8000) == 1, "UP is consumed by the panel");
        ok(p.n[2].reg_value == 3500, "UP dials the setpoint up by one step");
        for (int t = 0; t < 40; t++) widget_circuit_panel_key(&p, WIDGET_KEY_UP, 8000);
        ok(p.n[2].reg_value == 8000, "a held key cannot dial past the panel's own maximum");
        for (int t = 0; t < 60; t++) widget_circuit_panel_key(&p, WIDGET_KEY_DOWN, 8000);
        ok(p.n[2].reg_value == 0, "nor below zero");
        ok(widget_circuit_panel_key(&p, WIDGET_KEY_BACK, 8000) == 1 && p.mode == WIDGET_MODE_WORLD,
           "BACK returns to the world");
        ok(p.pressed == -1, "and the panel is released");
        ok(widget_circuit_panel_key(&p, WIDGET_KEY_UP, 8000) == 0,
           "keys in the world are not swallowed by a panel that is closed");
    }

    /* ---- the compiled defaults are PARENA's numbers ----------------------------------------- */
    {
        WidgetRules d = g_widget_rules;
        int mismatches = 0, samples = 0;
        widget_rules_install();
        for (int kind = -1; kind <= 5; kind++)
            for (int armed = 0; armed <= 15; armed += 5)
                for (int src = 0; src <= 15; src += 5)
                    for (int gate = 0; gate <= 15; gate += 5) {
                        samples++;
                        if (d.output_power(kind, armed, src, gate) != widget_output_power(kind, armed, src, gate))
                            mismatches++;
                    }
        printf("      PARENA vs compiled defaults: %d samples, %d mismatches\n", samples, mismatches);
        ok(mismatches == 0, "the module and the header defaults are the same circuit");
        ok(widget_kind_count() == 5, "five node kinds");
        ok(widget_delay_default_ticks() == 30 && widget_max_power() == 15, "the same delay and power range");
        ok(widget_focus_prompt_ms() > 0, "the prompt hysteresis window is real");
    }

    /* ---- malformed wiring degrades to "does nothing" ---------------------------------------- */
    {
        static CustomLevelData bad;
        static WidgetCircuit bc;
        const char *json =
            "{\"name\":\"BAD\",\"width\":4,\"height\":4,\"depth\":4,\"walls\":[],\"edu_widgets\":["
            "{\"entity_id\":\"m\",\"machine_type\":1,\"memory_address\":1,\"input_pin\":5},"
            "{\"entity_id\":\"\",\"machine_type\":6,\"memory_address\":2},"
            "{\"entity_id\":\"junk\",\"machine_type\":42,\"memory_address\":3}]}";
        ok(level_boxes_parse_json(json, &bad), "a level with bad wiring still parses");
        ok(bad.edu_widget_count == 1, "the empty id and the unknown machine type are dropped");
        widget_circuit_load(&bc, &bad);
        ok(bc.n[0].input_pin == -1, "a pin past the end of the list is dropped, never followed");
        widget_circuit_tick(&bc);
        ok(bc.n[0].power == 0, "so a mis-wired machine simply does nothing");
    }

    printf("\n%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
