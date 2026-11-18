/* packages/chat/chat_client.c -- plumbing for the client half of the chat channels; see chat_client.h.
 * Decisions are chat_channel_rules.c (PARENA). */
#include "chat_client.h"
#include "chat_channel_rules.h"
#include "../reflux/reflux_runtime.h"

#include <stdio.h>
#include <string.h>

/* Text seqs are unique across every ChatClient in the process, so a REFLUX row's seq only ever matches the ring of
 * the client that stored it. */
static int g_chat_text_seq = 0;

static void field(char *dst, size_t dcap, const char *src, size_t scap) {
    size_t n = 0;
    while (n < scap && src[n]) n++;
    if (n >= dcap) n = dcap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}
#define SET(dst, src) field((dst), sizeof(dst), (src), strlen(src) + 1)

static char lower(char ch) { return (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch; }
static int ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) if (lower(*a) != lower(*b)) return 0;
    return *a == *b;
}

/* Keep only the characters a name may hold (chan-name-char-ok), at most chan-name-max of them. */
static void clean_name(char *dst, size_t cap, const char *src) {
    size_t n = 0;
    int max = chan_name_max();
    for (; src && *src && n + 1 < cap && (int)n < max; src++)
        if (chan_name_char_ok((unsigned char)*src)) dst[n++] = *src;
    dst[n] = '\0';
}

static int name_ok(const char *s) {
    int n = (int)strlen(s);
    if (!chan_name_len_ok(n)) return 0;
    for (int i = 0; i < n; i++) if (!chan_name_char_ok((unsigned char)s[i])) return 0;
    return 1;
}

static int used_mask(const ChatClient *c) {
    int m = 0;
    for (int n = 1; n <= 10; n++) if (c->channels[n][0]) m |= 1 << n;
    return m;
}

static int number_of(const ChatClient *c, const char *name) {
    if (!name || !name[0]) return 0;
    for (int n = 1; n <= 10; n++) if (c->channels[n][0] && ieq(c->channels[n], name)) return n;
    return 0;
}

/* A channel argument: a number 1..10 or a name; returns its number, 0 if not joined. */
static int resolve(const ChatClient *c, const char *arg) {
    int n = 0, digits = 1;
    for (const char *p = arg; *p; p++) { if (*p < '0' || *p > '9') { digits = 0; break; } n = n * 10 + (*p - '0'); if (n > 99) break; }
    if (digits && arg[0]) return (chan_number_ok(n) && c->channels[n][0]) ? n : 0;
    return number_of(c, arg);
}

static ChatEntry *store(ChatClient *c) {
    int seq = ++g_chat_text_seq;
    if (seq <= 0) { g_chat_text_seq = 1; seq = 1; }   /* wrap: seqs stay positive */
    ChatEntry *e = &c->ring[seq % CHAT_CLIENT_RING];
    memset(e, 0, sizeof(*e));
    e->seq = seq;
    return e;
}

static void local_notice(ChatClient *c, int code, const char *channel, const char *text) {
    ChatEntry *e = store(c);
    e->kind = chk_system();
    e->code = code;
    if (channel) SET(e->channel, channel);
    if (text) SET(e->text, text);
    e->number = number_of(c, e->channel);
    reflux_host_dispatch(rx_chat_notice(), code, e->number, e->seq);
}

static void out_init(NetChat *o, int op) {
    memset(o, 0, sizeof(*o));
    o->hdr.type = PACKET_CHAT;
    o->op = (unsigned char)op;
}

void chat_client_init(ChatClient *c) {
    memset(c, 0, sizeof(*c));
    c->sticky_kind = chk_say();
    c->cursor_abs = reflux_host_total_dispatched();
}

