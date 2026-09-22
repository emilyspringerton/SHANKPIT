/* reflux_mod_host.h -- real extern declarations for reflux_mod.c's own 6 exported entry points.
 * Ported in from ECOWAR (packages/reflux/reflux_mod_host.h) as part of the BIG_O/PARENA engine
 * merge (founder real-time: "parena powered bring in reflux into shankpit") -- same "-include this
 * header before compiling the generated C" pattern every mod host header in that repo already
 * establishes; SHANKPIT's own reflux_runtime.c/.h (S485) already implements the exact
 * reflux_host_* contract PARENA/stdlib/reflux/reflux.prn's own #target inline-c bodies expect, so
 * this pass is the missing wire-up, not new host code.
 *
 * All 6 are pure, thin wrappers calling straight into reflux_runtime.c's own real host functions
 * (reflux_host_*) -- this mod IS the interface, reflux_runtime.c does the real work, same split
 * ECOWAR's own copy documents.
 */
#ifndef REFLUX_MOD_HOST_H
#define REFLUX_MOD_HOST_H

/* Pulls in RefluxLog/RefluxAction and the real REFLUX_ACTION_* constants too -- any file that
 * includes this header gets both the real reflux_dispatch/etc. prototypes below AND the shared
 * action-type constants for free, without a second include line at every call site. */
#include "reflux_runtime.h"

extern void reflux_dispatch(int action_type, int a, int b, int c);
extern int reflux_log_size(void);
extern int reflux_action_type_at(int index);
extern int reflux_action_a_at(int index);
extern int reflux_action_b_at(int index);
extern int reflux_action_c_at(int index);

#endif /* REFLUX_MOD_HOST_H */
