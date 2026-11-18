/* packages/chat/chat_server.c -- plumbing for the server half of the chat channels; see chat_server.h.
 * Decisions are chat_channel_rules.c (PARENA). */
#include "chat_server.h"
#include "chat_channel_rules.h"
#include "../reflux/reflux_runtime.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Copy a fixed-size wire field (maybe not NUL-terminated) into a C string. */
static void field(char *dst, size_t dcap, const char *src, size_t scap) {
    size_t n = 0;
    while (n < scap && src[n]) n++;
    if (n >= dcap) n = dcap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static char lower(char ch) { return (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch; }

static int ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) if (lower(*a) != lower(*b)) return 0;
    return *a == *b;
}

static int name_ok(const char *s) {
    int n = (int)strlen(s);
    if (!chan_name_len_ok(n)) return 0;
    for (int i = 0; i < n; i++) if (!chan_name_char_ok((unsigned char)s[i])) return 0;
    return 1;
}

/* "P" followed only by digits: the server's own default names, never claimable. */
static int reserved_default(const char *s) {
    if ((s[0] != 'P' && s[0] != 'p') || !s[1]) return 0;
    for (int i = 1; s[i]; i++) if (s[i] < '0' || s[i] > '9') return 0;
    return 1;
}

void chat_server_init(ChatServer *s) { memset(s, 0, sizeof(*s)); }

static ChatServerChannel *find_channel(ChatServer *s, const char *name);

int chat_server_inject(ChatServer *s, const char *channel, const char *from, const char *text, const ChatPeer *peers,
                       unsigned int now_ms, ChatServerSend send, void *ctx) {
    if (!s || !channel || !from || !text || !peers || !send) return 0;
    if (!chan_msg_len_ok((int)strlen(text))) return 0;
    ChatServerChannel *ch = find_channel(s, channel);
    if (!ch) return 0;
    NetChat o;
    memset(&o, 0, sizeof(o));
    o.hdr.type = PACKET_CHAT;
    o.hdr.timestamp = now_ms;
    o.op = (unsigned char)op_msg();
    o.kind = (unsigned char)chk_channel();
    field(o.channel, sizeof(o.channel), ch->name, sizeof(ch->name));
    field(o.from, sizeof(o.from), from, strlen(from) + 1);
    field(o.text, sizeof(o.text), text, strlen(text) + 1);
    int got = 0;
    for (int i = 1; i < MAX_CLIENTS; i++)
        if (peers[i].active && ch->member[i]) { send(ctx, i, &o); got++; }
    return got;
}

const char *chat_server_name(const ChatServer *s, int slot) {
    return (slot >= 0 && slot < MAX_CLIENTS) ? s->names[slot] : "";
}

static ChatServerChannel *find_channel(ChatServer *s, const char *name) {
    for (int k = 0; k < CHAT_SERVER_CHANNELS; k++) if (s->ch[k].used && ieq(s->ch[k].name, name)) return &s->ch[k];
    return NULL;
}

static ChatServerChannel *make_channel(ChatServer *s, const char *name) {
    int cap = chan_server_max();
    if (cap > CHAT_SERVER_CHANNELS) cap = CHAT_SERVER_CHANNELS;
    for (int k = 0; k < cap; k++) {
        if (!s->ch[k].used) {
            memset(&s->ch[k], 0, sizeof(s->ch[k]));
            field(s->ch[k].name, sizeof(s->ch[k].name), name, strlen(name) + 1);
            s->ch[k].used = 1;
            return &s->ch[k];
        }
    }
    return NULL;
}

static int channel_empty(const ChatServerChannel *c) {
    for (int i = 0; i < MAX_CLIENTS; i++) if (c->member[i]) return 0;
    return 1;
}

static int memberships(const ChatServer *s, int slot) {
    int n = 0;
    for (int k = 0; k < CHAT_SERVER_CHANNELS; k++) if (s->ch[k].used && s->ch[k].member[slot]) n++;
    return n;
}

static int slot_by_name(const ChatServer *s, const ChatPeer *peers, const char *name) {
    for (int i = 1; i < MAX_CLIENTS; i++)
        if (peers[i].active && s->names[i][0] && ieq(s->names[i], name)) return i;
    return -1;
}

void chat_server_drop(ChatServer *s, int slot) {
    if (slot < 0 || slot >= MAX_CLIENTS) return;
    s->names[slot][0] = '\0';
    s->greeted[slot] = 0;
    s->rate_start[slot] = 0;
    s->rate_count[slot] = 0;
    s->rate_told[slot] = 0;
    for (int k = 0; k < CHAT_SERVER_CHANNELS; k++) {
        if (!s->ch[k].used) continue;
        s->ch[k].member[slot] = 0;
        if (channel_empty(&s->ch[k])) s->ch[k].used = 0;
    }
}

