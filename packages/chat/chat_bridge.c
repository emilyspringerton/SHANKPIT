/* packages/chat/chat_bridge.c -- plumbing for the global chat bridge; see chat_bridge.h. Decisions are
 * chat_bridge_rules.c (PARENA). */
#include "chat_bridge.h"
#include "chat_bridge_rules.h"
#include "../reflux/reflux_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char lower(char ch) { return (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch; }
static int ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) if (lower(*a) != lower(*b)) return 0;
    return *a == *b;
}

/* ---- sources and tags ---------------------------------------------------------------------------------------- */
static const char *SOURCES[] = { "", "mud", "battlegrounds", "gfd_server", "einhorn_survival", "bigo_server",
                                 "shankpit_client", "shankpit_server", "twitch", "discord", "slack" };
static const char *TAGS[] = { "?", "MUD", "BG", "GFD", "MC", "BIGO", "SP", "SHANKPIT", "TWITCH", "DISCORD", "SLACK" };

int chat_bridge_source_code(const char *s) {
    if (!s) return 0;
    for (int i = 1; i <= src_last() && i < (int)(sizeof(SOURCES) / sizeof(SOURCES[0])); i++) if (!strcmp(s, SOURCES[i])) return i;
    return 0;
}
const char *chat_bridge_tag(int src) {
    int t = br_tag(src);
    return (t > 0 && t < (int)(sizeof(TAGS) / sizeof(TAGS[0]))) ? TAGS[t] : TAGS[0];
}

/* ---- tiny JSON, enough for the bus's flat objects ------------------------------------------------------------ */
static int next_object(const char *j, size_t len, size_t *pos, size_t *s, size_t *e) {
    int depth = 0, instr = 0;
    size_t start = 0;
    for (size_t i = *pos; i < len; i++) {
        char c = j[i];
        if (instr) { if (c == '\\') i++; else if (c == '"') instr = 0; continue; }
        if (c == '"') { instr = 1; continue; }
        if (c == '{') { if (depth++ == 0) start = i; }
        else if (c == '}' && depth > 0 && --depth == 0) { *s = start; *e = i + 1; *pos = i + 1; return 1; }
    }
    return 0;
}

/* Find "key": at the object's top level; returns the index just after the colon (spaces skipped), or 0. */
static size_t find_key(const char *o, size_t n, const char *key) {
    size_t kl = strlen(key);
    int depth = 0, instr = 0;
    for (size_t i = 0; i < n; i++) {
        char c = o[i];
        if (instr) { if (c == '\\') i++; else if (c == '"') instr = 0; continue; }
        if (c == '{') { depth++; continue; }
        if (c == '}') { depth--; continue; }
        if (c != '"') continue;
        if (depth == 1 && i + kl + 2 <= n && o[i + kl + 1] == '"' && !memcmp(o + i + 1, key, kl)) {
            size_t k = i + kl + 2;
            while (k < n && (o[k] == ' ' || o[k] == ':' || o[k] == '\t')) k++;
            return k;
        }
        instr = 1;
    }
    return 0;
}

static long long int_field(const char *o, size_t n, const char *key) {
    size_t k = find_key(o, n, key);
    if (!k) return -1;
    long long v = 0;
    int any = 0;
    while (k < n && o[k] >= '0' && o[k] <= '9') { v = v * 10 + (o[k] - '0'); k++; any = 1; if (v > 4000000000000000000LL) break; }
    return any ? v : -1;
}

/* String field, unescaped; control characters become spaces and \uXXXX becomes '?' (names and lines are shown
   as plain text). Returns 1 when the key holds a string. */
static int str_field(const char *o, size_t n, const char *key, char *out, size_t cap) {
    size_t k = find_key(o, n, key), w = 0;
    if (!k || k >= n || o[k] != '"') { if (cap) out[0] = '\0'; return 0; }
    for (k++; k < n && o[k] != '"'; k++) {
        char ch = o[k];
        if (ch == '\\' && k + 1 < n) {
            k++;
            ch = o[k];
            if (ch == 'n' || ch == 't' || ch == 'r') ch = ' ';
            else if (ch == 'u') { ch = '?'; k += 4; }
        }
        if ((unsigned char)ch < 32) ch = ' ';
        if (w + 1 < cap) out[w++] = ch;
    }
    out[w] = '\0';
    return 1;
}

