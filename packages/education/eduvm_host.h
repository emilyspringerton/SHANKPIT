/* eduvm_host.h -- host half of PARENA/stdlib/shankpit/eduvm.prn (card #494). The mod calls these
 * eduvm_host_* functions; the generated eduvm_mod.c is compiled with -include of this header.
 * One global EduScriptSystem (the Architect's Orb terminal state) lives in eduvm_host.c. */
#ifndef EDUVM_HOST_H
#define EDUVM_HOST_H
#include "edu_script.h"

EduScriptSystem *eduvm_system(void);
int eduvm_host_slot_count(void);
int eduvm_host_seed_slot(int slot, int preset);
int eduvm_host_compile_slot(int slot);
int eduvm_host_run_slot(int slot);
int eduvm_host_slot_compiled(int slot);
/* which: 0 gate_open 1 bridge_raised 2 portal_open 3 portal_stability 4 crate_x 5 last_print */
int eduvm_host_world(int which);
void eduvm_host_reset_world(void);

/* generated mod entry points (eduvm_mod.c) */
int edu_trial_step(int slot);
int edu_trial_complete(int gate, int bridge, int portal_open, int stability);
int edu_seed_slot(int slot, int preset);
int edu_compile_slot(int slot);
int edu_run_slot(int slot);
int edu_slot_count(void);
int edu_slot_compiled(int slot);
int edu_gate_open(void);
int edu_bridge_raised(void);
int edu_portal_open(void);
int edu_portal_stability(void);
int edu_crate_x(void);
int edu_last_print(void);
void edu_reset_world(void);
#endif
