/* zone_beacon_test.c -- the beacon's wire form and a real find, end to end.
 *
 * The codec half is untrusted-input work: a beacon arrives from every process on the LAN, and its
 * zone id becomes var/maps/proc_<id>.json on the receiving side, so the refusals are the point.
 *
 * The live half starts bin/shank_server with its own game port and beacon port, waits for a beacon
 * through zone_beacon_find (the same call the client makes), and checks that the world it found is
 * the one that was started -- including that the advertised game port is the real one, since a
 * beacon pointing at 6969 while the server runs elsewhere sends allies to nothing.
 *
 * Build/run: make test-zone-beacon   (from the repo root)
 */
#include "zone_beacon.h"
#include "coop_host.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <arpa/inet.h>
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
    unsigned char buf[ZONE_BEACON_SIZE + 8];

    printf("== zone_beacon: round trip ==\n");
    {
        ZoneBeacon a, b;
        memset(&a, 0, sizeof a);
        snprintf(a.zone, sizeof a.zone, "lab_basement");
        a.seed = 0x00C0FFEEu;
        a.players = 3;
        a.port = 7345;

        int n = zone_beacon_encode(&a, buf, sizeof buf);
        check(n == ZONE_BEACON_SIZE, "encode writes exactly the fixed size");
        check(buf[5] == 0 && buf[46] == 0 && buf[47] == 0, "reserved bytes are zero");

        memset(&b, 0, sizeof b);
        check(zone_beacon_decode(buf, n, &b) == 1, "decode accepts its own output");
        check(strcmp(b.zone, "lab_basement") == 0, "zone survives the wire");
        check(b.seed == 0x00C0FFEEu, "the seed survives the wire (it is the layout's identity)");
        check(b.players == 3, "players survive the wire");
        check(b.port == 7345, "the game port survives the wire");
    }

    printf("== zone_beacon: refusals ==\n");
    {
        ZoneBeacon a, b;
        memset(&a, 0, sizeof a);
        snprintf(a.zone, sizeof a.zone, "dungeon_1");
        a.seed = 7;
        a.port = 6969;
        int n = zone_beacon_encode(&a, buf, sizeof buf);

        check(zone_beacon_decode(buf, n - 1, &b) == 0, "a short datagram is refused");
        check(zone_beacon_decode(buf, n + 1, &b) == 0, "an over-long datagram is refused");
        check(zone_beacon_decode(NULL, n, &b) == 0, "NULL is refused");
        check(zone_beacon_decode(buf, n, NULL) == 0, "a NULL out is refused");

        unsigned char bad[ZONE_BEACON_SIZE];
        memcpy(bad, buf, sizeof bad);
        bad[0] ^= 0xFF;
        check(zone_beacon_decode(bad, ZONE_BEACON_SIZE, &b) == 0, "a wrong magic is refused");

        memcpy(bad, buf, sizeof bad);
        bad[4] = (unsigned char)(ZONE_BEACON_VERSION + 1);
        check(zone_beacon_decode(bad, ZONE_BEACON_SIZE, &b) == 0, "a future version is refused");

        memcpy(bad, buf, sizeof bad);
        memset(bad + 14, 0, ZONE_BEACON_ZONE_MAX);
        check(zone_beacon_decode(bad, ZONE_BEACON_SIZE, &b) == 0, "an empty zone id is refused");

        memcpy(bad, buf, sizeof bad);
        memset(bad + 14, 'a', ZONE_BEACON_ZONE_MAX); /* no terminator in the field */
        check(zone_beacon_decode(bad, ZONE_BEACON_SIZE, &b) == 0, "an unterminated zone id is refused");

        memcpy(bad, buf, sizeof bad);
        memset(bad + 14, 0, ZONE_BEACON_ZONE_MAX);
        memcpy(bad + 14, "../etc", 6);
        check(zone_beacon_decode(bad, ZONE_BEACON_SIZE, &b) == 0, "a zone id that is a path is refused");

        memcpy(bad, buf, sizeof bad);
        memset(bad + 14, 0, ZONE_BEACON_ZONE_MAX);
        memcpy(bad + 14, "-rf", 3);
        check(zone_beacon_decode(bad, ZONE_BEACON_SIZE, &b) == 0, "a zone id that looks like a flag is refused");

        memcpy(bad, buf, sizeof bad);
        bad[6] = 0; bad[7] = 0;
        check(zone_beacon_decode(bad, ZONE_BEACON_SIZE, &b) == 0, "game port 0 is refused");

        memcpy(bad, buf, sizeof bad);
        bad[6] = 80; bad[7] = 0;
        check(zone_beacon_decode(bad, ZONE_BEACON_SIZE, &b) == 0, "a privileged game port is refused");
    }

    printf("== zone_beacon: encode refusals ==\n");
    {
        ZoneBeacon a;
        memset(&a, 0, sizeof a);
        snprintf(a.zone, sizeof a.zone, "dungeon_1");
        a.port = 6969;
        check(zone_beacon_encode(&a, buf, ZONE_BEACON_SIZE - 1) == 0, "a too-small output buffer is refused");

        a.port = 80;
        check(zone_beacon_encode(&a, buf, sizeof buf) == 0, "an invalid port is not advertised");
        a.port = 6969;
        snprintf(a.zone, sizeof a.zone, "not a zone");
        check(zone_beacon_encode(&a, buf, sizeof buf) == 0, "an invalid zone is not advertised");

        snprintf(a.zone, sizeof a.zone, "dungeon_1");
        a.players = -4;
        int n = zone_beacon_encode(&a, buf, sizeof buf);
        ZoneBeacon b;
        check(n == ZONE_BEACON_SIZE && zone_beacon_decode(buf, n, &b) == 1 && b.players == 0,
              "a negative player count clamps to 0 instead of wrapping");
    }

    printf("== zone_beacon: finder refusals ==\n");
    {
        ZoneBeacon b;
        char ip[64];
        check(zone_beacon_find(80, 100, &b, ip, sizeof ip) == -1, "an invalid beacon port is refused");
        check(zone_beacon_announce(-1, ZONE_BEACON_PORT, &b) == 0, "announcing on a bad socket sends nothing");
    }

    printf("== zone_beacon: real announce and find ==\n");
    {
        const char *bin = "bin/shank_server";
        const int game_port = 7351, beacon_port = 7352;
        if (!file_exists(bin)) {
            printf("SKIP  %s not built -- run `make server` for the live discovery check\n", bin);
        } else {
            char port_buf[8], beacon_buf[8];
            snprintf(port_buf, sizeof port_buf, "%d", game_port);
            snprintf(beacon_buf, sizeof beacon_buf, "%d", beacon_port);
            /* Spawned directly rather than through coop_host_start because this test needs a
               non-default beacon port; everything else is the same binary and flags. */
            pid_t pid = fork();
            if (pid == 0) {
                execl(bin, bin, "--port", port_buf, "--beacon-port", beacon_buf,
                      "--dungeon", "disco_selftest", (char *)NULL);
                _exit(127);
            }
            check(pid > 0, "the server was forked");

            /* The zone file is the server's own proof of life before the beacon can carry it. */
            int zone_up = 0;
            for (int i = 0; i < 60 && !zone_up; i++) {
                if (file_exists("var/maps/proc_disco_selftest.json")) zone_up = 1;
                else {
                    struct timespec ts = {0, 50000000L};
                    nanosleep(&ts, NULL);
                }
            }
            check(zone_up, "the server generated its zone before advertising it");

            ZoneBeacon b;
            char ip[64] = "";
            memset(&b, 0, sizeof b);
            int found = zone_beacon_find(beacon_port, 4000, &b, ip, sizeof ip);
            check(found == 1, "the finder heard a beacon");
            check(strcmp(b.zone, "disco_selftest") == 0, "the beacon names the world that is actually running");
            check(b.port == game_port, "the beacon advertises the REAL game port, not the default");
            check(b.players >= 0 && b.players <= 64, "the advertised player count is sane");
            check(b.seed != 0, "the seed travels, so a joiner can rebuild the same dungeon");
            struct in_addr parsed;
            check(ip[0] && inet_pton(AF_INET, ip, &parsed) == 1, "the finder reports a real source address to dial");

            if (pid > 0) kill(pid, SIGTERM);
            if (pid > 0) {
                int status = 0;
                for (int i = 0; i < 40; i++) {
                    if (waitpid(pid, &status, WNOHANG) == pid) break;
                    struct timespec ts = {0, 25000000L};
                    nanosleep(&ts, NULL);
                }
            }
            remove("var/maps/proc_disco_selftest.json");
        }
    }

    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
