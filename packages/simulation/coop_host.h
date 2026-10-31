/* coop_host.h -- one command to host a LAN co-op world: start the local game server, join it.
 *
 * Phase 3 of docs/GFD_UNIFICATION.md. Before this, hosting meant two terminals (or a systemd unit):
 * `./bin/shank_server --dungeon dungeon_1` in one, then `./bin/shank_lobby --host 127.0.0.1` in the
 * other. `--coop-host [zone]` in the lobby does both: it starts the same real server binary as a
 * child process and points the client at loopback, so an ally on the LAN joins by IP the ordinary
 * way while the host plays the ordinary MODE_QUEUE path. No second code path in the netcode.
 *
 * Deliberately the SAME shape as the two launchers already in apps/lobby/src/main.c
 * (lobby_launch_nock's fork/execl at --port 8787, lobby_launch_app's ZOMBIES self-relaunch at
 * --host 127.0.0.1 --port 6971) rather than a new process abstraction: this is the third instance of
 * an established pattern, not a reason to rewrite the other two.
 *
 * Two behaviours here are not cosmetic:
 *
 *  - The port probe. A UDP port cannot be probed like a TCP one (there is no accept), so
 *    coop_host_port_busy binds it briefly and lets go. That is what makes relaunching the client work:
 *    a server from a previous session is still running and still holds the world, so we ATTACH
 *    instead of spawning a second one that would fail to bind and die. It is also the honest way to
 *    answer "is a world already up?" without a lockfile that goes stale when a process is killed.
 *
 *  - Zone validation. The zone id becomes a real filename on both sides
 *    (var/maps/proc_<zone>.json, apps/server/src/main.c) and travels in PACKET_ZONE_STATE, so an id
 *    that can escape the map directory, or one that starts with '-' and would be read as a flag by
 *    the child, is rejected here at the entry point instead of failing later as a mystery.
 */
#ifndef SHANKPIT_COOP_HOST_H
#define SHANKPIT_COOP_HOST_H

#include <stddef.h>

#define COOP_HOST_DEFAULT_PORT 6969 /* the live queue port; MODE_QUEUE clients already default here */
#define COOP_HOST_DEFAULT_ZONE "dungeon_1" /* apps/server/src/main.c's own --dungeon default */
#define COOP_HOST_ZONE_MAX 32
#define COOP_HOST_ARG_MAX 8

typedef struct {
    char zone[COOP_HOST_ZONE_MAX];
    int port;
} CoopHostConfig;

typedef struct {
    int ok;      /* 1 = a world is reachable on this port (spawned now, or already running) */
    int attached; /* 1 = nothing was spawned; an existing server already owns the port */
    long pid;    /* child pid (POSIX) when spawned; 0 when attached or on Windows */
    char error[160]; /* why ok == 0, in words fit to put on screen */
} CoopHostResult;

/* Zone ids: 1..COOP_HOST_ZONE_MAX-1 chars of [A-Za-z0-9_-], and not starting with '-'.
 * The conservative set is on purpose -- the id is a filename component and a wire string. */
int coop_host_zone_valid(const char *zone);

/* Usable unprivileged ports only; anything else is a typo or an attempt to bind something real. */
int coop_host_port_valid(int port);

/* Fill `out` from optional arguments, applying the defaults above. Absent/empty zone and a
 * non-positive port take the default. Returns 1 on success; 0 (out untouched) if a supplied value is
 * invalid, so a bad argument is refused rather than silently replaced by a default. */
int coop_host_config(CoopHostConfig *out, const char *zone, int port);

/* Describe the child's command line: argv = { bin, "--port", port_buf, "--dungeon", zone, NULL }.
 * Separate from the spawn so the exact arguments are testable without starting a process. */
int coop_host_fill_argv(const CoopHostConfig *cfg, char *argv[COOP_HOST_ARG_MAX], char port_buf[8]);

/* Is another process bound to 0.0.0.0:<port> (UDP)? 1 = busy, 0 = free. A probe failure (no socket)
 * answers 0 -- "free" -- because spawning and letting the child report a bind error is a better
 * outcome than refusing to start a world at all. */
int coop_host_port_busy(int port);

/* Start `server_bin` with this config and confirm it did not die on startup. On POSIX the child is
 * forked and exec'd with coop_host_fill_argv's exact argv; on Windows CreateProcessA(CREATE_NO_WINDOW)
 * with the equivalent command line. The child is intentionally not waited on: a hosted world outlives
 * the lobby window, and the port probe above is what makes a relaunch rejoin it instead of racing it. */
int coop_host_start(const char *server_bin, const CoopHostConfig *cfg, CoopHostResult *out);

#endif /* SHANKPIT_COOP_HOST_H */
