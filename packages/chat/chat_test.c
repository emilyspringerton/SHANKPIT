/* packages/chat/chat_test.c -- WoW-style chat channels, headless: one ChatServer and five ChatClients in one process,
 * packets passed through a queue exactly as the lobby and the server pass them, every line read back through the
 * REFLUX chat mod. make test-chat-channels */
#include "chat_client.h"
#include "chat_server.h"
#include "chat_channel_rules.h"
#include "../reflux/reflux_runtime.h"

#include <stdio.h>
#include <string.h>

static int passed, failed;
#define EQI(got, want, what) do { long g_ = (long)(got), w_ = (long)(want); \
    if (g_ == w_) passed++; else { failed++; printf("FAIL: %s (got %ld, want %ld)\n", what, g_, w_); } } while (0)
#define HAS(s, sub, what) do { if (strstr((s), (sub))) passed++; else { failed++; printf("FAIL: %s\n  wanted: %s\n  in: %s\n", what, (sub), (s)); } } while (0)
#define LACKS(s, sub, what) do { if (!strstr((s), (sub))) passed++; else { failed++; printf("FAIL: %s\n  did not want: %s\n", what, (sub)); } } while (0)

#define NCLIENT 6   /* slots 1..5 */
static ChatServer srv;
static ChatPeer peers[MAX_CLIENTS];
static ChatClient cl[NCLIENT];
static int online[NCLIENT];
static unsigned int now_ms = 100000;

/* the wire: a FIFO of (to-server from slot k) or (to-client slot k) packets */
typedef struct { int to_server, slot; NetChat pkt; } Wire;
static Wire q[4096];
static int qhead, qtail;
static int wire_drop_next_to_client;   /* simulate UDP loss of the next server->client packet */

static void client_send(void *ctx, const NetChat *p) {
    int slot = (int)(size_t)ctx;
    q[qtail % 4096].to_server = 1; q[qtail % 4096].slot = slot; q[qtail % 4096].pkt = *p; qtail++;
}
static void server_send(void *ctx, int to, const NetChat *p) {
    (void)ctx;
    if (wire_drop_next_to_client) { wire_drop_next_to_client = 0; return; }
    q[qtail % 4096].to_server = 0; q[qtail % 4096].slot = to; q[qtail % 4096].pkt = *p; qtail++;
}
static void pump(void) {
    while (qhead < qtail) {
        Wire w = q[qhead % 4096]; qhead++;
        if (w.to_server) chat_server_handle(&srv, w.slot, &w.pkt, peers, now_ms, server_send, NULL);
        else if (online[w.slot]) chat_client_receive(&cl[w.slot], &w.pkt);
    }
}

/* everything client k's chat mod prints, accumulated */
static char seen[NCLIENT][16384];
static int last_color[NCLIENT];
static void drain(void) {
    char out[16][CHAT_CLIENT_LINE];
    int colors[16];
    for (int k = 1; k < NCLIENT; k++) {
        int n;
        while ((n = chat_client_poll(&cl[k], out, colors, 16)) > 0) {
            for (int i = 0; i < n; i++) {
                size_t len = strlen(seen[k]);
                snprintf(seen[k] + len, sizeof(seen[k]) - len, "%s\n", out[i]);
                last_color[k] = colors[i];
            }
        }
    }
}
static void clear_seen(void) { drain(); for (int k = 0; k < NCLIENT; k++) seen[k][0] = '\0'; }
static void say(int k, const char *line) { chat_client_submit(&cl[k], line, online[k], client_send, (void *)(size_t)k); pump(); drain(); }
static void tick_all(const char *const *names) {
    for (int k = 1; k < NCLIENT; k++)
        if (online[k]) chat_client_tick(&cl[k], 1, names[k], now_ms, client_send, (void *)(size_t)k);
    pump(); drain();
}

