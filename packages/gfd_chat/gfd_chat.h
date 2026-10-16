/*
 * gfd_chat.h -- SHANKPIT's GFD phone app on the cross-game chat bus (IDUNA POST/GET /api/v1/chat/messages).
 * Founder real-time, 2026-10-16: every SHANKPIT GFD phone talks to every other one (same game or not),
 * under the player's IDUNA display name, and to the GFD MUD's `global` chat.
 *
 *   - Posts channel "shankpit", sender_source "shankpit_client", authenticated with the player's own
 *     IDUNA login token (the same one the LOGIN IDUNA button / phone IDUNA app obtains).
 *   - Polls the bus (3s) and queues every channel=="shankpit" message for the main thread.
 *   - Blocking curl runs only on worker threads (same popen-curl convention as the rest of the lobby).
 *   - Own posts are echoed locally at send time; the matching bus copy is swallowed (gfd_chat_is_echo).
 *
 * Pure parsing (gfd_chat_next_object / gfd_chat_str_field / gfd_chat_json_escape) is covered by
 * gfd_chat_test.c with no network. Header-only, POSIX + pthreads (Windows: stubbed no-op).
 */
#ifndef GFD_CHAT_H
#define GFD_CHAT_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define GFD_CHAT_NAME_MAX 64
#define GFD_CHAT_BODY_MAX 128
#define GFD_CHAT_QUEUE 32
#define GFD_CHAT_ECHO_RING 8

typedef struct {
    char name[GFD_CHAT_NAME_MAX];
    char source[24];
    char body[GFD_CHAT_BODY_MAX];
} GfdChatMsg;

/* Walk a JSON array: find the next top-level {...}; strings are skipped so braces in chat text can't desync. */
static inline int gfd_chat_next_object(const char *j, size_t len, size_t *pos, size_t *s, size_t *e) {
    size_t i = *pos; int depth = 0, instr = 0; size_t start = 0;
    for (; i < len; i++) {
        char c = j[i];
        if (instr) { if (c == '\\') i++; else if (c == '"') instr = 0; continue; }
        if (c == '"') { instr = 1; continue; }
        if (c == '{') { if (depth++ == 0) start = i; }
        else if (c == '}' && depth > 0 && --depth == 0) { *s = start; *e = i + 1; *pos = i + 1; return 1; }
    }
    return 0;
}

/* Extract top-level string field "key" from object [o,o+n) into out (unescaping \" \\ \n \/ \uXXXX->'?'). */
static inline int gfd_chat_str_field(const char *o, size_t n, const char *key, char *out, size_t cap) {
    char pat[40]; snprintf(pat, sizeof pat, "\"%s\"", key);
    size_t pl = strlen(pat); int depth = 0, instr = 0;
    for (size_t i = 0; i < n; i++) {
        char c = o[i];
        if (instr) { if (c == '\\') i++; else if (c == '"') instr = 0; continue; }
        if (c == '{') depth++; else if (c == '}') depth--;
        if (c == '"') {
            if (depth == 1 && i + pl <= n && memcmp(o + i, pat, pl) == 0) {
                size_t k = i + pl;
                while (k < n && (o[k] == ' ' || o[k] == ':')) k++;
                if (k < n && o[k] == '"') {
                    k++; size_t w = 0;
                    while (k < n && o[k] != '"') {
                        char ch = o[k];
                        if (ch == '\\' && k + 1 < n) {
                            k++; ch = o[k];
                            if (ch == 'n' || ch == 't' || ch == 'r') ch = ' ';
                            else if (ch == 'u') { ch = '?'; k += 4; }
                        }
                        if (w + 1 < cap) out[w++] = ch;
                        k++;
                    }
                    out[w] = '\0'; return 1;
                }
                return 0;
            }
            instr = 1;
        }
    }
    return 0;
}

static inline long long gfd_chat_int_field(const char *o, size_t n, const char *key) {
    char pat[40]; snprintf(pat, sizeof pat, "\"%s\":", key);
    size_t pl = strlen(pat);
    for (size_t i = 0; i + pl < n; i++)
        if (memcmp(o + i, pat, pl) == 0) { char b[24]; size_t k = i + pl, w = 0; while (k < n && o[k] == ' ') k++;
            while (k < n && w < 20 && o[k] >= '0' && o[k] <= '9') b[w++] = o[k++];
            b[w] = 0; return w ? atoll(b) : -1; }
    return -1;
}

static inline size_t gfd_chat_json_escape(const char *in, char *out, size_t cap) {
    size_t w = 0;
    for (; *in && w + 7 < cap; in++) {
        unsigned char c = (unsigned char)*in;
        if (c == '"' || c == '\\') { out[w++] = '\\'; out[w++] = (char)c; }
        else if (c < 0x20) out[w++] = ' ';
        else out[w++] = (char)c;
    }
    out[w] = '\0'; return w;
}

#if !defined(_WIN32)
#include <pthread.h>
#include <unistd.h>
#include <sys/wait.h>

