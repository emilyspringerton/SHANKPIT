/* edu_widgets_test.c -- level-authored programmable widgets driven by EduVM scripts.
 * Build: gcc -Wall -Wextra -o /tmp/edu_widgets_test packages/education/edu_widgets_test.c \
 *   packages/education/edu_lexer.c packages/education/edu_bytecode.c packages/education/edu_parser.c \
 *   packages/education/edu_vm.c packages/education/edu_bindings.c   (or: make test-edu-widgets) */
#include <stdio.h>
#include <string.h>
#include "edu_parser.h"
#include "edu_vm.h"
#include "edu_bindings.h"

static int failures = 0;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); failures++; } else printf("ok:   %s\n", m); } while (0)

/* returns 1 on compile+run success; *vm_err holds the VM error flag on a runtime failure */
static int run(const char *src, EduWorldState *w, int *compiled) {
    static unsigned char bc[EDU_MAX_BYTECODE];
    EduCompileOutput out;
    memset(&out, 0, sizeof out);
    out.bytecode = bc; out.bytecode_cap = sizeof bc;
    *compiled = edu_compile_source(src, &out);
    if (!*compiled) { printf("      compile error: %s\n", out.error); return 0; }
    EduVmProgramMeta meta = { out.strings, out.string_count };
    EduVm vm; edu_vm_init(&vm);
    EduVmLimits lim; memset(&lim, 0, sizeof lim);
    lim.max_instructions = 5000; lim.max_stack = EDU_VM_STACK_MAX; lim.max_vars = EDU_VM_VAR_MAX;
    char msg[256];
    return edu_vm_exec(&vm, out.bytecode, out.bytecode_len, &meta, w, &lim, msg, sizeof msg);
}

int main(void) {
    EduWorldState w;
    memset(&w, 0, sizeof w);
    int c;

    /* bind: plate on reg 1, three gates on regs 2..4 */
    CHECK(edu_widget_bind(&w, "plate_a", EDU_WIDGET_PLATE, 0, 1, 0, 0, 8, 1.5f) == 0, "bind plate -> slot 0");
    CHECK(edu_widget_bind(&w, "gate_1", EDU_WIDGET_GATE, 0, 2, 0, 0, 16, 0) == 1, "bind gate_1");
    CHECK(edu_widget_bind(&w, "gate_2", EDU_WIDGET_GATE, 0, 3, 0, 0, 24, 0) == 2, "bind gate_2");
    CHECK(edu_widget_bind(&w, "gate_3", EDU_WIDGET_GATE, 1, 4, 0, 0, 32, 0) == 3, "bind gate_3 (starts open)");
    CHECK(edu_widget_bind(&w, "dup_reg", EDU_WIDGET_GATE, 0, 2, 0, 0, 0, 0) == -1, "a second widget on a taken register is rejected");
    CHECK(edu_widget_bind(&w, "", EDU_WIDGET_GATE, 0, 9, 0, 0, 0, 0) == -1, "empty id rejected");
    CHECK(edu_widget_bind(&w, "bad", 99, 0, 9, 0, 0, 0, 0) == -1, "unknown machine type rejected");
    CHECK(edu_widget_bind(&w, "gate_1", EDU_WIDGET_GATE, 1, 2, 0, 0, 16, 0) == 1 && w.widget_count == 4, "re-binding an id updates in place");
    edu_widget_bind(&w, "gate_1", EDU_WIDGET_GATE, 0, 2, 0, 0, 16, 0);

    /* by-name write, by-name read */
    CHECK(run("set_state(\"gate_1\", 1);", &w, &c) && edu_widget_find_id(&w, "gate_1")->state == 1, "set_state by id opens gate_1");
    CHECK(run("print(read_state(\"gate_1\") + read_state(\"gate_2\"));", &w, &c) && w.last_print == 1, "read_state by id");
    CHECK(run("set_state(\"nope\", 1);", &w, &c) == 0 && c == 1, "set_state on an unknown id is a runtime failure");

    /* register-addressed */
    CHECK(run("set_reg(3, 1);", &w, &c) && edu_widget_find_id(&w, "gate_2")->state == 1, "set_reg opens the widget on that register");
    CHECK(run("print(read_sensor(4));", &w, &c) && w.last_print == 1, "read_sensor reads gate_3's initial state 1");
    CHECK(run("print(read_sensor(77));", &w, &c) && w.last_print == 0, "an unwired register reads 0");

    /* sensors are host-written only: a script cannot forge a plate */
    CHECK(run("set_reg(1, 1);", &w, &c) == 0 && edu_widget_find_id(&w, "plate_a")->state == 0, "set_reg on a plate fails, state untouched");
    CHECK(run("set_state(\"plate_a\", 1);", &w, &c) == 0 && edu_widget_find_id(&w, "plate_a")->state == 0, "set_state on a plate fails");
    CHECK(edu_widget_host_set(&w, "plate_a", 1) && edu_widget_find_id(&w, "plate_a")->state == 1, "the host can drive the plate");

    /* the tutorial sequencer: while-loop over consecutive gate registers, gated on the plate */
    edu_widgets_reset(&w);
    const char *seq =
        "let r = 2;\n"
        "while r < 5 {\n"
        "  if read_sensor(1) {\n"
        "    set_reg(r, 1);\n"
        "  }\n"
        "  r = r + 1;\n"
        "}\n";
    CHECK(run(seq, &w, &c) && edu_widget_find_reg(&w, 2)->state == 0 && edu_widget_find_reg(&w, 3)->state == 0,
          "sequencer with the plate released leaves gates shut");
    edu_widget_host_set(&w, "plate_a", 1);
    CHECK(run(seq, &w, &c) && edu_widget_find_reg(&w, 2)->state == 1 && edu_widget_find_reg(&w, 3)->state == 1 &&
          edu_widget_find_reg(&w, 4)->state == 1, "sequencer with the plate held opens gates on regs 2,3,4");

    /* reset returns every widget to its authored initial state */
    edu_widgets_reset(&w);
    CHECK(edu_widget_find_id(&w, "gate_1")->state == 0 && edu_widget_find_id(&w, "gate_3")->state == 1 &&
          edu_widget_find_id(&w, "plate_a")->state == 0, "reset restores initial states");

    /* clear */
    edu_widgets_clear(&w);
    CHECK(w.widget_count == 0 && edu_widget_find_id(&w, "gate_1") == NULL, "clear empties the table");
    CHECK(run("print(read_sensor(2));", &w, &c) && w.last_print == 0, "scripts against a widget-less level read 0, not crash");

    printf(failures ? "\n%d FAILED\n" : "\nAll edu widget checks passed.\n", failures);
    return failures ? 1 : 0;
}