void chat_client_tick(ChatClient *c, int connected, const char *wanted, unsigned int now_ms,
                      ChatClientSend send, void *ctx) {
    char w[NET_CHAT_NAME];
    clean_name(w, sizeof(w), wanted);
    if (strcmp(w, c->wanted) != 0) { SET(c->wanted, w); c->named = 0; c->next_hello_ms = now_ms; }
    if (!connected) {
        if (c->was_connected) {           /* the session ended: its channels and name went with it */
            memset(c->channels, 0, sizeof(c->channels));
            c->named = 0;
            c->me[0] = '\0';
            if (c->sticky_kind == chk_channel()) c->sticky_kind = chk_say();
        }
        c->was_connected = 0;
        return;
    }
    if (!c->was_connected) { c->was_connected = 1; c->named = 0; c->next_hello_ms = now_ms; }
    if (c->named || (int)(now_ms - c->next_hello_ms) < 0) return;
    NetChat o;
    out_init(&o, op_name());
    SET(o.from, c->wanted);
    send(ctx, &o);
    c->next_hello_ms = now_ms + (unsigned int)chan_hello_ms();
}

/* Send a message of kind `kind` (say, yell, channel n, whisper to `to`). */
static int send_message(ChatClient *c, int kind, int number, const char *to, const char *text, int connected,
                        ChatClientSend send, void *ctx) {
    if (!chan_msg_len_ok((int)strlen(text))) { local_notice(c, note_err_len(), NULL, NULL); return 1; }
    if (!connected) {
        if (kind == chk_say() || kind == chk_yell()) {   /* offline: the old local echo, as a say */
            ChatEntry *e = store(c);
            e->kind = kind;
            SET(e->from, c->me[0] ? c->me : "You");
            SET(e->text, text);
            reflux_host_dispatch(rx_chat_message(), kind, 0, e->seq);
        } else {
            local_notice(c, note_err_offline(), NULL, NULL);
        }
        return 1;
    }
    NetChat o;
    if (kind == chk_channel()) {
        if (!c->channels[number][0]) { local_notice(c, note_err_not_member(), NULL, NULL); return 1; }
        out_init(&o, op_channel());
        SET(o.channel, c->channels[number]);
    } else if (kind == chk_whisper_to()) {
        out_init(&o, op_whisper());
        SET(o.to, to);
    } else {
        out_init(&o, kind == chk_yell() ? op_yell() : op_say());
    }
    SET(o.text, text);
    send(ctx, &o);
    if (chan_sticky(kind)) {
        c->sticky_kind = kind;
        c->sticky_number = number;
        if (to) SET(c->sticky_to, to);
    }
    return 1;
}

static int send_op(ChatClient *c, int op, const char *channel, int connected, ChatClientSend send, void *ctx) {
    if (!connected) { local_notice(c, note_err_offline(), NULL, NULL); return 1; }
    NetChat o;
    out_init(&o, op);
    if (channel) SET(o.channel, channel);
    send(ctx, &o);
    return 1;
}

static const char *skip_spaces(const char *p) { while (*p == ' ' || *p == '\t') p++; return p; }

/* Split "word rest" into word (cleaned to cap) and the rest after spaces. */
static const char *word(const char *p, char *w, size_t cap) {
    size_t n = 0;
    p = skip_spaces(p);
    while (*p && *p != ' ' && *p != '\t') { if (n + 1 < cap) w[n++] = *p; p++; }
    w[n] = '\0';
    return skip_spaces(p);
}

static void help(ChatClient *c) {
    static const char *lines[] = {
        "Chat: type to talk (Say until you pick another). /s say, /y yell, /1-/10 a channel.",
        "/join name, /leave n, /w name text whispers, /r replies, /chatwho n lists a channel.",
        "/name newname claims a display name.",
    };
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) local_notice(c, 0, NULL, lines[i]);
}

