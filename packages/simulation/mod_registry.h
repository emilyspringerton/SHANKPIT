#ifndef MOD_REGISTRY_H
#define MOD_REGISTRY_H

/* mod_registry.h -- the C host's mod loader for shield-regen curves.
 *
 * shield_regen.h can already ask a host-installed hook what a tick is worth; this is where those
 * hooks COME FROM. A body is bound to a mod by its player id, the mod's numbers are the
 * PARENA-generated functions in shambler_mod.c (packages/sim/shambler_mod.prn), and the registry
 * installs one trampoline so shield_regen_tick consults the binding for whichever body it is ticking.
 *
 * Why a binding table rather than a species field: the C host has no "what kind of body is this"
 * concept on PlayerState, and inventing one so a registry could guess would put a gameplay rule in a
 * struct. The host that knows -- a mode script, a spawner, a test -- calls mod_registry_bind(id, mod)
 * once per body. Unbound bodies fall through to the PARENA rule, so "no mod" and "a mod with no
 * opinion" behave the same, which is the same fall-through mod_abi.prn already defines.
 *
 * Header-only on purpose, like shield_regen.h: the tree carries no .c implementations (45a4eda), so
 * a host includes this and gets the table and the trampoline in its own translation unit. The table
 * is per-TU, which is correct here because a host that binds a body is also the host that ticks it.
 *
 * HONEST LIMITS: this is a registry, not a sandbox -- a bound mod is a compiled-in function, because
 * this engine has no dynamic loading. And only the shield-regen hook is wired through it so far; the
 * other mod_abi hooks still run in the PARENA host (boot), which is the documented gap in
 * mod_abi.prn's own header. */

#include "protocol.h"
#include "shield_regen.h"
#include "shambler_mod.h"

#define MOD_REGISTRY_MAX_BODIES 64

typedef struct {
    const char *name;                              /* for logs; never parsed */
    int (*shield_regen_amount)(int tick_index);    /* a mod's hook, or NULL for "no opinion" */
} ShieldRegenMod;

/* Per-translation-unit binding table: index is the player id. */
static const ShieldRegenMod *mod_registry_bindings[MOD_REGISTRY_MAX_BODIES];

static inline int mod_registry_slot(int body_id) {
    return (body_id < 0 || body_id >= MOD_REGISTRY_MAX_BODIES) ? -1 : body_id;
}

/* Bind a body to a mod. Returns 1 when the id is in range. NULL unbinds. */
static inline int mod_registry_bind(int body_id, const ShieldRegenMod *m) {
    int slot = mod_registry_slot(body_id);
    if (slot < 0) return 0;
    mod_registry_bindings[slot] = m;
    return 1;
}

static inline void mod_registry_unbind(int body_id) {
    (void)mod_registry_bind(body_id, NULL);
}

static inline const ShieldRegenMod *mod_registry_for_body(int body_id) {
    int slot = mod_registry_slot(body_id);
    return slot < 0 ? NULL : mod_registry_bindings[slot];
}

/* The trampoline shield_regen.h asks every tick: a bound mod's number, or 0 for "no opinion", which
 * shield_regen.h already treats as "use the PARENA rule". */
static inline int mod_registry_shield_regen_amount(const PlayerState *p, int tick_index) {
    const ShieldRegenMod *m = p ? mod_registry_for_body((int)p->id) : NULL;
    if (!m || !m->shield_regen_amount) return 0;
    return m->shield_regen_amount(tick_index);
}

/* Install the trampoline. Call once per host after the first bind (idempotent). */
static inline void mod_registry_install_shield_regen(void) {
    shield_regen_set_amount_hook(mod_registry_shield_regen_amount);
}

/* The mods this host knows, by their PARENA-generated symbols. Adding one is a table entry here plus
 * its .prn's generated C -- no host code changes. */
static inline const ShieldRegenMod *mod_registry_shambler(void) {
    static const ShieldRegenMod m = { "shambler", shambler_on_shield_regen };
    return &m;
}

#endif /* MOD_REGISTRY_H */
