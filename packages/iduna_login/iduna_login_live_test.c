/* Live round-trip test: the vendored IDUNA SSO header against a REAL IDUNA instance.
 *
 *   IDUNA_LIVE_BASE=http://127.0.0.1:18080 make test-iduna-live
 *
 * iduna_login_test.c drives the loopback listener with a scripted browser and never contacts IDUNA.
 * This one does the browser's half for real, in the order the profile specifies:
 *   1. register a throwaway account and sign in through /api/v1/auth/email/login -- the same endpoint
 *      the SSO page's own JS calls, and the only place a real JWT comes from;
 *   2. GET the SSO page at the exact URL iduna_login_begin_ex() built, requiring it to be the page
 *      that hands the token back in a fragment;
 *   3. POST state + that real token to our own loopback /token and require poll() to return it.
 *
 * It creates one account per run in the target instance, so it is opt-in: with IDUNA_LIVE_BASE unset
 * it prints SKIP and exits 0, like the other tests here that need a live service.
 * Build: cc -std=gnu99 -Wall -Wextra -pedantic -Werror -o /tmp/iltl iduna_login_live_test.c
 */
#include "iduna_login.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <unistd.h>

static void die(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fputs("iduna_login_live_test: FAIL: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}

static const char *body_of(const char *resp) {
    const char *p = strstr(resp, "\r\n\r\n");
    return p ? p + 4 : resp;
}

static void parse_base(const char *base, char *host, size_t hcap, int *port) {
    const char *p = base;
    if (strncmp(p, "http://", 7) != 0)
        die("IDUNA_LIVE_BASE must be http:// (a loopback dev instance), got %s", base);
    p += 7;
    const char *slash = strchr(p, '/');
    size_t n = slash ? (size_t)(slash - p) : strlen(p);
    const char *colon = memchr(p, ':', n);
    size_t hn = colon ? (size_t)(colon - p) : n;
    if (hn == 0 || hn + 1 > hcap) die("bad host in %s", base);
    memcpy(host, p, hn); host[hn] = 0;
    *port = colon ? atoi(colon + 1) : 80;
}

static int dial(const char *host, int port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &a.sin_addr) != 1) { close(s); return -1; }
    if (connect(s, (struct sockaddr *)&a, sizeof a) != 0) { close(s); return -1; }
    return s;
}

static void send_all(int s, const char *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = send(s, buf + off, n - off, 0);
        if (w <= 0) die("send failed: %s", strerror(errno));
        off += (size_t)w;
    }
}

/* One request/response. Returns the HTTP status (or -1 if it was not HTTP at all). */
static int http_call(const char *host, int port, const char *req, char *out, size_t cap) {
    int s = dial(host, port);
    if (s < 0) die("connect %s:%d failed: %s", host, port, strerror(errno));
    send_all(s, req, strlen(req));
    size_t have = 0;
    for (;;) {
        if (have + 1 >= cap) break;
        ssize_t r = recv(s, out + have, cap - 1 - have, 0);
        if (r <= 0) break;
        have += (size_t)r;
    }
    out[have] = 0;
    close(s);
    if (have < 12 || strncmp(out, "HTTP/1.", 7) != 0) return -1;
    return atoi(out + 9);
}

/* Pull a flat "key":"value" string out of a JSON body. The fields read here (token, player_id) are
 * base64url/uuid, never containing escapes. */
static int json_str(const char *body, const char *key, char *dst, size_t cap) {
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":\"", key);
    const char *p = strstr(body, pat);
    if (!p) return 0;
    p += strlen(pat);
    size_t o = 0;
    while (*p && *p != '"' && o + 1 < cap) dst[o++] = *p++;
    dst[o] = 0;
    return o > 0;
}

