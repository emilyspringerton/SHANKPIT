/* zone_beacon.h -- LAN discovery for a hosted world: "there is a dungeon at 192.168.1.20:6969".
 *
 * The last named gap in docs/GFD_UNIFICATION.md phase 3. `--coop-host` made hosting one command, but
 * the ally still had to be told an IP, which is exactly the friction the founder's "potentially a lan
 * ally could join for coop on our world" is about. Nothing here authenticates anything -- a beacon is
 * an advertisement, and the truthful source address of the datagram is the address worth dialling, so
 * a forged one can only point a player at a host that will not answer. That is deliberate: a
 * discovery protocol with trust in it is a much bigger thing than "find the world on this LAN".
 *
 * This is NOT part of the game protocol (packages/common/protocol.h): it travels on its own UDP port,
 * to nobody in particular, is never required for a session to work, and carries identity only -- the
 * same discipline as PACKET_ZONE_STATE, which sends a zone's (id, seed) and lets both sides generate
 * the geometry. A beacon that says the wrong port would send allies to nothing, so the game port is
 * carried explicitly rather than assumed to be 6969.
 */
#ifndef SHANKPIT_ZONE_BEACON_H
#define SHANKPIT_ZONE_BEACON_H

#define ZONE_BEACON_MAGIC 0x53505A42u /* "SPZB", little-endian on the wire */
#define ZONE_BEACON_VERSION 1
#define ZONE_BEACON_PORT 6968 /* one below the game port; nothing else in this tree uses it */
#define ZONE_BEACON_SIZE 48   /* fixed: a beacon is small, and a variable one invites length bugs */
#define ZONE_BEACON_ZONE_MAX 32
#define ZONE_BEACON_DEFAULT_TIMEOUT_MS 3000

typedef struct {
    char zone[ZONE_BEACON_ZONE_MAX];
    unsigned int seed;
    int players;
    int port; /* the UDP game port this world actually listens on */
} ZoneBeacon;

/* Serialise to exactly ZONE_BEACON_SIZE bytes. Returns the length, or 0 if the beacon is not
 * advertisable (bad zone id, port out of range) or `out` is too small. */
int zone_beacon_encode(const ZoneBeacon *b, unsigned char *out, int out_sz);

/* Treat the datagram as untrusted input: wrong length, magic or version, an empty/unterminated/
 * illegal zone id, or an out-of-range port are all refused rather than half-decoded. */
int zone_beacon_decode(const unsigned char *in, int len, ZoneBeacon *out);

/* Send one beacon to the LAN broadcast address AND to loopback. The broadcast is the LAN's copy; the
 * loopback copy is for the host's own finder, and it is what lets a discovery test run on a box with
 * no broadcast route (this one). Returns the number of sends that succeeded (0..2). */
int zone_beacon_announce(int sock, int beacon_port, const ZoneBeacon *b);

/* An announce socket: UDP, SO_BROADCAST, SO_REUSEADDR (a restarted host must be able to re-advertise
 * immediately). Returns -1 on failure. Close it with zone_beacon_close. */
int zone_beacon_socket(void);
void zone_beacon_close(int sock);

/* Wait up to `timeout_ms` for a beacon. On success fills `out`, and `source_ip` with the datagram's
 * real source address (the address to dial) -- never a field from inside the packet. Returns 1 on a
 * world found, 0 on timeout, -1 if the beacon port could not be bound. */
int zone_beacon_find(int beacon_port, int timeout_ms, ZoneBeacon *out, char *source_ip, int ip_sz);

#endif /* SHANKPIT_ZONE_BEACON_H */
