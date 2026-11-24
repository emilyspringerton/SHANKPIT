/* action_bar_test.c -- host half of the action bar, run against the REAL generated rules (not a mock).
 * Mirrors what the lobby does: key digit + modifiers -> bank/slot -> bind/plugin lookup -> action -> weapon request.
 *   make test-action-bar */
#include <stdio.h>
#include "protocol.h"
#include "action_bar_rules.h"

static int checks = 0, failures = 0;
static void ok(int c, const char *w) { checks++; if (!c) { failures++; printf("FAIL: %s\n", w); } }

/* the pre-bar lobby mapping, verbatim from apps/lobby/src/main.c (SDL_SCANCODE_1..9) */
static int legacy_wpn_for_digit(int d) {
    switch (d) {
        case 1: return 0; case 2: return 1; case 3: return 2; case 4: return 3; case 5: return 4; case 6: return 5;
        case 7: return WPN_FLASHLIGHT; case 8: return WPN_HAMMER; case 9: return WPN_SPRAYCAN;
    }
    return -1;
}

int main(void) {
    int d;
    for (d = 1; d <= 9; d++) {
        int a = ab_resolve(ab_bank_for_mods(0, 0, 0), ab_slot_for_digit(d), 0, 0);
        ok(ab_kind_of(a) == AB_KIND_WEAPON && ab_id_of(a) == legacy_wpn_for_digit(d), "default bank == legacy key mapping");
        ok(ab_id_of(a) < MAX_WEAPONS, "weapon id within MAX_WEAPONS");
    }
    ok(ab_resolve(1, ab_slot_for_digit(0), 0, 0) == 0, "key 0 empty by default");
    for (d = 0; d <= 9; d++) {
        ok(ab_resolve(3, ab_slot_for_digit(d), 0, 0) == 0, "Ctrl bank empty");
        ok(ab_resolve(4, ab_slot_for_digit(d), 0, 0) == 0, "Alt bank empty");
    }
    ok(ab_resolve(2, 1, 0, 0) == 0, "Shift+1 empty w/o plugin");
    ok(ab_resolve(2, 1, 0, ab_plugin_example_action(2, 1)) == ab_pack(AB_KIND_MOD, 1), "example plugin fills Shift+1");
    ok(ab_resolve(2, 1, ab_pack(AB_KIND_WEAPON, WPN_AR), ab_plugin_example_action(2, 1)) == ab_pack(AB_KIND_WEAPON, WPN_AR), "bind beats plugin");
    ok(ab_source(2, 1, 0, ab_plugin_example_action(2, 1)) == AB_SRC_PLUGIN, "source = plugin");
    ok(ab_bank_for_mods(1, 1, 0) == 3 && ab_bank_for_mods(1, 1, 1) == 4, "modifier precedence");
    ok(ab_cycle_slot(10, 1) == 1 && ab_cycle_slot(1, -1) == 10, "scroll wraps");
    ok(ab_resolve(9, 1, 0, 0) == 0 && ab_resolve(1, 99, 0, 0) == 0, "out of range empty");
    printf("action_bar_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
