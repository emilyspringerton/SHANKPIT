/* apps/game/core_main.c -- the native core with NO networking and NO SDL.
 *
 * This is the "ship the core without the networking mod" half of the porting ladder. It links the
 * same PARENA rules, REFLUX log and level-module registry as the full client, but deliberately not
 * apps/game/host.c: no sockets, no window, no event loop. A full client is host.c + the host-C UDP
 * transport; this binary is what remains when that transport is not linked.
 *
 * It is a registry runner, not a playable game: register a level, optionally change a module, print
 * what the rules decided. That is enough to prove the core does not secretly depend on networking.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game_api.h"
#include "level_mods_host.h"
#include "reflux_runtime.h"

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s [--level N] [--env surface|underground|moon] [--set-mod MOD 0|1]"
            " [--capabilities MASK]\n",
            argv0);
}

int main(int argc, char **argv) {
    int level = 1;
    int env = lm_env_surface();
    int set_mod = 0;
    int set_on = 0;
    int caps = 0;
    int caps_set = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--level") == 0 && i + 1 < argc) {
            level = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--env") == 0 && i + 1 < argc) {
            const char *v = argv[++i];
            if (strcmp(v, "surface") == 0) env = lm_env_surface();
            else if (strcmp(v, "underground") == 0) env = lm_env_underground();
            else if (strcmp(v, "moon") == 0) env = lm_env_moon();
            else env = atoi(v);
        } else if (strcmp(argv[i], "--set-mod") == 0 && i + 2 < argc) {
            set_mod = atoi(argv[++i]);
            set_on = atoi(argv[++i]) != 0;
        } else if (strcmp(argv[i], "--capabilities") == 0 && i + 1 < argc) {
            caps = atoi(argv[++i]);
            caps_set = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    level_mods_reset();
    reflux_host_reset();
    if (caps_set) level_mods_set_capabilities(caps);
    else level_mods_set_capabilities(bp_target_capabilities(bp_target_native_core()));

    if (level <= 0 || !lm_env_valid(env)) {
        fprintf(stderr, "shank_core: bad level %d / env %d\n", level, env);
        return 2;
    }
    if (!level_mods_register(level, env)) {
        fprintf(stderr, "shank_core: level %d could not register\n", level);
        return 2;
    }
    if (set_mod && !level_mods_set(level, set_mod, set_on)) {
        fprintf(stderr, "shank_core: --set-mod %d %d refused\n", set_mod, set_on);
        return 3;
    }

    printf("shank_core target=native-core net=none caps=%d level=%d env=%d mods=%d rev=%d\n",
           level_mods_capabilities(), level, level_mods_env(level),
           level_mods_mask(level), level_mods_revision(level));
    return 0;
}
