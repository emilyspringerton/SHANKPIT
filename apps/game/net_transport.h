/* apps/game/net_transport.h -- the seam between the game host and whatever transport a build links.
 *
 * host.c owns protocol behaviour (which packets, when, and what they mean). It must not own the
 * socket API: a core build links no transport at all, the native client links the host-C UDP
 * implementation today, and the FFI-free LLVM/WASM edges will link their own implementation later.
 *
 * No platform headers leak through this file. Addresses are IPv4 in network byte order, which is all
 * the current UDP transport needs.
 */
#ifndef SHANKPIT_NET_TRANSPORT_H
#define SHANKPIT_NET_TRANSPORT_H

typedef struct {
    unsigned int addr;   /* IPv4 address, network byte order */
    unsigned short port; /* UDP port, network byte order */
} NetAddr;

/* 0 on success, -1 on failure. bind_port > 0 binds a server socket; 0 opens a client socket on an
 * ephemeral port. */
int net_transport_open(int bind_port);
void net_transport_close(void);
int net_transport_is_open(void);

/* IPv4 literal only (no DNS). Returns 1 when parsed, 0 otherwise. */
int net_transport_addr(const char *ip, int port, NetAddr *out);

/* Returns bytes sent, or -1. */
int net_transport_send(const NetAddr *to, const void *data, int len);

/* Returns bytes received (filling from), or -1 when no datagram is queued. */
int net_transport_recv(NetAddr *from, void *data, int cap);

#endif /* SHANKPIT_NET_TRANSPORT_H */