typedef struct {
    pthread_mutex_t mu;
    GfdChatMsg q[GFD_CHAT_QUEUE]; int head, count;
    char echo[GFD_CHAT_ECHO_RING][GFD_CHAT_BODY_MAX + GFD_CHAT_NAME_MAX]; int echo_n;
    char token[4096];
    char base[128];
    long long last_id; int primed; int started;
} GfdChat;
static GfdChat g_gfd_chat = { .mu = PTHREAD_MUTEX_INITIALIZER };

static inline void gfd_chat_set_base(const char *b) { snprintf(g_gfd_chat.base, sizeof g_gfd_chat.base, "%s", b); }
static inline const char *gfd_chat_base(void) { return g_gfd_chat.base[0] ? g_gfd_chat.base : "https://iam.okemily.com"; }

/* Run curl with its config on stdin (keeps the bearer token out of argv/ps). Returns malloc'd body or NULL. */
static inline char *gfd_chat_curl(const char *method, const char *url, const char *json_body, const char *token) {
    char esc[4400], cfg[9000];
    gfd_chat_json_escape(token, esc, sizeof esc); /* token is base64url+dots: escape is a no-op, kept for safety */
    int n = snprintf(cfg, sizeof cfg, "silent\nmax-time = 6\nrequest = \"%s\"\nurl = \"%s\"\nheader = \"Authorization: Bearer %s\"\n", method, url, esc);
    if (json_body) {
        char be[1024]; gfd_chat_json_escape(json_body, be, sizeof be); /* second escape layer: curl config quoting */
        n += snprintf(cfg + n, sizeof cfg - n, "header = \"Content-Type: application/json\"\ndata = \"%s\"\n", be);
    }
    int in[2], out[2]; if (pipe(in) || pipe(out)) return NULL;
    pid_t pid = fork();
    if (pid < 0) return NULL;
    if (pid == 0) { dup2(in[0], 0); dup2(out[1], 1); close(in[1]); close(out[0]); execlp("curl", "curl", "-K", "-", (char *)NULL); _exit(127); }
    close(in[0]); close(out[1]);
    (void)!write(in[1], cfg, (size_t)n); close(in[1]);
    size_t cap = 65536, len = 0; char *buf = (char *)malloc(cap);
    ssize_t r; while (buf && len + 1 < cap && (r = read(out[0], buf + len, cap - 1 - len)) > 0) len += (size_t)r;
    close(out[0]); int st; waitpid(pid, &st, 0);
    if (buf) buf[len] = '\0';
    return buf;
}
static inline void gfd_chat_push_in(const GfdChatMsg *m) {
    pthread_mutex_lock(&g_gfd_chat.mu);
    if (g_gfd_chat.count < GFD_CHAT_QUEUE) { g_gfd_chat.q[(g_gfd_chat.head + g_gfd_chat.count) % GFD_CHAT_QUEUE] = *m; g_gfd_chat.count++; }
    pthread_mutex_unlock(&g_gfd_chat.mu);
}

/* 1 if this bus message is our own just-sent line (consumes the echo-ring entry). Caller holds no lock. */
static inline int gfd_chat_is_echo(const char *name, const char *body) {
    char key[GFD_CHAT_BODY_MAX + GFD_CHAT_NAME_MAX]; snprintf(key, sizeof key, "%s\n%s", name, body);
    int hit = 0;
    pthread_mutex_lock(&g_gfd_chat.mu);
    for (int i = 0; i < g_gfd_chat.echo_n; i++)
        if (strcmp(g_gfd_chat.echo[i], key) == 0) {
            memmove(g_gfd_chat.echo[i], g_gfd_chat.echo[i + 1], (size_t)(g_gfd_chat.echo_n - i - 1) * sizeof g_gfd_chat.echo[0]);
            g_gfd_chat.echo_n--; hit = 1; break;
        }
    pthread_mutex_unlock(&g_gfd_chat.mu);
    return hit;
}

static inline void *gfd_chat_poll_thread(void *arg) {
    (void)arg;
    for (;;) {
        char tok[4096], url[256];
        pthread_mutex_lock(&g_gfd_chat.mu); snprintf(tok, sizeof tok, "%s", g_gfd_chat.token); long long since = g_gfd_chat.last_id; int primed = g_gfd_chat.primed; pthread_mutex_unlock(&g_gfd_chat.mu);
        if (tok[0]) {
            snprintf(url, sizeof url, "%s/api/v1/chat/messages?since_id=%lld&limit=%d", gfd_chat_base(), since, primed ? 50 : 200);
            char *r = gfd_chat_curl("GET", url, NULL, tok);
            if (r) {
                size_t len = strlen(r), pos = 0, s, e; int got = 0;
                while (gfd_chat_next_object(r, len, &pos, &s, &e)) {
                    long long id = gfd_chat_int_field(r + s, e - s, "id"); got++;
                    if (id > since) since = id;
                    GfdChatMsg m; char ch[24] = "";
                    if (!primed) continue; /* first pass(es) only advance the high-water mark: no history replay */
                    if (!gfd_chat_str_field(r + s, e - s, "channel", ch, sizeof ch) || strcmp(ch, "shankpit") != 0) continue;
                    if (!gfd_chat_str_field(r + s, e - s, "sender_name", m.name, sizeof m.name)) continue;
                    m.source[0] = 0; gfd_chat_str_field(r + s, e - s, "sender_source", m.source, sizeof m.source);
                    if (!gfd_chat_str_field(r + s, e - s, "body", m.body, sizeof m.body)) continue;
                    if (strcmp(m.source, "shankpit_client") == 0 && gfd_chat_is_echo(m.name, m.body)) continue;
                    gfd_chat_push_in(&m);
                }
                free(r);
                pthread_mutex_lock(&g_gfd_chat.mu);
                g_gfd_chat.last_id = since;
                if (!primed && got < 200) g_gfd_chat.primed = 1; /* caught up to the end of history */
                pthread_mutex_unlock(&g_gfd_chat.mu);
            }
        }
        sleep(3);
    }
    return NULL;
}

