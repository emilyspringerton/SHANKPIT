/* coop_host_test.c -- the decisions AND the spawn, checked for real.
 *
 * The last section starts the actual bin/shank_server through the same coop_host_start the lobby
 * calls, and waits for the zone file the server writes. That is the only way to know the fork/exec
 * path works: a compiled spawn is not a running server. Skipped with a clear notice (not a pass) if
 * `make server` has not been run, so this binary is still useful on its own.
 *
 * Build/run: make test-coop-host   (runs from the repo root, which the server needs for var/maps/)
 */
#include "coop_host.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>

static int g_checks = 0;
static int g_failures = 0;

static void check(int cond, const char *what) {
    g_checks++;
    if (!cond) {
        g_failures++;
        printf("FAIL  %s\n", what);
    } else {
        printf("ok    %s\n", what);
    }
}

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

int main(void) {
    printf("== coop_host: config ==\n");
    {
        CoopHostConfig cfg;
        check(coop_host_config(&cfg, NULL, 0) == 1, "defaults accepted");
        check(strcmp(cfg.zone, COOP_HOST_DEFAULT_ZONE) == 0, "default zone is the server's own default");
        check(cfg.port == COOP_HOST_DEFAULT_PORT, "default port is the live queue port");

        check(coop_host_config(&cfg, "lab_basement", 7100) == 1, "explicit zone and port accepted");
        check(strcmp(cfg.zone, "lab_basement") == 0 && cfg.port == 7100, "explicit values are what was asked for");

        check(coop_host_config(&cfg, "lab/basement", 0) == 0, "a zone with a slash is refused");
        check(coop_host_config(&cfg, "..", 0) == 0, "a zone of '..' is refused");
        check(coop_host_config(&cfg, "-rf", 0) == 0, "a zone starting with '-' is refused");
        check(coop_host_config(&cfg, "", 80) == 0, "a privileged port is refused");
        check(coop_host_config(&cfg, "", 70000) == 0, "an out-of-range port is refused");
    }

    printf("== coop_host: zone ids ==\n");
    check(coop_host_zone_valid("dungeon_1") == 1, "the default id is valid");
    check(coop_host_zone_valid("dungeon-2") == 1, "hyphens are allowed");
    check(coop_host_zone_valid("a") == 1, "a single character is enough");
    check(coop_host_zone_valid("") == 0, "empty is not a zone");
    check(coop_host_zone_valid(NULL) == 0, "NULL is not a zone");
    check(coop_host_zone_valid("has space") == 0, "a space is not a zone");
    check(coop_host_zone_valid("dot.dot") == 0, "a dot is not a zone");
    check(coop_host_zone_valid("back\\slash") == 0, "a backslash is not a zone");
    {
        char long_id[COOP_HOST_ZONE_MAX + 8];
        memset(long_id, 'a', sizeof long_id);
        long_id[COOP_HOST_ZONE_MAX - 1] = '\0';
        check(coop_host_zone_valid(long_id) == 1, "an id just under the limit is valid");
        long_id[COOP_HOST_ZONE_MAX - 1] = 'a'; /* now >= the limit */
        check(coop_host_zone_valid(long_id) == 0, "an over-long id is refused");
    }

    printf("== coop_host: child argv ==\n");
    {
        CoopHostConfig cfg;
        char *argv[COOP_HOST_ARG_MAX];
        char port_buf[8];
        coop_host_config(&cfg, "plateau", 7123);
        check(coop_host_fill_argv(&cfg, argv, port_buf) == 1, "argv built");
        check(argv[1] && strcmp(argv[1], "--port") == 0, "flag --port first");
        check(argv[2] && strcmp(argv[2], "7123") == 0, "the port is a decimal string, not binary");
        check(argv[3] && strcmp(argv[3], "--dungeon") == 0, "flag --dungeon next");
        check(argv[4] && strcmp(argv[4], "plateau") == 0, "the zone is passed through");
        check(argv[5] == NULL, "argv is NULL-terminated (execv needs this)");
        check(argv[4] == cfg.zone, "the zone argument points at the config, not a copy");
    }

    printf("== coop_host: UDP port probe ==\n");
    {
        const int probe_port = 7311;
        check(coop_host_port_busy(probe_port) == 0, "an unused port reads as free");

        int s = socket(AF_INET, SOCK_DGRAM, 0);
        check(s >= 0, "probe harness bound a socket");
        if (s >= 0) {
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof addr);
            addr.sin_family = AF_INET;
            addr.sin_port = htons((unsigned short)probe_port);
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
            int rc = bind(s, (struct sockaddr *)&addr, sizeof addr);
            check(rc == 0, "probe harness holds the port");
            check(coop_host_port_busy(probe_port) == 1, "a held port reads as busy (this is what makes relaunch attach)");
            close(s);
            check(coop_host_port_busy(probe_port) == 0, "the port reads as free again after close");
        }
    }

    printf("== coop_host: real server spawn ==\n");
    {
        const char *bin = "bin/shank_server";
        if (!file_exists(bin)) {
            printf("SKIP  %s not built -- run `make server` for the live spawn check\n", bin);
        } else {
            CoopHostConfig cfg;
            CoopHostResult r;
            const int port = 7341;
            coop_host_config(&cfg, "coop_selftest", port);

            int started = coop_host_start(bin, &cfg, &r);
            check(started == 1, "coop_host_start reports a reachable world");
            check(r.ok == 1, "result says ok");
            check(r.attached == 0, "nothing was running, so it spawned rather than attached");
            check(r.pid > 0, "a child pid came back");

            /* The server writes its zone on startup through the ordinary custom-level path. Waiting
             * for the file observes the real outcome, not the fork's return code. */
            char path[256];
            snprintf(path, sizeof path, "var/maps/proc_%s.json", cfg.zone);
            int appeared = 0;
            for (int i = 0; i < 60 && !appeared; i++) {
                if (file_exists(path)) appeared = 1;
                else {
                    struct timespec ts;
                    ts.tv_sec = 0;
                    ts.tv_nsec = 50000000L;
                    nanosleep(&ts, NULL);
                }
            }
            check(appeared, "the spawned server wrote var/maps/proc_coop_selftest.json");
            check(coop_host_port_busy(port) == 1, "the spawned server is bound and listening");

            /* Second call on the same port must attach, never spawn a twin that cannot bind. */
            {
                CoopHostResult again;
                int ok2 = coop_host_start(bin, &cfg, &again);
                check(ok2 == 1 && again.attached == 1, "a second start attaches to the running world");
            }

            if (r.pid > 0) kill((pid_t)r.pid, SIGTERM);
            if (r.pid > 0) {
                int status = 0;
                for (int i = 0; i < 40; i++) {
                    pid_t w = waitpid((pid_t)r.pid, &status, WNOHANG);
                    if (w == (pid_t)r.pid) break;
                    struct timespec ts;
                    ts.tv_sec = 0;
                    ts.tv_nsec = 25000000L;
                    nanosleep(&ts, NULL);
                }
            }
            for (int i = 0; i < 20 && coop_host_port_busy(port); i++) {
                struct timespec ts;
                ts.tv_sec = 0;
                ts.tv_nsec = 50000000L;
                nanosleep(&ts, NULL);
            }
            check(coop_host_port_busy(port) == 0, "the port is released once the server stops");
            remove(path); /* the test's own runtime artifact, not a committed map */
        }
    }

    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