static size_t json_escape(const char *in, char *out, size_t cap) {
    size_t w = 0;
    for (; *in && w + 7 < cap; in++) {
        unsigned char c = (unsigned char)*in;
        if (c == '"' || c == '\\') { out[w++] = '\\'; out[w++] = (char)c; }
        else if (c < 32) w += (size_t)snprintf(out + w, cap - w, "\\u%04x", c);
        else out[w++] = (char)c;
    }
    out[w] = '\0';
    return w;
}

/* ---- the core ------------------------------------------------------------------------------------------------ */
void chat_bridge_init(ChatBridge *b) { memset(b, 0, sizeof(*b)); }
long long chat_bridge_since(const ChatBridge *b) { return b->last_id; }

int chat_bridge_offer(ChatBridge *b, int slot, const char *channel, const char *name, const char *text) {
    if (!b || !channel || !name || !text) return 0;
    if (!br_should_post(3, ieq(channel, "General") ? 1 : 0, 0)) return 0;   /* 3 = chk-channel */
    int cap = br_post_queue();
    if (cap > CHAT_BRIDGE_QUEUE) cap = CHAT_BRIDGE_QUEUE;
    int queued = 1;
    if (b->q_count >= cap) {   /* the bus is away: the oldest line goes first */
        b->q_head = (b->q_head + 1) % CHAT_BRIDGE_QUEUE;
        b->q_count--;
        b->dropped++;
        queued = 0;
    }
    ChatBridgeLine *l = &b->q[(b->q_head + b->q_count) % CHAT_BRIDGE_QUEUE];
    snprintf(l->name, sizeof(l->name), "%s", name);
    snprintf(l->text, sizeof(l->text), "%.*s", br_body_max(), text);
    b->q_count++;
    b->queued++;
    reflux_host_dispatch(rx_bridge_out(), 3, slot, queued);
    return 1;
}

int chat_bridge_next_post(ChatBridge *b, char *json, size_t cap) {
    if (!b || b->q_count == 0 || cap < 64) return 0;
    ChatBridgeLine *l = &b->q[b->q_head];
    char en[NET_CHAT_NAME * 6], et[NET_CHAT_TEXT * 6];
    json_escape(l->name, en, sizeof(en));
    json_escape(l->text, et, sizeof(et));
    int w = snprintf(json, cap, "{\"channel\":\"shankpit\",\"sender_name\":\"%s\",\"sender_source\":\"shankpit_server\",\"body\":\"%s\"}", en, et);
    if (w < 0 || (size_t)w >= cap) return 0;
    b->q_head = (b->q_head + 1) % CHAT_BRIDGE_QUEUE;
    b->q_count--;
    return 1;
}

void chat_bridge_note_own(ChatBridge *b, long long id) {
    b->own[b->own_next] = id;
    b->own_next = (b->own_next + 1) % CHAT_BRIDGE_OWN;
}

static int is_own(const ChatBridge *b, long long id) {
    for (int i = 0; i < CHAT_BRIDGE_OWN; i++) if (b->own[i] == id && id > 0) return 1;
    return 0;
}

