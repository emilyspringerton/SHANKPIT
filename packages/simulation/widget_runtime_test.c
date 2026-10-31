/* widget_runtime_test.c -- a widget circuit running in a level, headlessly.
 *
 * The circuit's own maths is covered by `make test-widget-circuit`. This file covers the host: the first
 * tick authoritatively places every machine's box, a lever -> delay -> lift moves geometry four ticks
 * late on BOTH edges, a plate -> transistor -> security door is a real AND, a terminal panel's setpoint
 * survives the tick that used to clobber it, the register mirror fires once per change, and the two
 * properties a prompt depends on (busy suppresses it, walking away releases it after the hold window).
 */
#include <stdio.h>
#include <string.h>

#include "widget_runtime.h"
#include "widget_rules_host.h"

static int g_fail = 0, g_checks = 0;
static void ok(int cond, const char *what) {
    g_checks++;
    if (!cond) { g_fail++; printf("FAIL  %s\n", what); }
    else printf("ok    %s\n", what);
}

#define MAX_EVENTS 16
static int g_box_at[MAX_EVENTS], g_box_open[MAX_EVENTS], g_box_n;
static int g_reg16_events, g_reg_last_reg, g_reg_last_value;

static void box_sink(void *user, int box_index, int open) {
    (void)user;
    if (g_box_n < MAX_EVENTS) { g_box_at[g_box_n] = box_index; g_box_open[g_box_n] = open; g_box_n++; }
}
static void reg_sink(void *user, const char *id, int reg, int value) {
    (void)user; (void)id;
    g_reg_last_reg = reg;
    g_reg_last_value = value;
    if (reg == 16) g_reg16_events++;
}
/* Other machines can move in the same window (the gate lever also drives the lift's relay), so the
 * door checks ask "did box 9 move to this state", not "was it the only event". */
static int box_saw(int box, int open) {
    for (int i = 0; i < g_box_n; i++)
        if (g_box_at[i] == box && g_box_open[i] == open) return 1;
    return 0;
}
static void sinks_reset(void) {
    g_box_n = 0;
    g_reg16_events = 0;
    g_reg_last_reg = -1;
    g_reg_last_value = 0;
}

/* Ten real walls, because level_boxes.h detaches a box_index that points past the level's own geometry --
 * a machine can only drive a box that exists (box 7 is the lift car, box 9 the security door). */
#define WALLS_10 \
    "{\"x\":0,\"y\":2,\"z\":16,\"sx\":6,\"sy\":4,\"sz\":1}," \
    "{\"x\":0,\"y\":2,\"z\":18,\"sx\":6,\"sy\":4,\"sz\":1}," \
    "{\"x\":0,\"y\":2,\"z\":20,\"sx\":6,\"sy\":4,\"sz\":1}," \
    "{\"x\":0,\"y\":2,\"z\":22,\"sx\":6,\"sy\":4,\"sz\":1}," \
    "{\"x\":0,\"y\":2,\"z\":24,\"sx\":6,\"sy\":4,\"sz\":1}," \
    "{\"x\":0,\"y\":2,\"z\":26,\"sx\":6,\"sy\":4,\"sz\":1}," \
    "{\"x\":0,\"y\":2,\"z\":28,\"sx\":6,\"sy\":4,\"sz\":1}," \
    "{\"x\":0,\"y\":2,\"z\":30,\"sx\":6,\"sy\":4,\"sz\":1}," \
    "{\"x\":0,\"y\":2,\"z\":32,\"sx\":6,\"sy\":4,\"sz\":1}," \
    "{\"x\":0,\"y\":2,\"z\":34,\"sx\":6,\"sy\":4,\"sz\":1}"

/* The lab, wired the way the docs tell an agent to wire it:
 *   0 lever_a   (6) reg 10                          -- the light switch / gate lever
 *   1 relay_b   (7) reg 11  input 0, 4 ticks        -- the elevator's closing pause
 *   2 lift_c    (1) reg 12  input 1, box 7          -- the car
 *   3 plate_d   (5) reg 13  radius 2                -- the mat by the door
 *   4 gate_e    (8) reg 14  input 3, gate 0         -- "unlocked"
 *   5 door_f    (2) reg 15  input 4, box 9          -- the security door
 *   6 term_g    (1) reg 16  radius 3                -- a terminal: a register and no box
 */
