#include <stdio.h>
#include "../../include/mod_api.h"
#include "../../packages/mods/mod_registry.h"

static int handler_a(mod_hook_t hook, void *payload, void *ctx) {
    (void)hook; (void)payload; (void)ctx;
    return 0;
}

static int handler_b(mod_hook_t hook, void *payload, void *ctx) {
    (void)hook; (void)payload; (void)ctx;
    return 0;
}

int main(void) {
    ModHookRegistry reg;
    mod_registry_init(&reg);
    mod_registry_register(&reg, "a", MOD_HOOK_FRAME, handler_a, NULL, 10);
    mod_registry_register(&reg, "b", MOD_HOOK_FRAME, handler_b, NULL, 20);
    if (reg.count != 2) {
        printf("❌ FAIL: registry count\n");
        return 1;
    }
    mod_registry_dispatch(&reg, MOD_HOOK_FRAME, NULL);
    mod_registry_unregister(&reg, "a");
    if (reg.count != 1) {
        printf("❌ FAIL: unregister\n");
        return 1;
    }
    printf("✅ PASS: mod registry\n");
    return 0;
}