int chat_client_submit(ChatClient *c, const char *line, int connected, ChatClientSend send, void *ctx) {
    line = skip_spaces(line ? line : "");
    if (!*line) return 1;
    if (line[0] != '/') {
        int kind = c->sticky_kind, number = c->sticky_number;
        if (kind == chk_channel() && !c->channels[number][0]) kind = chk_say();   /* left it: back to Say */
        if (kind == chk_whisper_to() && !c->sticky_to[0]) kind = chk_say();
        if (kind != chk_channel() && kind != chk_whisper_to()) kind = chk_say();
        return send_message(c, kind, number, c->sticky_to, line, connected, send, ctx);
    }
    char cmd[16], arg[NET_CHAT_NAME + 8];
    const char *rest = word(line + 1, cmd, sizeof(cmd));
    for (char *p = cmd; *p; p++) *p = lower(*p);

    int n = 0, digits = cmd[0] != '\0';
    for (const char *p = cmd; *p; p++) { if (*p < '0' || *p > '9') { digits = 0; break; } n = n * 10 + (*p - '0'); if (n > 99) break; }
    if (digits) {
        if (!chan_number_ok(n) || !c->channels[n][0]) { local_notice(c, note_err_not_member(), NULL, cmd); return 1; }
        return send_message(c, chk_channel(), n, NULL, rest, connected, send, ctx);
    }
    if (!strcmp(cmd, "s") || !strcmp(cmd, "say")) return send_message(c, chk_say(), 0, NULL, rest, connected, send, ctx);
    if (!strcmp(cmd, "y") || !strcmp(cmd, "yell") || !strcmp(cmd, "sh") || !strcmp(cmd, "shout"))
        return send_message(c, chk_yell(), 0, NULL, rest, connected, send, ctx);
    if (!strcmp(cmd, "w") || !strcmp(cmd, "whisper") || !strcmp(cmd, "t") || !strcmp(cmd, "tell") || !strcmp(cmd, "msg")) {
        rest = word(rest, arg, sizeof(arg));
        if (!arg[0]) { help(c); return 1; }
        return send_message(c, chk_whisper_to(), 0, arg, rest, connected, send, ctx);
    }
    if (!strcmp(cmd, "r") || !strcmp(cmd, "reply")) {
        if (!c->last_whisper_from[0]) { local_notice(c, note_err_no_player(), NULL, ""); return 1; }
        return send_message(c, chk_whisper_to(), 0, c->last_whisper_from, rest, connected, send, ctx);
    }
    if (!strcmp(cmd, "join") || !strcmp(cmd, "j")) {
        word(rest, arg, sizeof(arg));
        if (!name_ok(arg)) { local_notice(c, note_err_channel_bad(), arg, NULL); return 1; }
        if (number_of(c, arg)) { local_notice(c, note_joined(), arg, NULL); return 1; }   /* already in it: show it */
        if (chan_first_free(used_mask(c)) < 0) { local_notice(c, note_err_too_many(), arg, NULL); return 1; }
        return send_op(c, op_join(), arg, connected, send, ctx);
    }
    if (!strcmp(cmd, "leave") || !strcmp(cmd, "chatleave")) {
        word(rest, arg, sizeof(arg));
        int k = resolve(c, arg);
        if (!k) { local_notice(c, note_err_not_member(), arg, NULL); return 1; }
        return send_op(c, op_leave(), c->channels[k], connected, send, ctx);
    }
    if (!strcmp(cmd, "chatwho") || !strcmp(cmd, "chatlist") || !strcmp(cmd, "who")) {
        word(rest, arg, sizeof(arg));
        int k = arg[0] ? resolve(c, arg) : (c->sticky_kind == chk_channel() ? c->sticky_number : 1);
        if (!k || !c->channels[k][0]) { local_notice(c, note_err_not_member(), arg, NULL); return 1; }
        return send_op(c, op_who(), c->channels[k], connected, send, ctx);
    }
    if (!strcmp(cmd, "name")) {
        char w[NET_CHAT_NAME];
        word(rest, arg, sizeof(arg));
        clean_name(w, sizeof(w), arg);
        if (!w[0]) { local_notice(c, note_err_name_bad(), NULL, arg); return 1; }
        if (!connected) { local_notice(c, note_err_offline(), NULL, NULL); return 1; }
        NetChat o;
        out_init(&o, op_name());
        SET(o.from, w);
        send(ctx, &o);
        return 1;
    }
    if (!strcmp(cmd, "chathelp") || !strcmp(cmd, "chat")) { help(c); return 1; }
    return 0;   /* not chat: the lobby runs it as a macro */
}