static const char *LEVEL_JSON =
    "{\"name\":\"LAB\",\"width\":40,\"height\":10,\"depth\":40,\"ground_plane_enabled\":true,"
    "\"ground_plane_squares\":1,\"walls\":[" WALLS_10 "],"
    "\"edu_widgets\":["
    "{\"entity_id\":\"lever_a\",\"machine_type\":6,\"memory_address\":10,\"x\":0,\"y\":0,\"z\":0,\"radius\":3},"
    "{\"entity_id\":\"relay_b\",\"machine_type\":7,\"memory_address\":11,\"x\":1,\"y\":0,\"z\":0,"
    "\"input_pin\":0,\"delay_ticks\":4},"
    "{\"entity_id\":\"lift_c\",\"machine_type\":1,\"memory_address\":12,\"x\":2,\"y\":0,\"z\":0,"
    "\"input_pin\":1,\"box_index\":7},"
    "{\"entity_id\":\"plate_d\",\"machine_type\":5,\"memory_address\":13,\"x\":10,\"y\":0,\"z\":0,\"radius\":2},"
    "{\"entity_id\":\"gate_e\",\"machine_type\":8,\"memory_address\":14,\"x\":4,\"y\":0,\"z\":0,"
    "\"input_pin\":3,\"gate_pin\":0},"
    "{\"entity_id\":\"door_f\",\"machine_type\":2,\"memory_address\":15,\"x\":5,\"y\":0,\"z\":0,"
    "\"input_pin\":4,\"box_index\":9},"
    "{\"entity_id\":\"term_g\",\"machine_type\":1,\"memory_address\":16,\"x\":-10,\"y\":0,\"z\":0,\"radius\":3}"
    "]}";

static const char *EMPTY_JSON =
    "{\"name\":\"PLAIN\",\"width\":10,\"height\":10,\"depth\":10,\"ground_plane_enabled\":true,"
    "\"ground_plane_squares\":1,\"walls\":[]}";

