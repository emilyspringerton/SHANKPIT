#ifndef CHAT_CLIENT_H
#define CHAT_CLIENT_H

/* chat_client.h -- the client half of World of Warcraft style chat channels: SHANKPIT as the telepresence app.
 *
 *   typed line --chat_client_submit--> PACKET_CHAT to the server (or a local notice)
 *   PACKET_CHAT from the server --chat_client_receive--> REFLUX rows 1200-1203 (text kept here, by seq)
 *   REFLUX --chat_client_poll (the chat mod)--> WoW-style lines + colour, for the chat pane
 *
 * What it understands (WoW's own words where WoW has them):
 *   text            the sticky chat type (Say until you pick another)
 *   /s /say         say (40 m)            /y /yell /sh /shout   yell (300 m)
 *   /1 .. /10       a numbered channel     /join name /j         join (lowest free number)
 *   /leave n|name   leave                  /w /whisper /t /tell /msg name text   whisper
 *   /r /reply       reply to the last whisper
 *   /chatwho /chatlist [n|name]            who is in a channel
 *   /name newname   claim a display name   /chathelp             these lines
 * Any other /command returns 0, so the lobby can run it as a macro (its existing slash language).
 *
 * Every decision is chat_channel_rules.c (PARENA). No SDL, no GL, no sockets: the caller passes a send callback. */

#include "../common/protocol.h"

#define CHAT_CLIENT_RING 64      /* chan-ring */
#define CHAT_CLIENT_LINE 320

typedef void (*ChatClientSend)(void *ctx, const NetChat *pkt);

typedef struct {
    int seq;                     /* process-wide text seq; 0 = empty slot */
    int kind, code, number;
    char channel[NET_CHAT_NAME], from[NET_CHAT_NAME], to[NET_CHAT_NAME];
    char text[NET_CHAT_TEXT];
} ChatEntry;

typedef struct {
    char channels[11][NET_CHAT_NAME];   /* WoW numbers 1..10 -> channel name; "" = free; [0] unused */
    char wanted[NET_CHAT_NAME];         /* the name to claim; "" = let the server pick (P<slot>) */
    char me[NET_CHAT_NAME];             /* the name the server bound */
    int named, was_connected;
    unsigned int next_hello_ms;
    int sticky_kind, sticky_number;
    char sticky_to[NET_CHAT_NAME];
    char last_whisper_from[NET_CHAT_NAME];
    ChatEntry ring[CHAT_CLIENT_RING];
    int cursor_abs;                     /* REFLUX: next absolute row the chat mod reads */
} ChatClient;

void chat_client_init(ChatClient *c);
/* Every frame. connected: a live server session. wanted: the display name to claim (the IDUNA display name, or
 * SHANKPIT_CHAT_NAME; NULL/"" = the server's P<slot>); it is cleaned to letters, digits, - and _ first. Sends the
 * hello until the server names this client, and forgets channels when the connection goes away. */
void chat_client_tick(ChatClient *c, int connected, const char *wanted, unsigned int now_ms,
                      ChatClientSend send, void *ctx);
/* Enter in the chat box. Returns 1 when handled, 0 for a /command that is not chat (run it as a macro). */
int chat_client_submit(ChatClient *c, const char *line, int connected, ChatClientSend send, void *ctx);
/* One PACKET_CHAT from the server (already copied out of the datagram). */
void chat_client_receive(ChatClient *c, const NetChat *pkt);
/* The chat mod: read new REFLUX rows, write up to max lines with their colour (chan-color). Returns lines written. */
int chat_client_poll(ChatClient *c, char out[][CHAT_CLIENT_LINE], int *colors, int max);
/* The input prompt for the sticky type: "Say: ", "[2. Backroom]: ", "To [Bjorn]: ". */
void chat_client_prompt(const ChatClient *c, char *buf, int cap);

#endif
