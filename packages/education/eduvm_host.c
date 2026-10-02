/* eduvm_host.c -- the host state + seed presets behind stdlib/shankpit/eduvm.prn (card #494). */
#include <stdio.h>
#include <string.h>
#include "eduvm_host.h"

static EduScriptSystem g_sys;
static int g_inited = 0;

EduScriptSystem *eduvm_system(void) {
    if (!g_inited) { edu_script_init(&g_sys); g_inited = 1; }
    return &g_sys;
}

static const char *const PRESET_NAME[5] = { "Architect Trial", "Orb Scan", "Legacy Motion", "Bubble Sort", "Empty" };
static const char *const PRESET_SRC[5] = {
    "let stability = scan_portal();\n"
    "if stability < 100 {\n"
    "  stabilize_portal(100);\n"
    "}\n"
    "raise_bridge();\n"
    "open_gate();\n"
    "open_portal();\n",
    "let gate = scan_gate();\n"
    "let portal = scan_portal();\n"
    "let enemies = scan_enemy_count();\n"
    "print(gate + portal + enemies);\n",
    "set_crate_speed(2);\n"
    "move_crate();\n"
    "print(7);\n",
    "let arr = array(5);\n"
    "arr[0] = 5; arr[1] = 3; arr[2] = 4; arr[3] = 1; arr[4] = 2;\n"
    "let i = 0;\n"
    "while (i < 5) {\n"
    "  let j = 0;\n"
    "  while (j < 5 - i - 1) {\n"
    "    if (arr[j] > arr[j+1]) {\n"
    "      let t = arr[j]; arr[j] = arr[j+1]; arr[j+1] = t;\n"
    "    }\n"
    "    j = j + 1;\n"
    "  }\n"
    "  i = i + 1;\n"
    "}\n"
    "print(arr[0]);\n",
    "" };

static EduScriptSlot *slot_at(int slot) {
    EduScriptSystem *s = eduvm_system();
    if (slot < 0 || slot >= EDU_MAX_SCRIPTS) return NULL;
    return &s->slots[slot];
}

int eduvm_host_slot_count(void) { return EDU_MAX_SCRIPTS; }

int eduvm_host_seed_slot(int slot, int preset) {
    EduScriptSlot *sl = slot_at(slot);
    if (!sl || preset < 0 || preset > 4) return 0;
    snprintf(sl->name, sizeof sl->name, "%s", PRESET_NAME[preset]);
    snprintf(sl->source, sizeof sl->source, "%s", PRESET_SRC[preset]);
    sl->source_len = (int)strlen(sl->source);
    sl->compile_ok = 0;
    return 1;
}

int eduvm_host_compile_slot(int slot) {
    EduScriptSystem *s = eduvm_system();
    if (!slot_at(slot)) return 0;
    int keep = s->active_slot;
    s->active_slot = slot;
    int ok = edu_script_compile_active(s);
    s->active_slot = keep;
    return ok ? 1 : 0;
}

int eduvm_host_run_slot(int slot) {
    EduScriptSystem *s = eduvm_system();
    if (!slot_at(slot)) return 0;
    int keep = s->active_slot;
    s->active_slot = slot;
    int ok = edu_script_run_active(s);
    s->active_slot = keep;
    return ok ? 1 : 0;
}

int eduvm_host_slot_compiled(int slot) { EduScriptSlot *sl = slot_at(slot); return sl ? sl->compile_ok : 0; }

int eduvm_host_world(int which) {
    EduWorldState *w = &eduvm_system()->world;
    switch (which) {
        case 0: return w->gate_open;
        case 1: return w->bridge_raised;
        case 2: return w->portal_open;
        case 3: return w->portal_stability;
        case 4: return w->crate_x;
        case 5: return w->last_print;
        default: return 0;
    }
}

void eduvm_host_reset_world(void) {
    EduScriptSystem *s = eduvm_system();
    EduWorldState keep_caps = s->world;
    memset(&s->world, 0, sizeof s->world);
    s->world.crate_speed = 1;
    s->world.grounded_test_platform = 1;
    s->world.portal_stability = 25;
    s->world.enemy_count = 1;
    s->world.can_open_gate = keep_caps.can_open_gate;
    s->world.can_raise_bridge = keep_caps.can_raise_bridge;
    s->world.can_modify_portal = keep_caps.can_modify_portal;
    s->world.can_affect_enemies = keep_caps.can_affect_enemies;
}
