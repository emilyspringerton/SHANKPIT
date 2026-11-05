/* packages/game/level_mods_host.h -- the live table behind packages/game/level_mods.prn.
 *
 * The rules own every decision (default masks, whether a module may be turned off, the three REFLUX
 * row numbers); this host owns only what a pure function cannot: which levels are registered RIGHT NOW,
 * their current mask, their revision, and how far each one has read the shared REFLUX log.
 *
 * Two directions, deliberately separate:
 *   request -> dispatch a REFLUX row (the server's update, "these are the mods this level should run")
 *   sync    -> apply the rows this level has not seen yet (the subscriber half)
 *
 * level_mods_set() is the convenience both halves in order, for a caller that wants the update to take
 * effect before it returns. The per-tick host path calls level_mods_sync() so an update that arrived
 * from anywhere else in the process still lands without the level being reloaded.
 */
#ifndef SHANKPIT_LEVEL_MODS_HOST_H
#define SHANKPIT_LEVEL_MODS_HOST_H

/* Eight live levels is enough for a server's hub + a handful of loaded neighbourhoods, and small
 * enough that the table can be a plain static array (VS0's runtime has no allocation path here). */
#define LEVEL_MODS_MAX_LEVELS 8

/* Register a level with its environment. Idempotent: registering the same id twice keeps the existing
 * mask/revision rather than resetting a level that is already live. Returns the opening mask, or 0 on
 * a bad id, unknown environment, or a full table. Dispatches REFLUX_ACTION_LEVEL_REGISTERED. */
int level_mods_register(int level_id, int env);

/* Remove a live level from the table and free its slot. Dispatches
 * REFLUX_ACTION_LEVEL_UNREGISTERED with the last mask/revision, so the event stream can rebuild the
 * removal and a mod can react to it. Returns 1 when the level was live, 0 for an unknown id. */
int level_mods_unregister(int level_id);

/* Ask for one module to change on one level: validates the request and dispatches
 * REFLUX_ACTION_LEVEL_MOD_SET. Does NOT mutate the table -- sync does that, so the log stays the one
 * source of truth for updates. Returns 1 when the row was dispatched, 0 when refused (unknown level or
 * module, or an attempt to disable a mandatory module). */
int level_mods_request(int level_id, int mod, int on);

/* Apply a module change directly, the way sync does once it has read a row. Idempotent: a row that
 * changes nothing does not bump the revision and returns 0. Returns 1 when the mask changed. */
int level_mods_apply(int level_id, int mod, int on);

/* Apply an authoritative full state from outside this process (the server's network relay). The mask
 * is sanitised first, and a revision older than the level's current one is ignored -- a replayed
 * packet cannot roll a live level back. Returns 1 when the mask changed. */
int level_mods_apply_state(int level_id, int mask, int revision);

/* Read this level's unseen REFLUX rows and apply its updates. Returns the number of changes applied.
 * Safe after reflux_host_reset(): a cursor past the new log's end is clamped instead of reading
 * another level's rows. */
int level_mods_sync(int level_id);

/* request + sync. Returns 1 when the change took effect, 0 when it was refused or made no change. */
int level_mods_set(int level_id, int mod, int on);

/* Re-emit this level's full state (REFLUX_ACTION_LEVEL_STATE) so a log reader that joined after the
 * level was registered, or after a log reset, can reconstruct it. Returns 0 for an unknown level. */
int level_mods_publish(int level_id);

void level_mods_reset(void);
int level_mods_count(void);

/* The modules THIS BINARY has ported. Defaults to every module. A WASM/core build sets a narrower
 * mask before registering anything: opening masks are intersected with it, incoming authoritative
 * state can never enable an unported module, and a request to enable one is refused. Mandatory
 * modules are always kept, and the capability mask is a property of the build, so it survives
 * level_mods_reset(). Returns the effective mask. */
int level_mods_set_capabilities(int mask);
int level_mods_capabilities(void);
int level_mods_level_at(int index); /* -1 past the table */

int level_mods_env(int level_id);      /* -1 for an unknown level */
int level_mods_mask(int level_id);     /* 0 for an unknown level */
int level_mods_revision(int level_id); /* 0 for an unknown level */
int level_mods_enabled(int level_id, int mod);

/* The two render gates the host asks every frame. Both false for an unknown level, so a level that
 * failed to register draws the conservative surface (no weather) rather than inventing one. */
int level_mods_weather_enabled(int level_id);
int level_mods_sky_enabled(int level_id);

#endif /* SHANKPIT_LEVEL_MODS_HOST_H */
