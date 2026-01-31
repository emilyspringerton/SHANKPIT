#ifndef SHANKPIT_MOD_MWGG_H
#define SHANKPIT_MOD_MWGG_H

#include <string.h>

#define MOD_MWGG_MAX_RULES 16
#define MOD_MWGG_MAX_PROFILES 8

typedef struct {
    char id[64];
    char name[128];
    char description[256];
    int weight;
} mod_mwgg_rule_t;

typedef struct {
    char id[64];
    char name[128];
    int rule_count;
    mod_mwgg_rule_t rules[MOD_MWGG_MAX_RULES];
} mod_mwgg_profile_t;

typedef struct {
    mod_mwgg_profile_t profiles[MOD_MWGG_MAX_PROFILES];
    int profile_count;
} ModMwggRegistry;

static inline void mod_mwgg_registry_init(ModMwggRegistry *registry) {
    memset(registry, 0, sizeof(ModMwggRegistry));
}

static inline int mod_mwgg_register_profile(ModMwggRegistry *registry,
                                            const mod_mwgg_profile_t *profile) {
    if (!profile || profile->id[0] == '\0') return -1;
    if (registry->profile_count >= MOD_MWGG_MAX_PROFILES) return -1;
    registry->profiles[registry->profile_count++] = *profile;
    return 0;
}

#endif
