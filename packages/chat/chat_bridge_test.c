/* packages/chat/chat_bridge_test.c -- the global chat bridge's core, headless: what goes out to IDUNA's chat bus,
 * what comes in, tags, loop guards, pacing, and the chat server's hook and inject. make test-chat-bridge */
#include "chat_bridge.h"
#include "chat_bridge_rules.h"
#include "chat_channel_rules.h"
#include "../reflux/reflux_runtime.h"

#include <stdio.h>
#include <string.h>

static int passed, failed;
#define EQI(got, want, what) do { long g_ = (long)(got), w_ = (long)(want); \
    if (g_ == w_) passed++; else { failed++; printf("FAIL: %s (got %ld, want %ld)\n", what, g_, w_); } } while (0)
#define HAS(s, sub, what) do { if (strstr((s), (sub))) passed++; else { failed++; printf("FAIL: %s\n  wanted: %s\n  in: %s\n", what, (sub), (s)); } } while (0)

static ChatBridge br;
static void hook(void *ctx, int slot, const char *channel, const char *name, const char *text) {
    (void)ctx;
    chat_bridge_offer(&br, slot, channel, name, text);
}

static NetChat sent[64];
static int sent_to[64], nsent;
static void server_send(void *ctx, int to, const NetChat *p) { (void)ctx; if (nsent < 64) { sent[nsent] = *p; sent_to[nsent] = to; nsent++; } }