int main(void) {
    reflux_host_reset();
    chat_server_init(&srv);
    /* slots 1-3 in scene 0 along x; slot 4 in scene 1; slot 5 joins later */
    float xs[NCLIENT] = { 0, 0, 10, 100, 0, 5 };
    for (int k = 1; k < NCLIENT; k++) {
        chat_client_init(&cl[k]);
        peers[k].active = (k <= 4); online[k] = (k <= 4);
        peers[k].scene = (k == 4) ? 1 : 0;
        peers[k].x = xs[k];
    }
    const char *names[NCLIENT] = { "", "Ingrid", "Bjorn", "Emily Springerton", "", "" };

    /* ---- hello: names, General as /1 -------------------------------------------------------------------- */
    tick_all(names);
    EQI(cl[1].named, 1, "client 1 is named");
    EQI(strcmp(cl[1].me, "Ingrid"), 0, "as the name it asked for");
    EQI(strcmp(cl[3].me, "EmilySpringerton"), 0, "a display name with a space is cleaned to a valid name");
    EQI(strcmp(cl[4].me, "P4"), 0, "no name: the server's P<slot>");
    EQI(strcmp(cl[1].channels[1], "General"), 0, "everyone starts in General as /1");
    HAS(seen[1], "You are known as Ingrid.", "the client is told its name");
    HAS(seen[1], "Joined Channel: [1. General]", "and that it joined General");
    EQI(last_color[1], chan_color(chk_system()), "notices are system-coloured");

    clear_seen();
    now_ms += 5000;
    tick_all(names);
    EQI(qtail - qhead, 0, "no hello once named");
    /* a repeated hello (as after a lost answer) is answered again but shows nothing new */
    cl[1].named = 0; cl[1].next_hello_ms = 0;
    tick_all(names);
    EQI(cl[1].named, 1, "the repeated hello is answered");
    EQI(seen[1][0], 0, "and the client shows nothing twice");
    /* a lost answer: the hello repeats after chan-hello-ms */
    cl[2].named = 0; cl[2].next_hello_ms = 0;
    wire_drop_next_to_client = 1;
    tick_all(names);
    EQI(cl[2].named, 0, "the answer was lost");
    now_ms += 1000; tick_all(names);
    EQI(cl[2].named, 0, "no resend before chan-hello-ms");
    now_ms += 1500; tick_all(names);
    EQI(cl[2].named, 1, "the hello repeats and completes");

    /* ---- names ------------------------------------------------------------------------------------------- */
    clear_seen();
    say(4, "/name Ingrid");
    HAS(seen[4], "The name 'Ingrid' is taken.", "a taken name is refused");
    EQI(strcmp(cl[4].me, "P4"), 0, "and the slot keeps its name");
    say(4, "/name P7");
    HAS(seen[4], "'P7' can't be a name", "P<digits> is reserved for the server");
    say(4, "/name Dagny");
    EQI(strcmp(cl[4].me, "Dagny"), 0, "a free, valid name is taken");
    EQI(strcmp(chat_server_name(&srv, 4), "Dagny"), 0, "on the server too");

    /* ---- General (/1) reaches everyone, any scene ---------------------------------------------------------- */
    clear_seen();
    now_ms += 6000;
    say(1, "/1 hello all");
    for (int k = 1; k <= 4; k++) HAS(seen[k], "[1. General] [Ingrid]: hello all", "General reaches every member, any scene");
    EQI(last_color[2], chan_color(chk_channel()), "channel lines are channel-coloured");
    char prompt[64];
    chat_client_prompt(&cl[1], prompt, sizeof(prompt));
    EQI(strcmp(prompt, "[1. General]: "), 0, "a channel is sticky: the prompt says where Enter goes");

    /* ---- custom channels: lowest free number, each client's own numbering ------------------------------------ */
    clear_seen();
    say(1, "/join backroom");
    say(2, "/join backroom");
    say(3, "/join zeta");
    say(3, "/join Backroom");
    HAS(seen[1], "Joined Channel: [2. backroom]", "a new channel takes the lowest free number");
    HAS(seen[3], "Joined Channel: [2. zeta]", "client 3 joined zeta first");
    HAS(seen[3], "Joined Channel: [3. backroom]", "so backroom is its /3 (names are case-insensitive)");
    clear_seen();
    say(1, "/2 psst");
    HAS(seen[1], "[2. backroom] [Ingrid]: psst", "the sender sees its channel line");
    HAS(seen[2], "[2. backroom] [Ingrid]: psst", "members see it under their own number");
    HAS(seen[3], "[3. backroom] [Ingrid]: psst", "client 3 sees it as /3");
    EQI(seen[4][0], 0, "a non-member sees nothing");
    clear_seen();
    say(1, "again");
    HAS(seen[2], "[2. backroom] [Ingrid]: again", "plain text goes to the sticky channel");
    say(1, "/join backroom");
    HAS(seen[1], "You are already in [2. backroom].", "joining twice is shown, not sent");

    clear_seen();
    say(1, "/chatwho 2");
    HAS(seen[1], "[2. backroom] members: Ingrid, Bjorn, EmilySpringerton", "who lists a channel's members");
    say(4, "/chatwho backroom");
    HAS(seen[4], "You are not in channel 'backroom'.", "who needs membership");

    clear_seen();
    say(2, "/leave 2");
    HAS(seen[2], "Left Channel: [2. backroom]", "leaving says so");
    say(2, "/2 hello?");
    HAS(seen[2], "You are not in channel 2.", "a number you left is refused locally");
    say(1, "/2 bye bjorn");
    LACKS(seen[2], "bye bjorn", "a member who left hears nothing");
    HAS(seen[3], "[3. backroom] [Ingrid]: bye bjorn", "the others still do");

    /* ---- say and yell: distance and scene ------------------------------------------------------------------ */
    clear_seen();
    now_ms += 6000;
    say(1, "/s hi there");
    HAS(seen[1], "[Ingrid] says: hi there", "the speaker hears its own say");
    HAS(seen[2], "[Ingrid] says: hi there", "10 m away hears a say");
    LACKS(seen[3], "hi there", "100 m away does not");
    LACKS(seen[4], "hi there", "another scene does not");
    EQI(last_color[2], chan_color(chk_say()), "say is say-coloured");
    chat_client_prompt(&cl[1], prompt, sizeof(prompt));
    EQI(strcmp(prompt, "Say: "), 0, "say is sticky");
    clear_seen();
    say(1, "/y HEY");
    HAS(seen[3], "[Ingrid] yells: HEY", "100 m away hears a yell");
    LACKS(seen[4], "HEY", "another scene still does not");
    chat_client_prompt(&cl[1], prompt, sizeof(prompt));
    EQI(strcmp(prompt, "Say: "), 0, "yell is not sticky");

    /* ---- whispers -------------------------------------------------------------------------------------------- */
    clear_seen();
    now_ms += 6000;
    say(1, "/w Bjorn secret plan");
    HAS(seen[2], "[Ingrid] whispers: secret plan", "the target gets the whisper");
    HAS(seen[1], "To [Bjorn]: secret plan", "the sender gets the echo");
    LACKS(seen[3], "secret plan", "nobody else");
    LACKS(seen[4], "secret plan", "nobody else at all");
    EQI(last_color[2], chan_color(chk_whisper()), "whispers are whisper-coloured");
    say(2, "/r got it");
    HAS(seen[1], "[Bjorn] whispers: got it", "/r replies to the last whisper");
    say(1, "/w nobody hello");
    HAS(seen[1], "No player named 'nobody' is here.", "an unknown name is reported");
    say(3, "/r anyone?");
    HAS(seen[3], "No one has whispered you yet.", "/r with nobody to answer");
    chat_client_prompt(&cl[2], prompt, sizeof(prompt));
    EQI(strcmp(prompt, "To [Ingrid]: "), 0, "a whisper is sticky");

    /* ---- limits ---------------------------------------------------------------------------------------------- */
    clear_seen();
    now_ms += 6000;
    for (int i = 0; i < 8; i++) say(4, "/s spam");
    int got = 0;
    for (const char *p = seen[4]; (p = strstr(p, "[Dagny] says: spam")) != NULL; p++) got++;
    EQI(got, chan_rate_max(), "flood control lets chan-rate-max messages through per window");
    HAS(seen[4], "You are sending messages too fast.", "and says why the rest were dropped");
    const char *tf = strstr(seen[4], "too fast");
    EQI(tf && strstr(tf + 1, "too fast") == NULL, 1, "once per window, not once per message");
    now_ms += 6000;
    clear_seen();
    say(4, "/s back again");
    HAS(seen[4], "[Dagny] says: back again", "the next window opens again");

    clear_seen();
    char longmsg[260] = "/1 ";
    memset(longmsg + 3, 'x', 205); longmsg[208] = '\0';
    say(1, longmsg);
    HAS(seen[1], "Messages are 1-200 characters.", "an over-long message is refused, not cut");
    char join[32];
    for (int i = 0; i < 12; i++) { snprintf(join, sizeof(join), "/join c%d", i); now_ms += 1000; say(4, join); }
    HAS(seen[4], "Joined Channel: [10. c8]", "channels fill numbers up to 10");
    HAS(seen[4], "You can be in at most 10 channels.", "the 11th is refused");
    say(1, "/join bad!name");
    HAS(seen[1], "'bad!name' can't be a channel", "a bad channel name is refused");
    say(1, "/frobnicate");
    EQI(chat_client_submit(&cl[1], "/frobnicate", 1, client_send, (void *)(size_t)1), 0, "a non-chat /command is left for the macro language");

    /* ---- offline, disconnect and rejoin ------------------------------------------------------------------- */
    clear_seen();
    online[5] = 0;
    say(5, "talking to myself");
    HAS(seen[5], "[You] says: talking to myself", "offline: the old local echo");
    say(5, "/join anything");
    HAS(seen[5], "Channels and whispers need a server: join a game first.", "offline: channels say why they can't work");

    clear_seen();
    chat_server_drop(&srv, 1);
    peers[1].active = 0; online[1] = 0;
    chat_client_tick(&cl[1], 0, "Ingrid", now_ms, client_send, (void *)(size_t)1);
    EQI(cl[1].channels[1][0], 0, "a client that loses its server forgets its channels");
    say(4, "/name Ingrid");
    EQI(strcmp(cl[4].me, "Ingrid"), 0, "a dropped slot's name is free again");
    peers[5].active = 1; online[5] = 1;
    const char *n5[NCLIENT] = { "", "", "", "", "", "Freya" };
    chat_client_tick(&cl[5], 1, n5[5], now_ms, client_send, (void *)(size_t)5); pump(); drain();
    HAS(seen[5], "Joined Channel: [1. General]", "a new player joins General");
    say(3, "/1 welcome Freya");
    HAS(seen[5], "[1. General] [EmilySpringerton]: welcome Freya", "and hears it");

    /* ---- the server ignores what it must ------------------------------------------------------------------- */
    NetChat bad;
    memset(&bad, 'A', sizeof(bad));
    bad.hdr.type = PACKET_CHAT;
    bad.op = (unsigned char)op_join();
    EQI(chat_server_handle(&srv, 0, &bad, peers, now_ms, server_send, NULL), 0, "slot 0 is not a chat slot");
    EQI(chat_server_handle(&srv, 1, &bad, peers, now_ms, server_send, NULL), 0, "an inactive slot is ignored");
    now_ms += 6000;
    int r = chat_server_handle(&srv, 3, &bad, peers, now_ms, server_send, NULL);
    EQI(r, 1, "unterminated strings are clipped, not overrun (one notice back)");
    pump(); drain();
    bad.op = 99;
    EQI(chat_server_handle(&srv, 3, &bad, peers, now_ms, server_send, NULL), 0, "an unknown op is ignored");

    /* ---- REFLUX: the server dispatched a routed row per message ------------------------------------------- */
    int routed = 0;
    for (int i = 0; i < reflux_host_log_size(); i++) if (reflux_host_action_type_at(i) == REFLUX_ACTION_CHAT_ROUTED) routed++;
    EQI(routed > 0, 1, "the server dispatches CHAT_ROUTED rows for server mods");
    EQI(reflux_action_is_chat(1204) && !reflux_action_is_chat(1220), 1, "the chat family is 1200-1219");

    printf("chat_channels: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
