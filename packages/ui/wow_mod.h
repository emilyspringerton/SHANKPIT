/* WOW MOD SURFACE -- what a mod is allowed to give the macro engine, and nothing more.
 *
 * A mod registers two things, both by NAME and both bounded:
 *   - a MACRO: text compiled ONCE at registration into the same WowMacroLine the wire ships as bytes,
 *     with an id the client can invoke (5 bytes) instead of re-sending the text;
 *   - a SPELL ALIAS: a name a player can type in /cast that resolves to one of the engine's own weapon
 *     ids (wow_weapons.h, generated from protocol.h).
 *
 * Refusals are explicit and testable: text that does not compile is refused (0), a duplicate name or
 * duplicate id is refused (-1), an alias for a weapon the engine does not have is refused (0). A mod
 * cannot invent a weapon id, cannot shadow another mod's macro name, and cannot register a macro the
 * compiler rejects -- which is the whole security story here: the surface is a table, not a VM.
 */
#ifndef SHANKPIT_UI_WOW_MOD_H
#define SHANKPIT_UI_WOW_MOD_H

#include <string.h>
#include "wow_macro.h"
#include "wow_weapons.h"

#define WOW_MOD_NAME_MAX    24
#define WOW_MOD_MACROS_MAX  16
#define WOW_MOD_SPELLS_MAX  16

typedef struct {
    char          name[WOW_MOD_NAME_MAX];
    unsigned char id;          /* 1..255: the wire's macro id (WOW_OP_DEFINE_MACRO) */
    WowMacroLine  line;
} WowModMacro;

typedef struct {
    char name[WOW_MOD_NAME_MAX];
    int  weapon;               /* a WPN_* id from the generated table */
} WowModSpell;

typedef struct {
    WowModMacro macros[WOW_MOD_MACROS_MAX];
    int         macro_count;
    WowModSpell spells[WOW_MOD_SPELLS_MAX];
    int         spell_count;
} WowModRegistry;

static inline void wow_mod_begin(WowModRegistry *r) { if (r) memset(r, 0, sizeof *r); }

static inline int wow_mod_name_eq(const char *a, const char *b) {
    size_t i;
    if (!a || !b) return 0;
    for (i = 0; a[i] && b[i]; i++)
        if (wow_wpn_lower(a[i]) != wow_wpn_lower(b[i])) return 0;
    return a[i] == '\0' && b[i] == '\0';
}

/* 1 registered, 0 the mod's text does not compile (or empty name / id 0), -1 a duplicate or a full
   table. The compiler's own rejection codes stay the compiler's business; a mod gets a yes or a no. */
static inline int wow_mod_register_macro(WowModRegistry *r, const char *name, unsigned char id,
                                         const char *text) {
    WowMacroLine line;
    int i;
    if (!r || !name || !*name || strlen(name) >= WOW_MOD_NAME_MAX || id == 0) return 0;
    if (wow_macro_parse(text, &line) != 1) return 0;
    for (i = 0; i < r->macro_count; i++)
        if (wow_mod_name_eq(r->macros[i].name, name) || r->macros[i].id == id) return -1;
    if (r->macro_count >= WOW_MOD_MACROS_MAX) return -1;
    {
        WowModMacro *m = &r->macros[r->macro_count++];
        size_t n = strlen(name);
        memset(m, 0, sizeof *m);
        memcpy(m->name, name, n + 1);
        m->id = id;
        m->line = line;
    }
    return 1;
}

static inline const WowModMacro *wow_mod_macro_by_name(const WowModRegistry *r, const char *name) {
    int i;
    if (!r || !name) return NULL;
    for (i = 0; i < r->macro_count; i++)
        if (wow_mod_name_eq(r->macros[i].name, name)) return &r->macros[i];
    return NULL;
}

static inline const WowModMacro *wow_mod_macro_by_id(const WowModRegistry *r, unsigned char id) {
    int i;
    if (!r) return NULL;
    for (i = 0; i < r->macro_count; i++)
        if (r->macros[i].id == id) return &r->macros[i];
    return NULL;
}

/* 1 registered, 0 unknown weapon / empty alias, -1 duplicate or full. */
static inline int wow_mod_register_spell(WowModRegistry *r, const char *alias, const char *weapon_name) {
    int w = wow_weapon_from_name(weapon_name), i;
    if (!r || !alias || !*alias || strlen(alias) >= WOW_MOD_NAME_MAX || w < 0) return 0;
    for (i = 0; i < r->spell_count; i++)
        if (wow_mod_name_eq(r->spells[i].name, alias)) return -1;
    if (r->spell_count >= WOW_MOD_SPELLS_MAX) return -1;
    {
        WowModSpell *s = &r->spells[r->spell_count++];
        size_t n = strlen(alias);
        memset(s, 0, sizeof *s);
        memcpy(s->name, alias, n + 1);
        s->weapon = w;
    }
    return 1;
}

/* A mod alias first, then the engine's own weapon names, then -1. /cast goes through exactly this. */
static inline int wow_mod_spell_weapon(const WowModRegistry *r, const char *name) {
    int i;
    if (!name) return -1;
    if (r) {
        for (i = 0; i < r->spell_count; i++)
            if (wow_mod_name_eq(r->spells[i].name, name)) return r->spells[i].weapon;
    }
    return wow_weapon_from_name(name);
}

#endif /* SHANKPIT_UI_WOW_MOD_H */
