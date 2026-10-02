#include <string.h>
#include <stdio.h>
#include "edu_script.h"

static void seed_script(EduScriptSlot *s, const char *name, const char *src) {
    snprintf(s->name, sizeof(s->name), "%s", name);
    snprintf(s->source, sizeof(s->source), "%s", src);
    s->source_len = (int)strlen(s->source);
}

void edu_script_init(EduScriptSystem *sys) {
    memset(sys, 0, sizeof(*sys));
    sys->world.crate_speed = 1;
    sys->world.grounded_test_platform = 1;
    sys->world.portal_stability = 25;
    sys->world.enemy_count = 1;
    sys->world.rift_hound_active = 1;
    sys->world.rift_hound_hp = 100;
    sys->world.can_open_gate = 1;
    sys->world.can_raise_bridge = 1;
    sys->world.can_modify_portal = 1;
    sys->world.can_affect_enemies = 1;
    sys->world.can_spawn_entities = 0;
    sys->world.puppet_active = 1;
    sys->world.puppet_type = 1;
    sys->world.puppet_x = 64.0f;
    sys->world.puppet_z = 53.0f;
    sys->active_slot = 0;
    seed_script(&sys->slots[0], "Architect Trial Script",
                "let stability = scan_portal();\n"
                "if stability < 100 {\n"
                "  stabilize_portal(50);\n"
                "}\n"
                "raise_bridge();\n"
                "open_gate();\n"
                "open_portal();\n"
                "slow_enemy(0);\n"
                "mark_enemy(0);\n");
    seed_script(&sys->slots[1], "Orb Scan",
                "let gate = scan_gate();\n"
                "let portal = scan_portal();\n"
                "let enemies = scan_enemy_count();\n"
                "print(gate + portal + enemies);\n");
    seed_script(&sys->slots[2], "Legacy Motion",
                "set_crate_speed(2);\n"
                "move_crate();\n"
                "print(7);\n");
    snprintf(sys->compile_status, sizeof(sys->compile_status), "idle");
    snprintf(sys->run_status, sizeof(sys->run_status), "idle");
}

void edu_script_toggle_terminal(EduScriptSystem *sys) {
    sys->terminal_open = !sys->terminal_open;
    printf("[EDU_ORB] %s\n", sys->terminal_open ? "opened" : "closed");
}

void edu_script_reset_active(EduScriptSystem *sys) {
    EduScriptSlot *s = &sys->slots[sys->active_slot];
    s->source[0] = 0;
    s->source_len = 0;
    s->compile_ok = 0;
    snprintf(sys->compile_status, sizeof(sys->compile_status), "reset");
    snprintf(sys->run_status, sizeof(sys->run_status), "reset");
}

void edu_script_insert_text(EduScriptSystem *sys, const char *text) {
    EduScriptSlot *s = &sys->slots[sys->active_slot];
    int len = (int)strlen(text);
    if (s->source_len + len >= EDU_MAX_SCRIPT_TEXT - 1) return;
    memcpy(s->source + s->source_len, text, (size_t)len);
    s->source_len += len;
    s->source[s->source_len] = '\0';
}

void edu_script_backspace(EduScriptSystem *sys) {
    EduScriptSlot *s = &sys->slots[sys->active_slot];
    if (s->source_len <= 0) return;
    s->source_len--;
    s->source[s->source_len] = '\0';
}

void edu_script_newline(EduScriptSystem *sys) { edu_script_insert_text(sys, "\n"); }

int edu_script_compile_active(EduScriptSystem *sys) {
    EduScriptSlot *s = &sys->slots[sys->active_slot];
    EduCompileOutput out;
    memset(&out, 0, sizeof(out));
    out.bytecode = s->bytecode;
    out.bytecode_cap = EDU_MAX_BYTECODE;
    int ok = edu_compile_source(s->source, &out);
    s->compile_ok = ok;
    s->bytecode_len = out.bytecode_len;
    s->string_count = out.string_count;
    for (int i = 0; i < out.string_count; i++) {
        snprintf(s->string_storage[i], sizeof(s->string_storage[i]), "%s", out.string_storage[i]);
        s->strings[i] = s->string_storage[i];
    }
    if (ok) {
        snprintf(sys->compile_status, sizeof(sys->compile_status), "ok (%d bytes)", s->bytecode_len);
        printf("[EDU_VM] compile ok bytes=%d slot=%d\n", s->bytecode_len, sys->active_slot);
    } else {
        snprintf(sys->compile_status, sizeof(sys->compile_status), "error %d:%d %.88s", out.error_line, out.error_col, out.error);
        printf("[EDU_VM] compile error line=%d col=%d msg=%s\n", out.error_line, out.error_col, out.error);
    }
    return ok;
}

int edu_script_run_active(EduScriptSystem *sys) {
    EduScriptSlot *s = &sys->slots[sys->active_slot];
    if (!s->compile_ok) {
        snprintf(sys->run_status, sizeof(sys->run_status), "compile first");
        return 0;
    }
    EduVm vm;
    EduVmLimits lim;
    memset(&lim, 0, sizeof(lim));
    lim.max_instructions = 5000; /* SHANKPIT: was 256 in GFD; array curriculum (bubble sort) needs more headroom */
    lim.max_stack = 128;
    lim.max_vars = 64;
    EduVmProgramMeta meta;
    meta.strings = s->strings;
    meta.string_count = s->string_count;
    int ok = edu_vm_exec(&vm, s->bytecode, s->bytecode_len, &meta, &sys->world, &lim, sys->run_status, (int)sizeof(sys->run_status));
    world_objects_tick(&sys->world, 0);
    if (ok) {
        snprintf(sys->last_output, sizeof(sys->last_output), "%.96s | print=%d", vm.debug_line, sys->world.last_print);
        printf("[EDU_VM] run ok instruction_count=%d\n", vm.instruction_count);
    } else {
        snprintf(sys->last_output, sizeof(sys->last_output), "runtime failure");
        printf("[EDU_VM] run error %s\n", sys->run_status);
    }
    if (sys->world.entity_count > 0) {
        snprintf(sys->last_output, sizeof(sys->last_output), "Crate pos: %.2f", sys->world.entities[0].x);
    }
    sys->last_instruction_count = vm.instruction_count;
    return ok;
}
