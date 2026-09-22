/* reflux_mod_test.c -- real test for reflux_mod.c (PARENA-generated, from PARENA/stdlib/reflux/
 * reflux.prn) round-tripping through reflux_runtime.c's own host log. BIG_O/PARENA engine merge
 * (founder real-time: "parena powered bring in reflux into shankpit") -- reflux_runtime.c's
 * reflux_host_* functions already implemented the exact contract the generated file expects
 * (S485); this test verifies the wire-up actually works, not just compiles. Same real, plain
 * assert()-free CHECK() harness ECOWAR's own tests/test_reflux.c uses (no SDL/GL dependency).
 *
 * Build and run:
 *   gcc -Wall -Wextra -O2 -Ipackages/reflux -o /tmp/reflux_mod_test \
 *       packages/reflux/reflux_mod_test.c packages/reflux/reflux_mod.c \
 *       packages/reflux/reflux_runtime.c && /tmp/reflux_mod_test
 */
#include <stdio.h>

#include "reflux_mod_host.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

int main(void) {
    reflux_host_reset();
    CHECK(reflux_log_size() == 0, "a freshly reset REFLUX log starts empty");

    reflux_dispatch(REFLUX_ACTION_BUTTON_PRESSED, 7, 1, 0);
    CHECK(reflux_log_size() == 1, "reflux_dispatch (PARENA-generated) grows the real host log");
    CHECK(reflux_action_type_at(0) == REFLUX_ACTION_BUTTON_PRESSED, "the dispatched action's real type round-trips");
    CHECK(reflux_action_a_at(0) == 7 && reflux_action_b_at(0) == 1, "the dispatched action's real payload round-trips");

    reflux_dispatch(REFLUX_ACTION_LOOK_AT, 3, 1, 250);
    CHECK(reflux_log_size() == 2, "a second dispatch appends rather than overwrites");
    CHECK(reflux_action_c_at(1) == 250, "a third payload scalar (c) round-trips too");

    printf(failures == 0 ? "ALL PASS\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
