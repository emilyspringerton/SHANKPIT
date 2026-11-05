/* apps/game/build_profile.c -- apply and report the PARENA build-profile table.
 *
 * The target is a compile-time choice, not a runtime guess: game.mk passes one of the bp_target_*
 * names in SHANKPIT_BUILD_TARGET. That keeps every binary reading packages/game/build_profiles.prn
 * for its caps and transport status instead of repeating the target in its own main().
 */
#include "build_profile.h"
#include "game_api.h"
#include "level_mods_host.h"

#ifndef SHANKPIT_BUILD_TARGET
#error "SHANKPIT_BUILD_TARGET must name a bp_target_* function from build_profiles.prn"
#endif

int build_profile_target(void) {
    return SHANKPIT_BUILD_TARGET();
}

const char *build_profile_name(void) {
    int target = build_profile_target();
    if (target == bp_target_native()) return "native";
    if (target == bp_target_llvm()) return "llvm";
    if (target == bp_target_wasm_core()) return "wasm-core";
    if (target == bp_target_native_core()) return "native-core";
    return "unknown";
}

int build_profile_net(void) {
    return bp_target_net(build_profile_target());
}

const char *build_profile_net_name(void) {
    int transport = build_profile_net();
    if (transport == bp_net_none()) return "none";
    if (transport == bp_net_host_c()) return "host-c";
    if (transport == bp_net_parena()) return "parena";
    return "unknown";
}

int build_profile_apply(void) {
    int target = build_profile_target();
    if (!bp_target_valid(target)) return 0;
    return level_mods_set_capabilities(bp_target_capabilities(target));
}