int main(void) {
    reflux_host_reset();
    chat_bridge_init(&br);

    /* ---- out: only General, queued in order, the oldest dropped when the bus is away ---------------------- */
    EQI(chat_bridge_offer(&br, 1, "backroom", "Ingrid", "secret"), 0, "a custom channel is a back channel: it stays");
    EQI(chat_bridge_offer(&br, 1, "General", "Ingrid", "hello \"world\" \\o/"), 1, "General goes out");
    EQI(chat_bridge_offer(&br, 2, "general", "Bjorn", "hi"), 1, "General is matched case-insensitively");
    char json[2048];
    EQI(chat_bridge_next_post(&br, json, sizeof(json)), 1, "a queued line becomes a POST body");
    HAS(json, "\"channel\":\"shankpit\"", "on the bus's shankpit channel");
    HAS(json, "\"sender_source\":\"shankpit_server\"", "as a SHANKPIT world");
    HAS(json, "\"sender_name\":\"Ingrid\"", "under the speaker's bound name");
    HAS(json, "\"body\":\"hello \\\"world\\\" \\\\o/\"", "quotes and backslashes escaped");
    EQI(chat_bridge_next_post(&br, json, sizeof(json)), 1, "the second line next");
    HAS(json, "\"sender_name\":\"Bjorn\"", "in order");
    EQI(chat_bridge_next_post(&br, json, sizeof(json)), 0, "then the queue is empty");
    for (int i = 0; i < br_post_queue() + 5; i++) {
        char t[16]; snprintf(t, sizeof(t), "line %d", i);
        chat_bridge_offer(&br, 1, "General", "Ingrid", t);
    }
    EQI(br.q_count, br_post_queue(), "the queue holds br-post-queue lines");
    EQI(br.dropped, 5, "and drops the oldest past that");
    chat_bridge_next_post(&br, json, sizeof(json));
    HAS(json, "\"body\":\"line 5\"", "so the oldest still queued is line 5");
    while (chat_bridge_next_post(&br, json, sizeof(json))) {}
    int out_rows = 0;
    for (int i = 0; i < reflux_host_log_size(); i++) if (reflux_host_action_type_at(i) == rx_bridge_out()) out_rows++;
    EQI(out_rows, 2 + br_post_queue() + 5, "every offered General line is a CHAT_BRIDGE_OUT REFLUX row");

    /* ---- in: priming pages through the oldest-first bus without showing history ---------------------------- */
    {
        ChatBridge pb;
        chat_bridge_init(&pb);
        char page[16384];
        size_t pl = (size_t)snprintf(page, sizeof(page), "[");
        for (int i = 0; i < br_max_per_poll(); i++)
            pl += (size_t)snprintf(page + pl, sizeof(page) - pl, "%s{\"id\":%d,\"sender_name\":\"Old\",\"sender_source\":\"gfd_server\",\"body\":\"history\"}", i ? "," : "", i + 1);
        snprintf(page + pl, sizeof(page) - pl, "]");
        ChatBridgeShown sh[64];
        EQI(chat_bridge_ingest(&pb, page, strlen(page), sh, 64), 0, "a full first page shows nothing");
        EQI(pb.primed, 0, "and is not the end of the bus yet (oldest first)");
        EQI(chat_bridge_since(&pb), br_max_per_poll(), "the cursor pages forward");
        const char *tail = "[{\"id\":26,\"sender_name\":\"Old\",\"sender_source\":\"gfd_server\",\"body\":\"still history\"}]";
        EQI(chat_bridge_ingest(&pb, tail, strlen(tail), sh, 64), 0, "a short page shows nothing either");
        EQI(pb.primed, 1, "but ends priming at the newest line");
        const char *fresh = "[{\"id\":27,\"sender_name\":\"New\",\"sender_source\":\"gfd_server\",\"body\":\"now\"}]";
        EQI(chat_bridge_ingest(&pb, fresh, strlen(fresh), sh, 64), 1, "after priming, new lines show");
    }

    /* ---- in: the first reply primes, then other worlds show tagged ----------------------------------------- */
    const char *first = "[{\"id\":40,\"channel\":\"gta7\",\"sender_name\":\"Steve\",\"sender_source\":\"einhorn_survival\",\"body\":\"old news\"},"
                        "{\"id\":41,\"channel\":\"yell\",\"sender_name\":\"Goblin\",\"sender_source\":\"gfd_server\",\"body\":\"older\"}]";
    ChatBridgeShown shown[64];
    EQI(chat_bridge_ingest(&br, first, strlen(first), shown, 64), 0, "the first reply only primes: no history replayed");
    EQI(chat_bridge_since(&br), 41, "the cursor starts at the newest id");
    chat_bridge_note_own(&br, 44);
    const char *second =
        "[{\"id\":42,\"channel\":\"gta7\",\"sender_name\":\"Steve\",\"sender_source\":\"einhorn_survival\",\"body\":\"hi from {minecraft}\"},"
        "{\"id\":43,\"channel\":\"yell\",\"sender_name\":\"Goblin\",\"sender_source\":\"gfd_server\",\"body\":\"line\\nbreak \\u00e9\"},"
        "{\"id\":44,\"channel\":\"shankpit\",\"sender_name\":\"Ingrid\",\"sender_source\":\"shankpit_server\",\"body\":\"my own line\"},"
        "{\"id\":45,\"channel\":\"shankpit\",\"sender_name\":\"Freya\",\"sender_source\":\"shankpit_server\",\"body\":\"from another world\"},"
        "{\"id\":46,\"channel\":\"say\",\"sender_name\":\"X\",\"sender_source\":\"mystery\",\"body\":\"unknown\"},"
        "{\"id\":47,\"channel\":\"shankpit\",\"sender_name\":\"Pip\",\"sender_source\":\"shankpit_client\",\"body\":\"from a phone\"}]";
    int k = chat_bridge_ingest(&br, second, strlen(second), shown, 64);
    EQI(k, 4, "four lines from other worlds");
    EQI(strcmp(shown[0].from, "MC:Steve"), 0, "a Minecraft line is tagged MC");
    EQI(strcmp(shown[0].text, "hi from {minecraft}"), 0, "braces inside a string don't confuse the parser");
    EQI(strcmp(shown[1].from, "GFD:Goblin"), 0, "a GFD line is tagged GFD");
    EQI(strcmp(shown[1].text, "line break ?"), 0, "control characters become spaces, \\u escapes '?'");
    EQI(strcmp(shown[2].from, "SHANKPIT:Freya"), 0, "another SHANKPIT world is shown (multi-instance source)");
    EQI(strcmp(shown[3].from, "SP:Pip"), 0, "the SHANKPIT phone app is shown");
    EQI(chat_bridge_since(&br), 47, "the cursor moves past everything read, shown or not");
    EQI(chat_bridge_ingest(&br, second, strlen(second), shown, 64), 0, "a repeated reply shows nothing twice");

    /* ---- pacing: at most br-max-per-poll lines a poll, the rest next time --------------------------------- */
    char big[16384];
    size_t len = (size_t)snprintf(big, sizeof(big), "[");
    for (int i = 0; i < 30; i++)
        len += (size_t)snprintf(big + len, sizeof(big) - len, "%s{\"id\":%d,\"sender_name\":\"P%d\",\"sender_source\":\"bigo_server\",\"body\":\"b%d\"}",
                                i ? "," : "", 100 + i, i, i);
    snprintf(big + len, sizeof(big) - len, "]");
    k = chat_bridge_ingest(&br, big, strlen(big), shown, 64);
    EQI(k, br_max_per_poll(), "one poll shows at most br-max-per-poll lines");
    EQI(chat_bridge_since(&br), 100 + br_max_per_poll() - 1, "and stops its cursor there");
    k = chat_bridge_ingest(&br, big, strlen(big), shown, 64);
    EQI(k, 30 - br_max_per_poll(), "the next poll shows the rest");
    EQI(strcmp(shown[0].from, "BIGO:P25"), 0, "starting where the last one stopped");

    /* ---- hostile input -------------------------------------------------------------------------------------- */
    const char *bad = "[{\"id\":\"x\"},{\"id\":900,\"sender_source\":\"gfd_server\"},{\"id\":901,\"sender_name\":\"\",\"sender_source\":\"gfd_server\",\"body\":\"no name\"},"
                      "{\"id\":902,\"sender_name\":\"AVeryLongGoblinNameIndeed\",\"sender_source\":\"gfd_server\",\"body\":\"long name\"},{\"id\":903";
    k = chat_bridge_ingest(&br, bad, strlen(bad), shown, 64);
    EQI(k, 1, "objects without an id, a name or a body are skipped; a cut-off reply is not an error");
    EQI(strcmp(shown[0].from, "GFD:AVeryLongGoblinN"), 0, "a long name is cut to br-name-max before its tag");

    /* ---- the chat server: General lines reach the hook; injected lines reach members and never loop back -- */
    static ChatServer srv;
    chat_server_init(&srv);
    srv.on_channel = hook;
    ChatPeer peers[MAX_CLIENTS];
    memset(peers, 0, sizeof(peers));
    peers[1].active = peers[2].active = 1;
    NetChat in;
    memset(&in, 0, sizeof(in));
    in.hdr.type = PACKET_CHAT;
    in.op = (unsigned char)op_name();
    snprintf(in.from, sizeof(in.from), "Ingrid");
    chat_server_handle(&srv, 1, &in, peers, 1000, server_send, NULL);
    snprintf(in.from, sizeof(in.from), "Bjorn");
    chat_server_handle(&srv, 2, &in, peers, 1000, server_send, NULL);
    chat_bridge_init(&br);
    memset(&in, 0, sizeof(in));
    in.hdr.type = PACKET_CHAT;
    in.op = (unsigned char)op_channel();
    snprintf(in.channel, sizeof(in.channel), "General");
    snprintf(in.text, sizeof(in.text), "hello worlds");
    chat_server_handle(&srv, 1, &in, peers, 1000, server_send, NULL);
    EQI(br.q_count, 1, "a General line from a player reaches the bridge through the hook");
    EQI(chat_bridge_next_post(&br, json, sizeof(json)), 1, "and goes out");
    HAS(json, "\"sender_name\":\"Ingrid\"", "under the server's bound name");
    nsent = 0;
    int got = chat_server_inject(&srv, "General", "MC:Steve", "hi from minecraft", peers, 2000, server_send, NULL);
    EQI(got, 2, "an injected line reaches every General member");
    EQI(strcmp(sent[0].from, "MC:Steve"), 0, "shown with its tag");
    EQI(sent[0].kind, chk_channel(), "as a General channel line");
    EQI(br.q_count, 0, "and is never offered back to the bridge (no loop)");
    EQI(chat_server_inject(&srv, "nosuch", "MC:Steve", "x", peers, 2000, server_send, NULL), 0, "no such channel: nothing sent");

    printf("chat_bridge: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