static void out_init(NetChat *o, int op, unsigned int now_ms) {
    memset(o, 0, sizeof(*o));
    o->hdr.type = PACKET_CHAT;
    o->hdr.timestamp = now_ms;
    o->op = (unsigned char)op;
}

static int notice(int to, int code, const char *channel, const char *text, unsigned int now_ms,
                  ChatServerSend send, void *ctx) {
    NetChat o;
    out_init(&o, op_notice(), now_ms);
    o.code = (unsigned char)code;
    if (channel) field(o.channel, sizeof(o.channel), channel, strlen(channel) + 1);
    if (text) field(o.text, sizeof(o.text), text, strlen(text) + 1);
    send(ctx, to, &o);
    return 1;
}

static int dist_cm(const ChatPeer *a, const ChatPeer *b) {
    double dx = (double)a->x - b->x, dy = (double)a->y - b->y, dz = (double)a->z - b->z;
    double cm = sqrt(dx * dx + dy * dy + dz * dz) * 100.0;   /* one world unit = 100 cm (chat_rules.prn) */
    if (!(cm == cm) || cm > 2.0e9) return 2000000000;
    return (int)cm;
}

int chat_server_handle(ChatServer *s, int from, const NetChat *in, const ChatPeer *peers, unsigned int now_ms,
                       ChatServerSend send, void *ctx) {
    if (!s || !in || !peers || !send || from <= 0 || from >= MAX_CLIENTS || !peers[from].active) return 0;
    char channel[NET_CHAT_NAME], want[NET_CHAT_NAME], to[NET_CHAT_NAME], text[NET_CHAT_TEXT];
    field(channel, sizeof(channel), in->channel, sizeof(in->channel));
    field(want, sizeof(want), in->from, sizeof(in->from));
    field(to, sizeof(to), in->to, sizeof(in->to));
    field(text, sizeof(text), in->text, sizeof(in->text));
    int op = in->op;

    if (!s->greeted[from]) {
        s->greeted[from] = 1;
        ChatServerChannel *g = find_channel(s, "General");
        if (!g) g = make_channel(s, "General");
        if (g) g->member[from] = 1;
    }
    if (!s->names[from][0]) snprintf(s->names[from], sizeof(s->names[from]), "P%d", from);

    if (op == op_name()) {
        int sent = 0;
        if (want[0] && !ieq(want, s->names[from])) {
            int other = slot_by_name(s, peers, want);
            if (!name_ok(want) || reserved_default(want)) sent += notice(from, note_err_name_bad(), NULL, want, now_ms, send, ctx);
            else if (other >= 0 && other != from) sent += notice(from, note_err_name_taken(), NULL, want, now_ms, send, ctx);
            else field(s->names[from], sizeof(s->names[from]), want, sizeof(want));
        }
        NetChat o;
        out_init(&o, op_notice(), now_ms);
        o.code = (unsigned char)note_named();
        field(o.from, sizeof(o.from), s->names[from], sizeof(s->names[from]));
        send(ctx, from, &o);
        sent++;
        /* every channel it is in, so a client that lost its state (or a dropped notice) catches up */
        for (int k = 0; k < CHAT_SERVER_CHANNELS; k++)
            if (s->ch[k].used && s->ch[k].member[from])
                sent += notice(from, note_joined(), s->ch[k].name, NULL, now_ms, send, ctx);
        return sent;
    }

    if (op == op_say() || op == op_yell() || op == op_channel() || op == op_whisper()) {
        /* flood control on messages (joins are capped at chan-max-joined per slot and chan-server-max per
           server, and a who answer is no bigger than its request); "too fast" goes out once per window */
        unsigned int elapsed = now_ms - s->rate_start[from];
        if (elapsed >= (unsigned int)chan_rate_window_ms()) {
            s->rate_start[from] = now_ms;
            s->rate_count[from] = 0;
            s->rate_told[from] = 0;
            elapsed = 0;
        }
        if (!chan_rate_ok((int)elapsed, s->rate_count[from])) {
            s->refused++;
            if (s->rate_told[from]) return 0;
            s->rate_told[from] = 1;
            return notice(from, note_err_rate(), NULL, NULL, now_ms, send, ctx);
        }
        s->rate_count[from]++;
        if (!chan_msg_len_ok((int)strlen(text))) return notice(from, note_err_len(), NULL, NULL, now_ms, send, ctx);
        NetChat o;
        out_init(&o, op_msg(), now_ms);
        field(o.from, sizeof(o.from), s->names[from], sizeof(s->names[from]));
        field(o.text, sizeof(o.text), text, sizeof(text));
        int kind, got = 0;
        if (op == op_whisper()) {
            int t = slot_by_name(s, peers, to);
            if (t < 0) return notice(from, note_err_no_player(), NULL, to, now_ms, send, ctx);
            kind = chk_whisper();
            field(o.to, sizeof(o.to), s->names[t], sizeof(s->names[t]));
            o.kind = (unsigned char)kind;
            if (chan_delivers(kind, 0, 0, 0, 1)) { send(ctx, t, &o); got++; }
            NetChat echo = o;
            echo.kind = (unsigned char)chk_whisper_to();
            send(ctx, from, &echo);
        } else if (op == op_channel()) {
            ChatServerChannel *ch = find_channel(s, channel);
            if (!ch) return notice(from, note_err_no_channel(), channel, NULL, now_ms, send, ctx);
            if (!ch->member[from]) return notice(from, note_err_not_member(), channel, NULL, now_ms, send, ctx);
            kind = chk_channel();
            o.kind = (unsigned char)kind;
            field(o.channel, sizeof(o.channel), ch->name, sizeof(ch->name));
            for (int i = 1; i < MAX_CLIENTS; i++)
                if (peers[i].active && chan_delivers(kind, 0, 0, ch->member[i] ? 1 : 0, 0)) { send(ctx, i, &o); got++; }
            if (s->on_channel) s->on_channel(s->on_channel_ctx, from, ch->name, s->names[from], text);
        } else {
            kind = (op == op_say()) ? chk_say() : chk_yell();
            o.kind = (unsigned char)kind;
            for (int i = 1; i < MAX_CLIENTS; i++) {
                if (!peers[i].active) continue;
                int same = peers[i].scene == peers[from].scene;
                if (chan_delivers(kind, same ? 1 : 0, dist_cm(&peers[from], &peers[i]), 0, 0)) { send(ctx, i, &o); got++; }
            }
        }
        reflux_host_dispatch(rx_chat_routed(), kind, from, got);
        s->routed++;
        return got;
    }

    if (op == op_join()) {
        if (!name_ok(channel)) return notice(from, note_err_channel_bad(), channel, NULL, now_ms, send, ctx);
        ChatServerChannel *ch = find_channel(s, channel);
        if (ch && ch->member[from]) return notice(from, note_joined(), ch->name, NULL, now_ms, send, ctx);
        if (memberships(s, from) >= chan_max_joined()) return notice(from, note_err_too_many(), channel, NULL, now_ms, send, ctx);
        if (!ch) ch = make_channel(s, channel);
        if (!ch) return notice(from, note_err_server_full(), channel, NULL, now_ms, send, ctx);
        ch->member[from] = 1;
        return notice(from, note_joined(), ch->name, NULL, now_ms, send, ctx);
    }

    if (op == op_leave()) {
        ChatServerChannel *ch = find_channel(s, channel);
        if (!ch || !ch->member[from]) return notice(from, note_err_not_member(), channel, NULL, now_ms, send, ctx);
        char name[NET_CHAT_NAME];
        field(name, sizeof(name), ch->name, sizeof(ch->name));
        ch->member[from] = 0;
        if (channel_empty(ch)) ch->used = 0;
        return notice(from, note_left(), name, NULL, now_ms, send, ctx);
    }

    if (op == op_who()) {
        ChatServerChannel *ch = find_channel(s, channel);
        if (!ch || !ch->member[from]) return notice(from, note_err_not_member(), channel, NULL, now_ms, send, ctx);
        NetChat o;
        out_init(&o, op_who_list(), now_ms);
        field(o.channel, sizeof(o.channel), ch->name, sizeof(ch->name));
        size_t len = 0;
        for (int i = 1; i < MAX_CLIENTS; i++) {
            if (!ch->member[i] || !peers[i].active) continue;
            const char *nm = s->names[i][0] ? s->names[i] : "?";
            size_t need = strlen(nm) + (len ? 2 : 0);
            /* keep 6 bytes free for ", ..." + NUL, so the marker always fits */
            if (len + need + 6 >= sizeof(o.text)) { memcpy(o.text + len, ", ...", 6); len += 5; break; }
            if (len) { memcpy(o.text + len, ", ", 2); len += 2; }
            memcpy(o.text + len, nm, strlen(nm));
            len += strlen(nm);
            o.text[len] = '\0';
        }
        send(ctx, from, &o);
        return 1;
    }
    return 0;   /* an op this server does not know: ignored */
}
