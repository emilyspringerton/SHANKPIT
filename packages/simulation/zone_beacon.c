/* zone_beacon.c -- see zone_beacon.h. No SDL, no GL: the server and the client's CLI both use this,
 * and the test links it on its own. */
#include "zone_beacon.h"
#include "coop_host.h" /* one definition of what a zone id may be, shared with --coop-host */

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#endif

/* Fixed little-endian layout, written byte by byte so the wire form does not depend on struct
 * packing or host endianness:
 *   [0..3]  magic
 *   [4]     version
 *   [5]     reserved (0)
 *   [6..7]  game port
 *   [8..9]  players
 *   [10..13] seed
 *   [14..45] zone id, NUL-padded
 *   [46..47] reserved (0)
 */
static void put_u16(unsigned char *p, unsigned int v) { p[0] = (unsigned char)(v & 0xFF); p[1] = (unsigned char)((v >> 8) & 0xFF); }
static void put_u32(unsigned char *p, unsigned int v) {
    p[0] = (unsigned char)(v & 0xFF); p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF); p[3] = (unsigned char)((v >> 24) & 0xFF);
}
static unsigned int get_u16(const unsigned char *p) { return (unsigned int)p[0] | ((unsigned int)p[1] << 8); }
static unsigned int get_u32(const unsigned char *p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

int zone_beacon_encode(const ZoneBeacon *b, unsigned char *out, int out_sz) {
    if (!b || !out || out_sz < ZONE_BEACON_SIZE) return 0;
    if (!coop_host_zone_valid(b->zone)) return 0;
    if (!coop_host_port_valid(b->port)) return 0;
    int players = b->players;
    if (players < 0) players = 0;
    if (players > 65535) players = 65535;

    memset(out, 0, ZONE_BEACON_SIZE);
    put_u32(out + 0, ZONE_BEACON_MAGIC);
    out[4] = (unsigned char)ZONE_BEACON_VERSION;
    out[5] = 0;
    put_u16(out + 6, (unsigned int)b->port);
    put_u16(out + 8, (unsigned int)players);
    put_u32(out + 10, b->seed);
    size_t n = strlen(b->zone);
    if (n >= ZONE_BEACON_ZONE_MAX) return 0; /* cannot happen after the id check; belt and braces */
    memcpy(out + 14, b->zone, n); /* the rest stays zero, so the id is always NUL-terminated */
    return ZONE_BEACON_SIZE;
}

int zone_beacon_decode(const unsigned char *in, int len, ZoneBeacon *out) {
    if (!in || !out || len != ZONE_BEACON_SIZE) return 0;
    if (get_u32(in + 0) != ZONE_BEACON_MAGIC) return 0;
    if (in[4] != ZONE_BEACON_VERSION) return 0;

    ZoneBeacon b;
    memset(&b, 0, sizeof b);
    b.port = (int)get_u16(in + 6);
    b.players = (int)get_u16(in + 8);
    b.seed = get_u32(in + 10);

    /* The id must be present AND terminated inside its own field: a half-copied id is a filename on
     * the receiving side (var/maps/proc_<id>.json), so this is a boundary, not pedantry. */
    int terminated = 0;
    for (int i = 0; i < ZONE_BEACON_ZONE_MAX; i++) {
        if (in[14 + i] == '\0') { terminated = 1; break; }
    }
    if (!terminated) return 0;
    memcpy(b.zone, in + 14, ZONE_BEACON_ZONE_MAX);
    b.zone[ZONE_BEACON_ZONE_MAX - 1] = '\0';
    if (!coop_host_zone_valid(b.zone)) return 0;
    if (!coop_host_port_valid(b.port)) return 0;

    *out = b;
    return 1;
}

#ifdef _WIN32
static int ensure_wsa(void) {
    static int ready = 0;
    if (!ready) {
        WSADATA w;
        if (WSAStartup(MAKEWORD(2, 2), &w) != 0) return 0;
        ready = 1;
    }
    return 1;
}
#endif

int zone_beacon_socket(void) {
#ifdef _WIN32
    if (!ensure_wsa()) return -1;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return -1;
    int yes = 1;
    if (setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char *)&yes, sizeof yes) != 0 ||
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes) != 0) {
        closesocket(s);
        return -1;
    }
    return (int)s;
