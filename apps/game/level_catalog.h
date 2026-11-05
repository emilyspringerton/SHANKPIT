/* apps/game/level_catalog.h -- load a server's level manifest and register its levels.
 *
 * A level's environment (and optional mod override) belongs to the LEVEL, not to whichever command
 * line happened to boot the server. This catalog reads a small JSON manifest, validates it, registers
 * every level in the REFLUX registry, and keeps the names for the HUD/log.
 *
 * Manifest shape (see assets/game/levels.json):
 *   { "levels": [
 *       { "id": 1, "name": "SURFACE YARD", "env": 0 },
 *       { "id": 2, "name": "UNDERGROUND BUNKER", "env": 1 },
 *       { "id": 3, "name": "MOON BUNKER", "env": 2, "mods": 3 }
 *   ] }
 *
 * `mods` is optional. It goes through level_mods_apply_state(), so the mandatory core sim/level bits
 * are restored even if the manifest asks for them to be off. `id` is 1..65535 (the wire row is u16),
 * `env` is 0..2, and `name` is 1..31 characters.
 *
 * Deliberately bounded: at most LEVEL_MODS_MAX_LEVELS entries, at most 64 KB of manifest, no escapes
 * in names. A malformed manifest is refused as a whole and registers NOTHING, so a typo cannot leave
 * the server running half a level list.
 */
#ifndef SHANKPIT_LEVEL_CATALOG_H
#define SHANKPIT_LEVEL_CATALOG_H

#include "level_mods_host.h"

#define LEVEL_CATALOG_NAME_MAX 32

typedef struct {
    int id;
    int env;
    int mask;
    int revision;
    char name[LEVEL_CATALOG_NAME_MAX];
} LevelDesc;

void level_catalog_reset(void);

/* Load and register every level in `path`. Returns the number registered (> 0), or:
 *   -1 file could not be opened
 *   -2 manifest is malformed / too large / too many levels
 *   -3 an entry is invalid (bad id, env, name, or duplicate id)
 * On any failure the registry is left empty and no level is registered. */
int level_catalog_load(const char *path);

int level_catalog_count(void);
const LevelDesc *level_catalog_at(int index); /* NULL past the end */
const LevelDesc *level_catalog_find(int level_id); /* NULL when not in the manifest */

/* Name for a level, or "" when it is not in the catalog. */
const char *level_catalog_name(int level_id);

#endif /* SHANKPIT_LEVEL_CATALOG_H */