int chat_bridge_ingest(ChatBridge *b, const char *json, size_t len, ChatBridgeShown *out, int max) {
    if (!b || !json) return 0;
    size_t pos = 0, s, e;
    int n = 0;
    int lim = br_max_per_poll();
    if (max > lim) max = lim;
    while (next_object(json, len, &pos, &s, &e)) {
        const char *o = json + s;
        size_t on = e - s;
        long long id = int_field(o, on, "id");
        if (id <= 0) continue;
        if (!b->primed) { if (id > b->last_id) b->last_id = id; continue; }
        if (id <= b->last_id) continue;
        char source[32], name[64], body[NET_CHAT_TEXT];
        str_field(o, on, "sender_source", source, sizeof(source));
        int src = chat_bridge_source_code(source);
        if (!br_shows(src_shankpit(), src, is_own(b, id) ? 1 : 0)) { b->last_id = id; continue; }
        if (n >= max) break;                       /* the rest waits for the next poll */
        str_field(o, on, "sender_name", name, sizeof(name));
        str_field(o, on, "body", body, sizeof(body));
        if (!name[0] || !body[0]) { b->last_id = id; continue; }
        ChatBridgeShown *sh = &out[n++];
        snprintf(sh->from, sizeof(sh->from), "%s:%.*s", chat_bridge_tag(src), br_name_max(), name);
        snprintf(sh->text, sizeof(sh->text), "%.*s", br_body_max(), body);
        sh->src = src;
        b->last_id = id;
        b->shown++;
    }
    b->primed = 1;
    return n;
}

/* ---- the network worker (POSIX) ------------------------------------------------------------------------------ */
#if !defined(_WIN32)
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define NET_OUT_MAX 64
#define NET_BODY_MAX 1800

struct ChatBridgeNet {
    pthread_mutex_t mu;
    char url[256], agent[64], secret[256], dir[300];
    char out[NET_OUT_MAX][NET_BODY_MAX];
    int out_n;
    long long since;
    char *inbox;
    size_t inbox_len;
    long long own[CHAT_BRIDGE_OWN];
    int own_n;
};

static int safe_chars(const char *s, const char *allowed_extra) {
    for (; *s; s++) {
        char c = *s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) continue;
        if (strchr(allowed_extra, c)) continue;
        return 0;
    }
    return 1;
}

static int write_private(const char *path, const char *data) {
    mode_t old = umask(077);
    FILE *f = fopen(path, "wb");
    umask(old);
    if (!f) return 0;
    fputs(data, f);
    fclose(f);
    return 1;
}

static char *read_all(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 4096, n = 0;
    char *buf = malloc(cap);
    while (buf) {
        size_t got = fread(buf + n, 1, cap - n - 1, f);
        n += got;
        if (got == 0) break;
        if (n + 1 >= cap) { char *nb = realloc(buf, cap * 2); if (!nb) { free(buf); buf = NULL; break; } buf = nb; cap *= 2; }
    }
    fclose(f);
    if (buf) { buf[n] = '\0'; if (len) *len = n; }
    return buf;
}

/* curl with everything sensitive in files: returns the HTTP status, 0 if curl could not run. */
static int curl_call(ChatBridgeNet *n, const char *args, const char *out) {
    char cmd[1400];
    snprintf(cmd, sizeof(cmd), "curl -sS -m 10 -o '%s' -w '%%{http_code}' %s 2>/dev/null", out, args);
    FILE *p = popen(cmd, "r");
    if (!p) return 0;
    char code[16] = "";
    if (!fgets(code, sizeof(code), p)) code[0] = '\0';
    pclose(p);
    (void)n;
    return atoi(code);
}

static int bridge_auth(ChatBridgeNet *n) {
    char sec[600], body[800], path[400], hdr[400], resp[400], args[1100];
    json_escape(n->secret, sec, sizeof(sec));
    snprintf(body, sizeof(body), "{\"agent_name\":\"%s\",\"agent_secret\":\"%s\"}", n->agent, sec);
    snprintf(path, sizeof(path), "%s/auth.json", n->dir);
    snprintf(resp, sizeof(resp), "%s/auth.out", n->dir);
    if (!write_private(path, body)) return 0;
    snprintf(args, sizeof(args), "-H 'Content-Type: application/json' --data-binary @'%s' '%s/api/v1/auth/agent'", path, n->url);
    int code = curl_call(n, args, resp);
    unlink(path);
    size_t rl = 0;
    char *r = read_all(resp, &rl);
    unlink(resp);
    if (code != 200 || !r) { free(r); fprintf(stderr, "[CHAT_BRIDGE] sign-in as %s failed (HTTP %d)\n", n->agent, code); return 0; }
    char token[2048];
    int ok = str_field(r, rl, "access_token", token, sizeof(token)) && token[0] && safe_chars(token, "._-");
    free(r);
    if (!ok) return 0;
    char line[2200];
    snprintf(line, sizeof(line), "Authorization: Bearer %s\nContent-Type: application/json\n", token);
    snprintf(hdr, sizeof(hdr), "%s/hdr", n->dir);
    return write_private(hdr, line);
}