int main(void) {
    const char *base = getenv("IDUNA_LIVE_BASE");
    if (!base || !*base) {
        puts("SKIP  IDUNA_LIVE_BASE not set -- start IDUNA (IDUNA_ADDR=127.0.0.1:18080) then run");
        puts("      IDUNA_LIVE_BASE=http://127.0.0.1:18080 make test-iduna-live");
        return 0;
    }
    char host[64]; int port = 80;
    parse_base(base, host, sizeof host, &port);

    unsigned nonce = (unsigned)time(NULL) ^ ((unsigned)getpid() << 16);
    char email[128], pass[64], who[64], json[768], out[32768], req[4096];
    snprintf(email, sizeof email, "live%u@example.invalid", nonce);
    snprintf(pass, sizeof pass, "live-test-pw-%u", nonce);
    snprintf(who, sizeof who, "LIVEPROBE%u", nonce % 100000u);

    snprintf(json, sizeof json, "{\"email\":\"%s\",\"password\":\"%s\",\"character_name\":\"%s\"}",
             email, pass, who);
    snprintf(req, sizeof req,
        "POST /api/v1/auth/email/register HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: application/json\r\n"
        "Content-Length: %d\r\nConnection: close\r\n\r\n%s", host, port, (int)strlen(json), json);
    int st = http_call(host, port, req, out, sizeof out);
    if (st != 200) die("register returned %d: %.200s", st, body_of(out));

    snprintf(json, sizeof json, "{\"email\":\"%s\",\"password\":\"%s\"}", email, pass);
    snprintf(req, sizeof req,
        "POST /api/v1/auth/email/login HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: application/json\r\n"
        "Content-Length: %d\r\nConnection: close\r\n\r\n%s", host, port, (int)strlen(json), json);
    st = http_call(host, port, req, out, sizeof out);
    if (st != 200) die("email login returned %d: %.200s", st, body_of(out));

    char token[IDUNA_LOGIN_TOKEN_MAX], player[64];
    if (!json_str(body_of(out), "token", token, sizeof token))
        die("login response carried no token: %.200s", body_of(out));
    if (!json_str(body_of(out), "player_id", player, sizeof player)) player[0] = 0;

    IdunaLogin L;
    if (iduna_login_begin_ex(&L, base, 0) != 0) die("iduna_login_begin_ex refused %s", base);
    if (!strstr(L.login_url, "/api/v1/auth/sso/login?redirect_uri="))
        die("unexpected login_url: %.120s", L.login_url);

    /* (2) fetch that page off the same host -- same URL the browser would open */
    const char *scheme = strstr(L.login_url, "://");
    const char *path = scheme ? strchr(scheme + 3, '/') : NULL;
    if (!path) die("login_url carries no path: %.120s", L.login_url);
    snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n\r\n", path, host, port);
    st = http_call(host, port, req, out, sizeof out);
    if (st != 200) die("SSO page returned %d: %.200s", st, body_of(out));
    if (!strstr(body_of(out), "sso_token")) die("SSO page is not the fragment-handoff page");

    /* (3) be the bounce page. encodeURIComponent leaves JWT characters alone, so the token goes in raw. */
    char form[IDUNA_LOGIN_TOKEN_MAX + 256], lreq[IDUNA_LOGIN_TOKEN_MAX + 512];
    snprintf(form, sizeof form, "state=%s&sso_token=%s&player_id=%s&display_name=Live%%20Test",
             L.state, token, player);
    snprintf(lreq, sizeof lreq,
        "POST /token HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nContent-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: %d\r\nConnection: close\r\n\r\n%s", L.port, (int)strlen(form), form);
    int s = dial("127.0.0.1", L.port);
    if (s < 0) die("connect to our own loopback listener failed: %s", strerror(errno));
    send_all(s, lreq, strlen(lreq));

    int r = 0;
    for (int i = 0; i < 500 && r == 0; i++) r = iduna_login_poll(&L);
    if (r != 1) { iduna_login_close(&L); die("iduna_login_poll returned %d, expected 1", r); }

    char reply[512];
    ssize_t got = recv(s, reply, sizeof reply - 1, 0);
    close(s);
    if (got <= 0) die("loopback listener sent no reply");
    reply[got] = 0;
    if (atoi(reply + 9) != 200) die("loopback /token replied %.40s", reply);

    if (strcmp(L.token, token) != 0) die("token did not survive the loopback: %.40s...", L.token);
    if (player[0] && strcmp(L.player_id, player) != 0)
        die("player_id drift: header '%s' vs IDUNA '%s'", L.player_id, player);
    if (strcmp(L.display_name, "Live Test") != 0)
        die("display_name form-decode: '%s'", L.display_name);
    iduna_login_close(&L);

    printf("iduna_login_live_test: OK  %s  account=%s  jwt=%d bytes  sso_page=200  loopback=200\n",
           base, email, (int)strlen(token));
    return 0;
}
