#ifndef CHAT_BRIDGE_H
#define CHAT_BRIDGE_H

/* chat_bridge.h -- the global chat bridge: a SHANKPIT world's General channel <-> IDUNA's cross-game chat bus
 * (POST/GET /api/v1/chat/messages), which already joins GFD's mud and server, REDGARDEN's Battlegrounds client,
 * BIG_O, GTA7 on EINHORN_SURVIVAL and SHANKPIT's phone app (founder, 2026-11-17: "WE NEED THE CHAT BRIDGE TO WORK
 * GLOBALY").
 *
 *   General line --chat server's channel hook--> chat_bridge_offer --queue--> worker POSTs (channel "shankpit",
 *   sender_source "shankpit_server")
 *   worker GETs since_id --> chat_bridge_ingest --> "MC:Steve: hi" injected into General (chat_server_inject)
 *
 * Only General crosses (say, yell, whispers and custom channels are back channels and stay). Every decision is
 * chat_bridge_rules.c (PARENA/stdlib/shankpit/chat_bridge_rules.prn): which sources show, what goes out, tags, pacing.
 * The core below is plain C (tested by chat_bridge_test.c); the network worker is POSIX and only runs on a server
 * whose environment names IDUNA and an agent:
 *   SHANKPIT_IDUNA_URL          e.g. https://iam.okemily.com  (no default)
 *   SHANKPIT_IDUNA_AGENT        the IDUNA agent this server signs in as  (no default: IDUNA creates it)
 *   SHANKPIT_IDUNA_SECRET_FILE  a file whose first line is that agent's secret  (never logged, never on a command line)
 * Any of them unset = the bridge is off and the world's chat stays local. */

#include "../common/protocol.h"
#include "chat_server.h"

#define CHAT_BRIDGE_QUEUE 64     /* br-post-queue */
#define CHAT_BRIDGE_OWN   64     /* ids of this host's own posts, remembered for the loop guard */

typedef struct { char name[NET_CHAT_NAME]; char text[NET_CHAT_TEXT]; } ChatBridgeLine;
typedef struct { char from[NET_CHAT_NAME]; char text[NET_CHAT_TEXT]; int src; } ChatBridgeShown;

typedef struct {
    ChatBridgeLine q[CHAT_BRIDGE_QUEUE];
    int q_head, q_count;
    long long last_id;            /* highest bus id seen */
    int primed;                   /* the first reply only sets last_id: a world joining doesn't replay history */
    long long own[CHAT_BRIDGE_OWN];
    int own_next;
    unsigned long queued, dropped, shown;
} ChatBridge;

void chat_bridge_init(ChatBridge *b);
/* The chat server's channel hook: queue a line for the bus if br-should-post says so. Returns 1 when queued. */
int chat_bridge_offer(ChatBridge *b, int slot, const char *channel, const char *name, const char *text);
/* Pop the next queued line as a bus POST body. Returns 0 when nothing is queued (or cap is too small). */
int chat_bridge_next_post(ChatBridge *b, char *json, size_t cap);
/* Remember the id the bus gave one of this host's posts (the loop guard for multi-instance sources). */
void chat_bridge_note_own(ChatBridge *b, long long id);
/* A bus GET reply. Writes up to max lines from other worlds, `from` tagged like "MC:Steve". Returns lines written. */
int chat_bridge_ingest(ChatBridge *b, const char *json, size_t len, ChatBridgeShown *out, int max);
long long chat_bridge_since(const ChatBridge *b);
/* IDUNA's sender_source string -> chat_bridge_rules src-* code (0 = unknown), and the tag a code prints as. */
int chat_bridge_source_code(const char *sender_source);
const char *chat_bridge_tag(int src);

/* ---- the network side: a worker thread speaking HTTPS through curl (POSIX servers only) ---- */
typedef struct ChatBridgeNet ChatBridgeNet;
/* NULL = off (an environment variable is unset, or this platform has no worker). */
ChatBridgeNet *chat_bridge_net_start(void);
/* Main thread, every tick: hand queued lines to the worker, take what it fetched and inject it into General. */
void chat_bridge_net_tick(ChatBridgeNet *n, ChatBridge *b, ChatServer *s, const ChatPeer *peers, unsigned int now_ms,
                          ChatServerSend send, void *ctx);

#endif