static void *bridge_worker(void *arg) {
    ChatBridgeNet *n = (ChatBridgeNet *)arg;
    int signed_in = 0;
    char hdr[400], bodyf[400], outf[400];
    snprintf(hdr, sizeof(hdr), "%s/hdr", n->dir);
    snprintf(bodyf, sizeof(bodyf), "%s/post.json", n->dir);
    snprintf(outf, sizeof(outf), "%s/resp.json", n->dir);
    static char batch[8][NET_BODY_MAX];
    for (;;) {
        struct timespec ts = { br_poll_ms() / 1000, (long)(br_poll_ms() % 1000) * 1000000L };
        nanosleep(&ts, NULL);
        if (!signed_in) { signed_in = bridge_auth(n); if (!signed_in) continue; }
        int nb = 0;
        long long since;
        pthread_mutex_lock(&n->mu);
        while (nb < 8 && nb < n->out_n) { memcpy(batch[nb], n->out[nb], NET_BODY_MAX); nb++; }
        memmove(n->out, n->out + nb, (size_t)(n->out_n - nb) * NET_BODY_MAX);
        n->out_n -= nb;
        since = n->since;
        pthread_mutex_unlock(&n->mu);

        char args[1200];
        for (int i = 0; i < nb; i++) {
            if (!write_private(bodyf, batch[i])) break;
            snprintf(args, sizeof(args), "-X POST -H @'%s' --data-binary @'%s' '%s/api/v1/chat/messages'", hdr, bodyf, n->url);
            int code = curl_call(n, args, outf);
            if (code == 401) { signed_in = 0; break; }      /* token expired: sign in again next round */
            if (code == 201) {
                size_t rl = 0;
                char *r = read_all(outf, &rl);
                long long id = r ? int_field(r, rl, "id") : -1;
                free(r);
                if (id > 0) { pthread_mutex_lock(&n->mu); if (n->own_n < CHAT_BRIDGE_OWN) n->own[n->own_n++] = id; pthread_mutex_unlock(&n->mu); }
            } else {
                fprintf(stderr, "[CHAT_BRIDGE] post refused (HTTP %d)\n", code);
            }
        }
        unlink(bodyf);
        if (!signed_in) continue;
        snprintf(args, sizeof(args), "-H @'%s' '%s/api/v1/chat/messages?since_id=%lld&limit=%d'", hdr, n->url, since, br_max_per_poll());
        int code = curl_call(n, args, outf);
        if (code == 401) { signed_in = 0; continue; }
        if (code != 200) continue;
        size_t rl = 0;
        char *r = read_all(outf, &rl);
        if (!r) continue;
        pthread_mutex_lock(&n->mu);
        free(n->inbox);
        n->inbox = r;
        n->inbox_len = rl;
        pthread_mutex_unlock(&n->mu);
    }
    return NULL;
}