/* Call whenever the IDUNA token changes (empty = signed out). Starts the poller once. */
static inline void gfd_chat_set_token(const char *token) {
    pthread_mutex_lock(&g_gfd_chat.mu);
    snprintf(g_gfd_chat.token, sizeof g_gfd_chat.token, "%s", token ? token : "");
    int start = token && token[0] && !g_gfd_chat.started; if (start) g_gfd_chat.started = 1;
    pthread_mutex_unlock(&g_gfd_chat.mu);
    if (start) { pthread_t t; if (pthread_create(&t, NULL, gfd_chat_poll_thread, NULL) == 0) pthread_detach(t); }
}

typedef struct { char name[GFD_CHAT_NAME_MAX]; char body[GFD_CHAT_BODY_MAX]; char token[4096]; } GfdChatSend;
static inline void *gfd_chat_send_thread(void *arg) {
    GfdChatSend *s = (GfdChatSend *)arg;
    char en[160], eb[320], js[640], url[256];
    gfd_chat_json_escape(s->name, en, sizeof en); gfd_chat_json_escape(s->body, eb, sizeof eb);
    snprintf(js, sizeof js, "{\"channel\":\"shankpit\",\"sender_name\":\"%s\",\"sender_source\":\"shankpit_client\",\"body\":\"%s\"}", en, eb);
    snprintf(url, sizeof url, "%s/api/v1/chat/messages", gfd_chat_base());
    char *r = gfd_chat_curl("POST", url, js, s->token); free(r); free(s);
    return NULL;
}

/* Fire-and-forget post; also records the echo-suppression key. Returns 0 on dispatch, -1 if not signed in. */
static inline int gfd_chat_send(const char *name, const char *body) {
    GfdChatSend *s = (GfdChatSend *)calloc(1, sizeof *s); if (!s) return -1;
    pthread_mutex_lock(&g_gfd_chat.mu);
    snprintf(s->token, sizeof s->token, "%s", g_gfd_chat.token);
    if (!s->token[0]) { pthread_mutex_unlock(&g_gfd_chat.mu); free(s); return -1; }
    snprintf(s->name, sizeof s->name, "%s", name); snprintf(s->body, sizeof s->body, "%s", body);
    if (g_gfd_chat.echo_n == GFD_CHAT_ECHO_RING) { memmove(g_gfd_chat.echo[0], g_gfd_chat.echo[1], (GFD_CHAT_ECHO_RING - 1) * sizeof g_gfd_chat.echo[0]); g_gfd_chat.echo_n--; }
    snprintf(g_gfd_chat.echo[g_gfd_chat.echo_n++], sizeof g_gfd_chat.echo[0], "%s\n%s", s->name, s->body);
    pthread_mutex_unlock(&g_gfd_chat.mu);
    pthread_t t; if (pthread_create(&t, NULL, gfd_chat_send_thread, s) != 0) { free(s); return -1; }
    pthread_detach(t); return 0;
}

/* Main thread: pop one inbound message. 1 = filled *m. */
static inline int gfd_chat_take(GfdChatMsg *m) {
    int ok = 0;
    pthread_mutex_lock(&g_gfd_chat.mu);
    if (g_gfd_chat.count > 0) { *m = g_gfd_chat.q[g_gfd_chat.head]; g_gfd_chat.head = (g_gfd_chat.head + 1) % GFD_CHAT_QUEUE; g_gfd_chat.count--; ok = 1; }
    pthread_mutex_unlock(&g_gfd_chat.mu);
    return ok;
}
#else /* Windows: no chat bridge yet -- honest no-ops */
static inline void gfd_chat_set_base(const char *b) { (void)b; }
static inline void gfd_chat_set_token(const char *t) { (void)t; }
static inline int gfd_chat_send(const char *n, const char *b) { (void)n; (void)b; return -1; }
static inline int gfd_chat_take(GfdChatMsg *m) { (void)m; return 0; }
#endif

#endif
