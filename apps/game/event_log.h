/* apps/game/event_log.h -- stream REFLUX level-module rows to a JSONL file.
 *
 * The shared REFLUX log already carries every level update (packages/reflux/reflux_runtime.h rows
 * 140..142), but it is a 256-row ring in memory. This is the host-side tail: once per tick it writes
 * the level family's new rows as one JSON object per line, so a server's mod changes survive the
 * process and can be replayed or audited.
 *
 * Only the level family is streamed. This is deliberately not a general event sink: the point is a
 * small, stable record of "this level changed these mods at this tick", not a second copy of REFLUX.
 *
 * Lines look like:
 *   {"seq":12,"tick":240,"event":"registered","level":2,"env":1,"mask":11}
 *   {"seq":13,"tick":241,"event":"mod_set","level":2,"mod":3,"on":1}
 *   {"seq":14,"tick":242,"event":"state","level":2,"mask":15,"revision":2}
 *   {"seq":-1,"tick":320,"event":"log_reset"}
 *
 * `seq` is the row's absolute REFLUX dispatch index, so two log streams can be compared. A reset is
 * written once when the cursor is found past the end of a newly reset log.
 */
#ifndef SHANKPIT_EVENT_LOG_H
#define SHANKPIT_EVENT_LOG_H

/* Open (append) the stream. Returns 1 on success, 0 when the path cannot be opened. The cursor starts
 * at the current end of the REFLUX log, so opening mid-run does not replay history. */
int event_log_open(const char *path);
void event_log_close(void);

/* Start reading the log from its current beginning. Call after reflux_host_reset() at the start of a
 * round, so that round's level rows are streamed from row 0. */
void event_log_reset_cursor(void);

/* Write the level rows that appeared since the last call. Returns the number of rows written. */
int event_log_sync(int tick);

/* Replay a stream produced by event_log_sync() into the live registry.
 *
 * This is the recovery path: a fresh server can rebuild which mods each level runs without the
 * original process. Registered rows register missing levels, mod_set rows go through the same
 * level_mods_set() path a live update uses, state rows are applied forward-only, and log_reset clears
 * the registry before the stream continues. Blank or unrecognised lines are skipped.
 *
 * Returns the number of rows that changed state (>= 0), or -1 when the file cannot be opened. */
int event_log_replay(const char *path);

#endif /* SHANKPIT_EVENT_LOG_H */
