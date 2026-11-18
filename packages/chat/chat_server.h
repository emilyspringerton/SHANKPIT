#ifndef CHAT_SERVER_H
#define CHAT_SERVER_H

/* chat_server.h -- the server half of World of Warcraft style chat channels: SHANKPIT as the telepresence app
 * (founder, 2026-11-17: "the telepresence app is shankpit itself ... it will start with world of warcraft style chat
 * channels for back channeling").
 *
 * No sockets here: apps/server/src/main.c hands every PACKET_CHAT to chat_server_handle with a view of the slots
 * (ChatPeer) and a send callback, and calls chat_server_drop when a slot is freed. This file holds the state
 * (each slot's bound display name, the channel table, flood-control windows); every decision -- which ops exist,
 * name and channel-name rules, who hears what, the flood limit, the 10-channel cap -- is
 * chat_channel_rules.c, generated from PARENA/stdlib/shankpit/chat_channel_rules.prn.
 *
 * Names: a slot claims a name with op-name (the client repeats it until answered). A valid, free name is bound to
 * the slot; otherwise the slot keeps "P<slot>" (names of that shape are reserved for the server). Every outgoing
 * message carries the BOUND name -- a client can never speak as someone else. Everyone is in General from their
 * first chat packet on. */

#include "../common/protocol.h"

#define CHAT_SERVER_CHANNELS 64   /* chan-server-max */

/* What the server knows about a slot, for routing: may it chat, which scene, where (world units). */
typedef struct {
    int active;
    int scene;
    float x, y, z;
} ChatPeer;

typedef void (*ChatServerSend)(void *ctx, int to_slot, const NetChat *pkt);
/* Sees every channel line the server routes (the global chat bridge listens here for General). */
typedef void (*ChatServerChannelHook)(void *ctx, int slot, const char *channel, const char *name, const char *text);

typedef struct {
    char name[NET_CHAT_NAME];
    unsigned char member[MAX_CLIENTS];
    int used;
} ChatServerChannel;

typedef struct {
    char names[MAX_CLIENTS][NET_CHAT_NAME];
    unsigned char greeted[MAX_CLIENTS];
    unsigned int rate_start[MAX_CLIENTS];
    int rate_count[MAX_CLIENTS];
    unsigned char rate_told[MAX_CLIENTS];
    ChatServerChannel ch[CHAT_SERVER_CHANNELS];
    unsigned long routed, refused;
    ChatServerChannelHook on_channel;   /* optional; NULL = nobody listening */
    void *on_channel_ctx;
} ChatServer;

void chat_server_init(ChatServer *s);
/* A slot was freed (disconnect, timeout): forget its name, channels and flood window. */
void chat_server_drop(ChatServer *s, int slot);
/* One PACKET_CHAT from slot `from`. peers has MAX_CLIENTS entries, indexed by slot. Returns packets sent. */
int chat_server_handle(ChatServer *s, int from, const NetChat *in, const ChatPeer *peers, unsigned int now_ms,
                       ChatServerSend send, void *ctx);
/* The slot's bound name ("" before it has chatted). */
const char *chat_server_name(const ChatServer *s, int slot);
/* A line from outside this world (the global chat bridge) into a channel, shown as `from` (already tagged, e.g.
 * "MC:Steve"). Goes to every live member, never to the channel hook (so it can't loop back out). Returns sent. */
int chat_server_inject(ChatServer *s, const char *channel, const char *from, const char *text, const ChatPeer *peers,
                       unsigned int now_ms, ChatServerSend send, void *ctx);

#endif