#else
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return -1;
    int yes = 1;
    if (setsockopt(s, SOL_SOCKET, SO_BROADCAST, &yes, sizeof yes) != 0 ||
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) != 0) {
        close(s);
        return -1;
    }
    return s;
#endif
}

void zone_beacon_close(int sock) {
    if (sock < 0) return;
#ifdef _WIN32
    closesocket((SOCKET)sock);
#else
    close(sock);
#endif
}

int zone_beacon_announce(int sock, int beacon_port, const ZoneBeacon *b) {
    if (sock < 0 || !coop_host_port_valid(beacon_port)) return 0;
    unsigned char buf[ZONE_BEACON_SIZE];
    int n = zone_beacon_encode(b, buf, sizeof buf);
    if (n <= 0) return 0;

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof dst);
    dst.sin_family = AF_INET;
    dst.sin_port = htons((unsigned short)beacon_port);

    int sent = 0;
    dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);
#ifdef _WIN32
    if (sendto((SOCKET)sock, (const char *)buf, n, 0, (struct sockaddr *)&dst, sizeof dst) == n) sent++;
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (sendto((SOCKET)sock, (const char *)buf, n, 0, (struct sockaddr *)&dst, sizeof dst) == n) sent++;
#else
    if (sendto(sock, buf, (size_t)n, 0, (struct sockaddr *)&dst, sizeof dst) == n) sent++;
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (sendto(sock, buf, (size_t)n, 0, (struct sockaddr *)&dst, sizeof dst) == n) sent++;
#endif
    return sent;
}

int zone_beacon_find(int beacon_port, int timeout_ms, ZoneBeacon *out, char *source_ip, int ip_sz) {
    if (!out || !coop_host_port_valid(beacon_port)) return -1;
    if (timeout_ms <= 0) timeout_ms = ZONE_BEACON_DEFAULT_TIMEOUT_MS;

#ifdef _WIN32
    if (!ensure_wsa()) return -1;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return -1;
#else
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return -1;
#endif
    int yes = 1;
#ifdef _WIN32
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);
#else
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)beacon_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr *)&addr, sizeof addr) != 0) {
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
        return -1;
    }

    int found = 0;
    unsigned int waited = 0;
    unsigned char buf[512];
    while (!found && waited < (unsigned int)timeout_ms) {
        fd_set rd;
        FD_ZERO(&rd);
#ifdef _WIN32
        FD_SET(s, &rd);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        int rc = select(0, &rd, NULL, NULL, &tv);
#else
        FD_SET(s, &rd);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        int rc = select(s + 1, &rd, NULL, NULL, &tv);
#endif
        waited += 100;
        if (rc <= 0) continue;

        struct sockaddr_in from;
        socklen_t fromlen = sizeof from;
#ifdef _WIN32
        int got = recvfrom(s, (char *)buf, (int)sizeof buf, 0, (struct sockaddr *)&from, &fromlen);
#else
        ssize_t got = recvfrom(s, buf, sizeof buf, 0, (struct sockaddr *)&from, &fromlen);
#endif
        if (got <= 0) continue;
        /* Length is checked by the decoder; anything that is not exactly a beacon is dropped and the
         * wait continues, because this port is open to every process on the LAN. */
        if (!zone_beacon_decode(buf, (int)got, out)) continue;
        found = 1;
        if (source_ip && ip_sz > 0) {
            const char *ip = inet_ntoa(from.sin_addr);
            snprintf(source_ip, (size_t)ip_sz, "%s", ip ? ip : "");
        }
    }

#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
    return found;
}
