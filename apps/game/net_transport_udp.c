/* apps/game/net_transport_udp.c -- the host-C UDP transport. This is the "baked C" rung of the
 * ladder: the socket API and the Winsock/POSIX split live here, not in host.c or the core. */
#include "net_transport.h"

#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET shank_socket;
#define SHANK_BAD_SOCKET INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int shank_socket;
#define SHANK_BAD_SOCKET (-1)
#endif

static shank_socket g_fd = SHANK_BAD_SOCKET;

int net_transport_is_open(void) { return g_fd != SHANK_BAD_SOCKET; }

int net_transport_open(int bind_port) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
#endif
    g_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_fd == SHANK_BAD_SOCKET) return -1;

    if (bind_port > 0) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons((unsigned short)bind_port);
        if (bind(g_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
            net_transport_close();
            return -1;
        }
    }

#ifdef _WIN32
    {
        u_long mode = 1;
        if (ioctlsocket(g_fd, FIONBIO, &mode) != 0) {
            net_transport_close();
            return -1;
        }
    }
#else
    {
        int flags = fcntl(g_fd, F_GETFL, 0);
        if (flags < 0 || fcntl(g_fd, F_SETFL, flags | O_NONBLOCK) != 0) {
            net_transport_close();
            return -1;
        }
    }
#endif
    return 0;
}

void net_transport_close(void) {
    if (g_fd == SHANK_BAD_SOCKET) return;
#ifdef _WIN32
    closesocket(g_fd);
    WSACleanup();
#else
    close(g_fd);
#endif
    g_fd = SHANK_BAD_SOCKET;
}

int net_transport_addr(const char *ip, int port, NetAddr *out) {
    unsigned int addr = inet_addr(ip);
    if (!out || addr == INADDR_NONE) return 0;
    out->addr = addr;
    out->port = htons((unsigned short)port);
    return 1;
}

int net_transport_send(const NetAddr *to, const void *data, int len) {
    struct sockaddr_in dst;
    if (g_fd == SHANK_BAD_SOCKET || !to) return -1;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = to->addr;
    dst.sin_port = to->port;
    return (int)sendto(g_fd, (const char *)data, len, 0, (struct sockaddr *)&dst, sizeof(dst));
}

int net_transport_recv(NetAddr *from, void *data, int cap) {
    struct sockaddr_in src;
#ifdef _WIN32
    int srclen = (int)sizeof(src);
#else
    socklen_t srclen = sizeof(src);
#endif
    int n;
    if (g_fd == SHANK_BAD_SOCKET) return -1;
    n = (int)recvfrom(g_fd, (char *)data, cap, 0, (struct sockaddr *)&src, &srclen);
    if (n < 0) return -1;
    if (from) {
        from->addr = src.sin_addr.s_addr;
        from->port = src.sin_port;
    }
    return n;
}
