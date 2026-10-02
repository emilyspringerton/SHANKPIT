/* eduvm_test.c -- the PARENA eduvm mod driving the vendored EduVM (card #494). Hand-derived expectations. */
#include <stdio.h>
#include "eduvm_host.h"
#include "../world/parena_runtime.h"
static int failures = 0;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); failures++; } else printf("ok:   %s\n", m); } while (0)

int main(void) {
    CHECK(edu_slot_count() == 8, "8 script slots");
    CHECK(edu_seed_slot(0, 0) == 1 && edu_seed_slot(0, 9) == 0 && edu_seed_slot(99, 0) == 0, "seed accepts valid slot/preset only");
    CHECK(edu_slot_compiled(0) == 0, "freshly seeded slot is not compiled");
    CHECK(edu_portal_stability() == 25 && edu_gate_open() == 0, "world starts: stability 25, gate shut");
    CHECK(edu_trial_complete(1, 1, 1, 100) == 1, "trial rule: all four satisfied -> complete");
    CHECK(edu_trial_complete(1, 1, 1, 99) == 0 && edu_trial_complete(0, 1, 1, 100) == 0 &&
          edu_trial_complete(1, 0, 1, 100) == 0 && edu_trial_complete(1, 1, 0, 100) == 0, "trial rule: any one missing -> not complete");
    CHECK(edu_trial_step(0) == 1, "Architect Trial preset solves the trial via the mod");
    CHECK(edu_gate_open() == 1 && edu_bridge_raised() == 1 && edu_portal_open() == 1 && edu_portal_stability() >= 100, "world mutated by the script");
    edu_reset_world();
    CHECK(edu_gate_open() == 0 && edu_portal_stability() == 25, "reset restores the world");
    CHECK(edu_seed_slot(1, 1) && edu_compile_slot(1) == 1 && edu_run_slot(1) == 1, "Orb Scan compiles and runs");
    CHECK(edu_last_print() == 0 + 25 + 1, "Orb Scan prints gate(0)+portal(25)+enemies(1)=26");
    CHECK(edu_seed_slot(2, 2) && edu_compile_slot(2) && edu_run_slot(2) && edu_crate_x() == 2, "Legacy Motion: speed 2 then one move -> crate x=2");
    CHECK(edu_seed_slot(3, 3) && edu_compile_slot(3) == 1 && edu_run_slot(3) == 1 && edu_last_print() == 1, "Bubble Sort preset sorts [5,3,4,1,2] (arr[0]==1)");
    edu_seed_slot(4, 4);
    CHECK(edu_trial_step(4) == 0, "empty slot never solves the trial");

    printf(failures ? "\n%d FAILED\n" : "\nAll EduVM mod checks passed.\n", failures);
    return failures ? 1 : 0;
}
