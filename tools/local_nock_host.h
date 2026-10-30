/* tools/local_nock_host.h -- prototypes for the thin C host behind tools/local_nock.prn.
 *
 * The generated C from `parena build` emits each `#target` body verbatim, so a body like
 * `ln_state_get(field, dest)` needs a real declaration visible BEFORE it. Same precedent as
 * PARENA's own `tools/ci_status.h`: build the generated file with `-include` this header rather
 * than teaching the emitter a new include form.
 *
 * Only what the VS0 emitter/host split genuinely needs lives here: the arena and env, a session-id
 * generator, plain file/directory access, and the session strings that outlive one request.
 * Routing, JSON shapes, level validation and the git commit are all PARENA in local_nock.prn.
 */
#ifndef LOCAL_NOCK_HOST_H
#define LOCAL_NOCK_HOST_H

#include "parena_runtime.h"

/* Every string-returning entry point returns `char *`, not `const char *`: a PARENA `String`
 * compiles to `char *`, and a `#target` body returning one is emitted verbatim, so a const-qualified
 * host declaration is a real `-Wdiscarded-qualifiers` build failure (found by actually compiling the
 * generated C with -Werror, not assumed). Each one hands back a fresh arena copy, so the caller may
 * treat it as its own. */
Arena *ln_scratch(void);
int ln_port(void);
char *ln_new_id(Arena *dest);
char *ln_state_get(const char *field, Arena *dest);
int ln_state_set(const char *field, const char *value);
char *ln_level_path(const char *name, Arena *dest);
char *ln_read_file(const char *path, Arena *dest);
int ln_write_file(const char *path, const char *text);
char *ln_editor_html(Arena *dest);
int ln_log_event(const char *body);
char *ln_session_json(Arena *dest);
char *ln_levels_json(Arena *dest);

/* nock_run -- the PARENA entry point this host calls from main(). */
int nock_run(Arena *dest);

#endif
