#include "../../include/mod_api.h"

static const mod_api_t *api_ref = NULL;
static const char *mod_id_ref = "timed_message";
static unsigned int frame_count = 0;

static int on_frame(mod_hook_t hook, void *payload, void *user_ctx) {
    (void)hook;
    (void)payload;
    (void)user_ctx;
    frame_count++;
    if (frame_count % 300 == 0) {
        api_ref->log(mod_id_ref, 1, "Timed message at frame %u", frame_count);
    }
    return 0;
}

int mod_init(const mod_api_t *api, const char *mod_id) {
    api_ref = api;
    mod_id_ref = mod_id;
    api_ref->register_hook(mod_id_ref, MOD_HOOK_FRAME, on_frame, NULL, 10);
    return 0;
}

void mod_shutdown(void) {
}