void chat_client_receive(ChatClient *c, const NetChat *pkt) {
    if (!pkt || pkt->hdr.type != PACKET_CHAT) return;
    char channel[NET_CHAT_NAME], from[NET_CHAT_NAME], to[NET_CHAT_NAME], text[NET_CHAT_TEXT];
    field(channel, sizeof(channel), pkt->channel, sizeof(pkt->channel));
    field(from, sizeof(from), pkt->from, sizeof(pkt->from));
    field(to, sizeof(to), pkt->to, sizeof(pkt->to));
    field(text, sizeof(text), pkt->text, sizeof(pkt->text));

    if (pkt->op == op_msg()) {
        int kind = pkt->kind;
        if (kind != chk_say() && kind != chk_yell() && kind != chk_channel() && kind != chk_whisper() &&
            kind != chk_whisper_to()) return;
        ChatEntry *e = store(c);
        e->kind = kind;
        SET(e->channel, channel); SET(e->from, from); SET(e->to, to); SET(e->text, text);
        e->number = kind == chk_channel() ? number_of(c, channel) : 0;
        if (kind == chk_whisper()) SET(c->last_whisper_from, from);
        reflux_host_dispatch(rx_chat_message(), kind, e->number, e->seq);
    } else if (pkt->op == op_who_list()) {
        ChatEntry *e = store(c);
        e->kind = chk_system();
        e->code = -1;   /* a who list, not a notice */
        SET(e->channel, channel); SET(e->text, text);
        e->number = number_of(c, channel);
        reflux_host_dispatch(rx_chat_message(), chk_system(), e->number, e->seq);
    } else if (pkt->op == op_notice()) {
        int code = pkt->code;
        if (code == note_joined()) {
            if (number_of(c, channel) || !name_ok(channel)) return;   /* already shown (a repeated hello) */
            int n = chan_first_free(used_mask(c));
            if (n < 0) return;
            SET(c->channels[n], channel);
            ChatEntry *e = store(c);
            e->kind = chk_system(); e->code = code; e->number = n;
            SET(e->channel, channel);
            reflux_host_dispatch(rx_chat_joined(), n, 0, e->seq);
        } else if (code == note_left()) {
            int n = number_of(c, channel);
            if (!n) return;
            c->channels[n][0] = '\0';
            if (c->sticky_kind == chk_channel() && c->sticky_number == n) c->sticky_kind = chk_say();
            ChatEntry *e = store(c);
            e->kind = chk_system(); e->code = code; e->number = n;
            SET(e->channel, channel);
            reflux_host_dispatch(rx_chat_left(), n, 0, e->seq);
        } else if (code == note_named()) {
            if (!strcmp(c->me, from)) { c->named = 1; return; }   /* a repeated hello's answer: nothing new */
            c->named = 1;
            SET(c->me, from);
            ChatEntry *e = store(c);
            e->kind = chk_system(); e->code = code;
            SET(e->text, from);
            reflux_host_dispatch(rx_chat_notice(), code, 0, e->seq);
        } else {
            local_notice(c, code, channel, text);
        }
    }
}

static void notice_text(const ChatEntry *e, char *out, size_t cap) {
    int code = e->code;
    if (code == note_named()) snprintf(out, cap, "You are known as %s.", e->text);
    else if (code == note_err_no_channel()) snprintf(out, cap, "No channel named '%s'.", e->channel);
    else if (code == note_err_not_member()) {
        if (e->channel[0]) snprintf(out, cap, "You are not in channel '%s'.", e->channel);
        else if (e->text[0]) snprintf(out, cap, "You are not in channel %s.", e->text);
        else snprintf(out, cap, "You are not in that channel.");
    }
    else if (code == note_err_too_many()) snprintf(out, cap, "You can be in at most %d channels.", chan_max_joined());
    else if (code == note_err_name_bad()) snprintf(out, cap, "'%s' can't be a name: 1-%d letters, digits, - or _ (not P and digits).", e->text, chan_name_max());
    else if (code == note_err_name_taken()) snprintf(out, cap, "The name '%s' is taken.", e->text);
    else if (code == note_err_no_player()) {
        if (e->text[0]) snprintf(out, cap, "No player named '%s' is here.", e->text);
        else snprintf(out, cap, "No one has whispered you yet.");
    }
    else if (code == note_err_rate()) snprintf(out, cap, "You are sending messages too fast.");
    else if (code == note_err_len()) snprintf(out, cap, "Messages are 1-%d characters.", chan_msg_max());
    else if (code == note_err_channel_bad()) snprintf(out, cap, "'%s' can't be a channel: 1-%d letters, digits, - or _.", e->channel, chan_name_max());
    else if (code == note_err_server_full()) snprintf(out, cap, "The server has no room for another channel.");
    else if (code == note_err_offline()) snprintf(out, cap, "Channels and whispers need a server: join a game first.");
    else if (code == note_joined()) snprintf(out, cap, "You are already in [%d. %s].", e->number, e->channel);
    else snprintf(out, cap, "%s", e->text);
}

