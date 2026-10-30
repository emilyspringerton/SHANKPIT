/* tools/local_nock_host.h -- prototypes for the thin C host behind tools/local_nock.prn.
 *
 * The generated C emits each `#target` body verbatim, so a body like `ln_level_path(dir, name,
 * dest)` needs a real declaration visible BEFORE it. Same precedent as PARENA's own
 * `tools/ci_status.h`: build with `-include` this header rather than teaching the emitter a new
 * include form.
 *
 * Deliberately only what PARENA cannot do itself: arenas, env, one id generator, and file/
 * directory access. Session ids, the event log, the draft, routing, JSON, validation and the git
 * commit are PARENA in local_nock.prn.
 *
 * Every string-returning entry point returns `char *`, not `const char *`: a PARENA `String`
 * compiles to `char *` and a `#target` body returning one is emitted verbatim, so a const-qualified
 * host declaration is a real -Wdiscarded-qualifiers build failure (found by compiling the generated
 * C with -Werror).
 */
#ifndef LOCAL_NOCK_HOST_H
#define LOCAL_NOCK_HOST_H

#include "parena_runtime.h"

/* Arenas. state is never reset (session state lives there); request is reset after every response;
 * draft is reset on every draft write so a 4 MiB level does not accumulate one copy per edit. */
Arena *ln_state_arena(void);
Arena *ln_request_arena(void);
int ln_request_reset(void);
Arena *ln_draft_arena(void);
int ln_draft_reset(void);

int ln_port(void);
char *ln_env(const char *key, const char *fallback, Arena *dest);
char *ln_new_id(Arena *dest);

char *ln_level_path(const char *dir, const char *name, Arena *dest);
char *ln_read_file(const char *path, Arena *dest);
int ln_write_file(const char *path, const char *text);
char *ln_levels_json(const char *dir, Arena *dest);

/* nock_run -- the PARENA entry point this host calls from main(). */
int nock_run(Arena *dest);

#endif
