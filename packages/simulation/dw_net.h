#ifndef DW_NET_H
#define DW_NET_H

/* dw_net.h -- THE INTERNET OF SHANKPIT SHIM: the citizens' DEADWEIGHT ladder, published as JSON.
 *
 * Founder real-time, 2026-11-03: "use real wotan we need an internet of shankpit shim" -> "use real
 * iduna and real wotan the local ones but shim me up skotti". The ladder (dw_ladder.h) lives INSIDE
 * the game process and has no outside world; WOTAN is a static site that can only read HTTP; IDUNA
 * is the identity authority. This module is the missing middle piece, and it is deliberately two
 * halves that never call each other:
 *
 *   PRODUCER (in the game)  dw_net_publish() serialises the live ladder and writes it to ONE file,
 *                           write-then-rename, so a reader never sees a half-written snapshot.
 *   CONSUMER (tools/shankpit_netd.c)  a tiny HTTP daemon that serves that file and nothing else.
 *
 * Why a file and not a socket in the game: the game's tick must never block on a web request, a
 * player's machine must not need to open a port to publish its ladder, and the daemon can be
 * restarted, curl'd and tested without a running world. One file is the whole contract, and
 * dw_net_export() is a pure function of the ladder so the JSON itself is testable without either
 * process.
 *
 * OFF BY DEFAULT. Publishing only happens when a snapshot path is set (dw_net_set_path, or the
 * SHANKPIT_NET_SNAPSHOT environment variable, read once). A normal game run writes nothing at all
 * and pays for nothing -- no stat, no allocation, no file. The shim is switched on by the host that
 * wants to serve a world, not imposed on a client.
 *
 * THE SHAPE IS THE CONTRACT (schema 3). Changing a key or a bracket is a schema bump, because the
 * WOTAN page (WOTAN/js/shankpit-net.js) reads it and its node test pins it.
 *
 * Schema 3 added `stage` -- the world's own tournament bracket (dw_stage.h), or `null` when nobody is
 * running one. That is what makes the stage publishable at all: WOTAN reads THIS file and nothing else,
 * so a bracket that lived only in the game could never appear on the site.
 *
 * Schema 2 added `rating` to every record -- the number the ladder climbs
 * (PARENA/stdlib/deadweight/rating.prn, compiled into dw_rating.c). It is NOT accompanied by a
 * pre-sorted ranking array on purpose: the order is (rating desc, wins desc, pid asc), which is a
 * one-line sort a reader can do, and shipping the same fact twice is how two pages start disagreeing
 * about who is first. The WOTAN page sorts; the export stays a record of what the world knows. */

#include <stddef.h>
#include "dw_ladder.h"
#include "dw_stage.h"

#define DW_NET_SCHEMA 3

/* How many finished matches the feed remembers. Newest first; a match older than this is gone, which
 * is fine because WOTAN's real match history is IDUNA's (this is the live ticker, not the archive). */
#define DW_NET_MAX_RECENT 32

/* A citizen's display name, resolved from its pid at export time. Player seats ("you", a coop peer)
 * have no body to read a handle from, so they read as "you" / "player <pid>" on the page. Unset is
 * legal: the export then carries an empty name and the page falls back to the pid. */
void dw_net_set_name_lookup(const char *(*fn)(int pid));
const char *(*dw_net_name_lookup(void))(int pid);

/* THE STAGE. The world owns the bracket (dw_stage.h) and this module only reads it, so there is one
 * bracket and not a copy per reader. NULL -- or a stage that was never opened -- exports as
 * `"stage":null`, which is a different fact from "a stage with nobody in it". */
void dw_net_set_stage(const DwStage *s);
const DwStage *dw_net_stage(void);

/* Where dw_net_publish writes. NULL or "" turns publishing off (the default). Non-NULL is read
 * immediately, so a caller does not have to keep the string alive. */
void dw_net_set_path(const char *path);
const char *dw_net_path(void);

/* Serialises the ladder + the recent-match feed into `buf` as JSON (schema DW_NET_SCHEMA).
 * Returns the length written (excluding the NUL), or -1 if the buffer is too small -- a truncated
 * snapshot is worse than none, so it refuses rather than emitting invalid JSON. `now_ms` is only
 * used for the "expires in" numbers and the generated timestamp; nothing in the export reads a
 * clock of its own, which is what makes it testable. */
int dw_net_export(const DwLadder *l, char *buf, size_t cap, unsigned now_ms);

/* Writes the same JSON to `path` (write-then-rename, parent directory created if it is one level
 * deep). Returns 1 on success, 0 on any failure -- never a partially written file. */
int dw_net_write(const DwLadder *l, const char *path, unsigned now_ms);

/* The tick-side entry point: writes at most once every DW_NET_PUBLISH_INTERVAL_MS, and immediately
 * (bounded by DW_NET_MIN_INTERVAL_MS) when a match has finished since the last write, so the feed is
 * live without the file being rewritten on every frame. Returns 1 if it wrote, 0 if it skipped. */
int dw_net_publish(const DwLadder *l, unsigned now_ms);

#define DW_NET_PUBLISH_INTERVAL_MS 2000u   /* heartbeat: the snapshot is never older than this */
#define DW_NET_MIN_INTERVAL_MS 500u        /* a burst of matches still writes at most this often */

/* Called by dw_ladder_accept for every finished match. `winner_seat` is 0/1 (2 = draw); the two pids
 * are seat 0 and seat 1, in the challenge's own order. */
void dw_net_note_match(int kind, int pid0, int pid1, int winner_seat, int rounds,
                       int hull0, int hull1, unsigned now_ms);

/* The recent-match feed, newest first. Returns the number available (<= DW_NET_MAX_RECENT). */
typedef struct {
    int kind;          /* DW_CHAL_* */
    int pid0, pid1;    /* seats, in the challenge's order */
    int winner;        /* 0/1 seat, 2 draw */
    int rounds;
    int hull0, hull1;  /* final hulls */
    unsigned ms;       /* when it finished */
} DwNetMatch;

int dw_net_recent(DwNetMatch *out, int max);

/* Drops the recent feed. Called with the world (witness_ai_reset), so a new world does not inherit
 * a previous session's ticker. */
void dw_net_reset(void);

#endif
