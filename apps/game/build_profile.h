/* apps/game/build_profile.h -- the one place a client asks "what build am I?".
 *
 * packages/game/build_profiles.prn owns the table (caps + network transport). This glue is compiled
 * into every client with SHANKPIT_BUILD_TARGET naming that target's bp_target_* function, so callers
 * apply and report a profile instead of hard-coding a target name in host.c / core_main.c / wasm_core.c.
 */
#ifndef SHANKPIT_BUILD_PROFILE_H
#define SHANKPIT_BUILD_PROFILE_H

int build_profile_target(void);
const char *build_profile_name(void);
int build_profile_net(void);
const char *build_profile_net_name(void);

/* Applies this build's capability mask to the level registry and returns it. Call before registering
 * any level; an explicit --capabilities override still wins in the two registry runners. */
int build_profile_apply(void);

#endif