int chat_client_poll(ChatClient *c, char out[][CHAT_CLIENT_LINE], int *colors, int max) {
    int total = reflux_host_total_dispatched();
    int oldest = reflux_host_oldest_abs();
    if (c->cursor_abs < oldest) c->cursor_abs = oldest;
    int n = 0;
    while (c->cursor_abs < total && n < max) {
        int i = reflux_host_index_for_abs(c->cursor_abs++);
        int t = reflux_host_action_type_at(i);
        if (t != rx_chat_message() && t != rx_chat_joined() && t != rx_chat_left() && t != rx_chat_notice()) continue;
        int seq = reflux_host_action_c_at(i);
        if (seq <= 0) continue;
        const ChatEntry *e = &c->ring[seq % CHAT_CLIENT_RING];
        if (e->seq != seq) continue;   /* another client's text, or one the ring already reused */
        char *o = out[n];
        int kind = e->kind;
        if (t == rx_chat_joined()) snprintf(o, CHAT_CLIENT_LINE, "Joined Channel: [%d. %s]", e->number, e->channel);
        else if (t == rx_chat_left()) snprintf(o, CHAT_CLIENT_LINE, "Left Channel: [%d. %s]", e->number, e->channel);
        else if (t == rx_chat_notice()) notice_text(e, o, CHAT_CLIENT_LINE);
        else if (kind == chk_say()) snprintf(o, CHAT_CLIENT_LINE, "[%s] says: %s", e->from, e->text);
        else if (kind == chk_yell()) snprintf(o, CHAT_CLIENT_LINE, "[%s] yells: %s", e->from, e->text);
        else if (kind == chk_whisper()) snprintf(o, CHAT_CLIENT_LINE, "[%s] whispers: %s", e->from, e->text);
        else if (kind == chk_whisper_to()) snprintf(o, CHAT_CLIENT_LINE, "To [%s]: %s", e->to, e->text);
        else if (kind == chk_channel()) {
            if (e->number) snprintf(o, CHAT_CLIENT_LINE, "[%d. %s] [%s]: %s", e->number, e->channel, e->from, e->text);
            else snprintf(o, CHAT_CLIENT_LINE, "[%s] [%s]: %s", e->channel, e->from, e->text);
        } else if (e->code == -1) {
            if (e->number) snprintf(o, CHAT_CLIENT_LINE, "[%d. %s] members: %s", e->number, e->channel, e->text);
            else snprintf(o, CHAT_CLIENT_LINE, "[%s] members: %s", e->channel, e->text);
        } else continue;
        if (colors) colors[n] = (t == rx_chat_message()) ? chan_color(kind) : chan_color(chk_system());
        n++;
    }
    return n;
}

void chat_client_prompt(const ChatClient *c, char *buf, int cap) {
    if (cap <= 0) return;
    if (c->sticky_kind == chk_channel() && c->channels[c->sticky_number][0])
        snprintf(buf, (size_t)cap, "[%d. %s]: ", c->sticky_number, c->channels[c->sticky_number]);
    else if (c->sticky_kind == chk_whisper_to() && c->sticky_to[0])
        snprintf(buf, (size_t)cap, "To [%s]: ", c->sticky_to);
    else
        snprintf(buf, (size_t)cap, "Say: ");
}