ChatBridgeNet *chat_bridge_net_start(void) {
    const char *url = getenv("SHANKPIT_IDUNA_URL"), *agent = getenv("SHANKPIT_IDUNA_AGENT"),
               *secret_file = getenv("SHANKPIT_IDUNA_SECRET_FILE");
    if (!url || !*url || !agent || !*agent || !secret_file || !*secret_file) return NULL;
    if ((strncmp(url, "https://", 8) && strncmp(url, "http://", 7)) || !safe_chars(url, ":/._-") || strlen(url) > 200) {
        fprintf(stderr, "[CHAT_BRIDGE] SHANKPIT_IDUNA_URL has characters it may not: bridge off\n");
        return NULL;
    }
    if (!safe_chars(agent, "_-") || strlen(agent) > 60) { fprintf(stderr, "[CHAT_BRIDGE] bad SHANKPIT_IDUNA_AGENT: bridge off\n"); return NULL; }
    ChatBridgeNet *n = calloc(1, sizeof(*n));
    if (!n) return NULL;
    snprintf(n->url, sizeof(n->url), "%s", url);
    size_t ul = strlen(n->url);
    while (ul && n->url[ul - 1] == '/') n->url[--ul] = '\0';
    snprintf(n->agent, sizeof(n->agent), "%s", agent);
    FILE *f = fopen(secret_file, "rb");
    if (!f || !fgets(n->secret, sizeof(n->secret), f)) { if (f) fclose(f); free(n); fprintf(stderr, "[CHAT_BRIDGE] can't read SHANKPIT_IDUNA_SECRET_FILE: bridge off\n"); return NULL; }
    fclose(f);
    n->secret[strcspn(n->secret, "\r\n")] = '\0';
    const char *tmp = getenv("TMPDIR");
    snprintf(n->dir, sizeof(n->dir), "%s/shankpit-bridge-%d", (tmp && *tmp && safe_chars(tmp, "/._-")) ? tmp : "/tmp", (int)getpid());
    if (mkdir(n->dir, 0700) != 0 && access(n->dir, W_OK) != 0) { free(n); fprintf(stderr, "[CHAT_BRIDGE] can't make %s: bridge off\n", n->dir); return NULL; }
    pthread_mutex_init(&n->mu, NULL);
    pthread_t th;
    if (pthread_create(&th, NULL, bridge_worker, n) != 0) { free(n); return NULL; }
    pthread_detach(th);
    fprintf(stderr, "[CHAT_BRIDGE] on: General <-> %s as %s\n", n->url, n->agent);
    return n;
}

void chat_bridge_net_tick(ChatBridgeNet *n, ChatBridge *b, ChatServer *s, const ChatPeer *peers, unsigned int now_ms,
                          ChatServerSend send, void *ctx) {
    if (!n || !b || !s) return;
    char *resp = NULL;
    size_t rl = 0;
    pthread_mutex_lock(&n->mu);
    while (n->out_n < NET_OUT_MAX && chat_bridge_next_post(b, n->out[n->out_n], NET_BODY_MAX)) n->out_n++;
    for (int i = 0; i < n->own_n; i++) chat_bridge_note_own(b, n->own[i]);
    n->own_n = 0;
    resp = n->inbox;
    rl = n->inbox_len;
    n->inbox = NULL;
    pthread_mutex_unlock(&n->mu);
    if (resp) {
        ChatBridgeShown shown[32];
        int k = chat_bridge_ingest(b, resp, rl, shown, 32);
        free(resp);
        int counts[16] = { 0 };
        for (int i = 0; i < k; i++) {
            chat_server_inject(s, "General", shown[i].from, shown[i].text, peers, now_ms, send, ctx);
            if (shown[i].src > 0 && shown[i].src < 16) counts[shown[i].src]++;
        }
        for (int src = 1; src < 16; src++) if (counts[src]) reflux_host_dispatch(rx_bridge_in(), src, 0, counts[src]);
    }
    pthread_mutex_lock(&n->mu);
    n->since = chat_bridge_since(b);
    pthread_mutex_unlock(&n->mu);
}

#else   /* Windows: the server is Linux-only; the bridge's worker is not built here */
ChatBridgeNet *chat_bridge_net_start(void) { return NULL; }
void chat_bridge_net_tick(ChatBridgeNet *n, ChatBridge *b, ChatServer *s, const ChatPeer *peers, unsigned int now_ms,
                          ChatServerSend send, void *ctx) {
    (void)n; (void)b; (void)s; (void)peers; (void)now_ms; (void)send; (void)ctx;
}
#endif