int main(void) {
    static CustomLevelData lvl;
    static CustomLevelData empty;
    static WidgetRuntime rt;
    WidgetActor a;

    ok(level_boxes_parse_json(LEVEL_JSON, &lvl), "the lab level parses");
    ok(lvl.count == 10 && lvl.edu_widget_count == 7, "ten walls and seven widgets are authored");
    ok(widget_runtime_load(&rt, &lvl) == 7, "the runtime loads all seven nodes");
    ok(rt.box[2] == 7 && rt.box[5] == 9 && rt.box[6] == -1,
       "each machine's authored box follows its id, and a terminal has none");
    ok(widget_runtime_owns_box(&rt, 7) && widget_runtime_owns_box(&rt, 9) &&
       !widget_runtime_owns_box(&rt, 3),
       "the host knows which boxes the circuit drives, so a legacy door path can stay out of the way");
    ok(strcmp(widget_runtime_kind_name(rt.c.n[1].kind), "RELAY") == 0,
       "a node can name itself for the prompt");

    /* ---- the first tick places the world -------------------------------------------------- */
    a.x = 0.0f; a.y = 0.0f; a.z = 0.0f;
    sinks_reset();
    ok(widget_runtime_tick(&rt, &a, 1, reg_sink, box_sink, NULL) == 2,
       "the first tick publishes both machines' authored state, once");
    ok(g_box_n == 2 && g_box_open[0] == 0 && g_box_open[1] == 0,
       "and both of those boxes start closed, where the author put them");

    /* ---- the elevator: lever -> relay -> car, and the car is late -------------------------- */
    sinks_reset();
    ok(widget_runtime_focus(&rt, a.x, a.y, a.z, 0, 16) == 0, "the lever is the focus at its own position");
    ok(widget_runtime_use(&rt) == 1, "F acts on the lever");
    ok(rt.c.mode == WIDGET_MODE_WORLD, "a lever stays in the world instead of opening a panel");
    ok(rt.c.n[0].armed == wg_on(), "and it latches on");

    ok(widget_runtime_tick(&rt, &a, 1, reg_sink, box_sink, NULL) == 0,
       "the car does not move on the tick the lever flips");
    for (int i = 0; i < 3; i++) widget_runtime_tick(&rt, &a, 1, reg_sink, box_sink, NULL);
    ok(g_box_n == 0, "the relay is still holding four ticks in, so nothing has moved");
    widget_runtime_tick(&rt, &a, 1, reg_sink, box_sink, NULL);
    ok(g_box_n == 1 && g_box_at[0] == 7 && g_box_open[0] == 1,
       "the lift opens when the relay closes, five ticks after the press");

    /* a relay is a relay: the falling edge is late too */
    sinks_reset();
    widget_runtime_focus(&rt, a.x, a.y, a.z, 0, 16);
    ok(widget_runtime_use(&rt) == 1 && rt.c.n[0].armed == wg_off(), "F flips the same lever back off");
    for (int i = 0; i < 4; i++) widget_runtime_tick(&rt, &a, 1, reg_sink, box_sink, NULL);
    ok(g_box_n == 0, "the lift does not drop early on the falling edge either");
    widget_runtime_tick(&rt, &a, 1, reg_sink, box_sink, NULL);
    ok(g_box_n == 1 && g_box_at[0] == 7 && g_box_open[0] == 0, "it drops when the relay opens");

    /* ---- the security door: a plate alone is not enough ------------------------------------ */
    sinks_reset();
    a.x = 10.0f; a.z = 0.0f;
    for (int i = 0; i < 10; i++) widget_runtime_tick(&rt, &a, 1, reg_sink, box_sink, NULL);
    ok(rt.c.n[3].power == wg_on(), "the plate reads as pressed while a body stands on it");
    ok(rt.c.n[4].power == wg_off(), "the transistor stays shut with its gate lever off");
    ok(g_box_n == 0, "so the security door never opens -- a plate alone is not enough");
    ok(rt.c.n[2].power == wg_off(), "and the lift is still down, undisturbed by the plate traffic");

    /* the gate lever is the \"unlocked\" switch */
    a.x = 0.0f; a.z = 0.0f;
    ok(widget_runtime_focus(&rt, a.x, a.y, a.z, 0, 16) == 0, "the gate lever is in reach");
    ok(widget_runtime_use(&rt) == 1, "and F turns it on");
    widget_runtime_tick(&rt, &a, 1, reg_sink, box_sink, NULL);
    ok(rt.c.n[0].power == wg_on(), "the gate line is live");

    a.x = 10.0f; a.z = 0.0f;
    sinks_reset();
    for (int i = 0; i < 3; i++) widget_runtime_tick(&rt, &a, 1, reg_sink, box_sink, NULL);
    ok(box_saw(9, 1), "with the gate powered the same plate opens the security door");

    a.x = 30.0f; a.z = 30.0f;
    sinks_reset();
    for (int i = 0; i < 3; i++) widget_runtime_tick(&rt, &a, 1, reg_sink, box_sink, NULL);
    ok(box_saw(9, 0), "stepping off closes it again");
    ok(rt.c.n[3].power == wg_off(), "and the plate drops to zero");

    /* a body above the plate is not standing on it */
    a.x = 10.0f; a.y = 6.0f; a.z = 0.0f;
    widget_runtime_tick(&rt, &a, 1, reg_sink, box_sink, NULL);
    ok(rt.c.n[3].power == wg_off(), "a body six units above the plate does not press it");
    a.y = 0.0f;

    /* ---- the terminal: F opens a panel, and the setpoint survives the tick ------------------ */
    a.x = -10.0f; a.z = 0.0f;
    ok(widget_runtime_focus(&rt, a.x, a.y, a.z, 0, 16) == 6, "the terminal is in reach");
    ok(widget_runtime_use(&rt) == 1 && widget_runtime_panel_open(&rt), "F on a terminal opens its panel");
    ok(widget_runtime_panel_node(&rt) && strcmp(widget_runtime_panel_node(&rt)->id, "term_g") == 0,
       "and the panel is that terminal");
    ok(widget_runtime_panel_key(&rt, WIDGET_KEY_UP) == 1, "UP is consumed by the panel");
    widget_runtime_panel_key(&rt, WIDGET_KEY_UP);
    widget_runtime_panel_key(&rt, WIDGET_KEY_UP);
    ok(rt.c.n[6].panel_value == 1500, "UP dials the setpoint in 500s");

    sinks_reset();
    for (int i = 0; i < 5; i++) widget_runtime_tick(&rt, &a, 1, reg_sink, box_sink, NULL);
    ok(widget_circuit_reg_value(&rt.c, 16) == 1500,
       "five ticks later the register a script reads still holds the setpoint");
    ok(g_reg_last_reg == 16 && g_reg_last_value == 1500, "the mirror reported that register at that value");
    ok(g_reg16_events == 1, "and only once -- a mirror fires on change, not every tick");

    ok(widget_runtime_panel_key(&rt, WIDGET_KEY_BACK) == 1 && !widget_runtime_panel_open(&rt),
       "BACK hands the world back");
    ok(widget_runtime_panel_key(&rt, WIDGET_KEY_UP) == 0, "a closed panel swallows nothing");
    ok(widget_circuit_reg_value(&rt.c, 16) == 1500, "closing the panel does not reset the machine");

    /* ---- the prompt: busy suppresses it, walking away releases it -------------------------- */
    ok(widget_runtime_focus(&rt, 0.0f, 0.0f, 0.0f, 0, 16) == 0, "back in reach, the lever focuses again");
    ok(widget_runtime_focus(&rt, 200.0f, 0.0f, 200.0f, 0, 16) == 0,
       "walking away keeps the last focus inside the hold window");
    ok(widget_runtime_focus(&rt, 200.0f, 0.0f, 200.0f, 0, 400) == -1,
       "and releases it once the window lapses");
    ok(widget_runtime_focus(&rt, 0.0f, 0.0f, 0.0f, 1, 400) == -1,
       "a busy host (phone, Orb, vehicle seat) suppresses the prompt");

    /* ---- a level with no widgets is a no-op, not a crash ----------------------------------- */
    ok(level_boxes_parse_json(EMPTY_JSON, &empty), "a plain level parses");
    ok(widget_runtime_load(&rt, &empty) == 0, "and loads no nodes");
    ok(widget_runtime_tick(&rt, &a, 1, reg_sink, box_sink, NULL) == 0, "ticking it is a no-op");
    ok(widget_runtime_focus(&rt, 0.0f, 0.0f, 0.0f, 0, 16) == -1 && widget_runtime_use(&rt) == 0,
       "focus and use are no-ops too");

    printf("\n%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
