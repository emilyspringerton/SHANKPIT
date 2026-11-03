/* tools/shankpit_netd.c -- the CONSUMER half of the internet-of-SHANKPIT shim.
 *
 * Serves the snapshot packages/simulation/dw_net.c writes (SHANKPIT_NET_SNAPSHOT / --snapshot) over
 * plain HTTP as JSON. No dependencies, no framework, no state: one file in, one file out. The game
 * never opens a port and this daemon never reads the game's memory, which is why either can be
 * restarted, curl'd or tested without the other.
 *
 * Routes (both with the /api/v1/shankpit-net prefix WOTAN proxies and bare, for a local curl):
 *   GET/HEAD /api/v1/shankpit-net/deadweight   the snapshot, application/json
 *   GET/HEAD /api/v1/shankpit-net/health       {"ok":true,"snapshot_bytes":N,"snapshot_age_ms":M}
 *   OPTIONS anything                          204 + CORS (a dev server on another port)
 *
 * Correctness that is actually checked by scripts/shankpit-net-test.js: a strong-ish ETag on the
 * snapshot (file mtime + size) with 304 on a matching If-None-Match, an honest 404 that names the
 * path it did not find, 405 (with Allow) for a write method, and CORS on every response.
 *
 * POSIX only, unlike tools/local_nock_host.c's dual-platform host: this process is a deployment-side
 * daemon behind WOTAN's nginx, not something a player's client ever runs. Named, not hidden.
 */
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>

#define REQ_MAX 8192

static char g_snapshot[1024];
static char g_bind[64] = "127.0.0.1";
static int g_port = 8791;

static int read_file(const char *path, char **out, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return 0; }
    rewind(f);
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return 0; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = 0;
    *out = buf;
    *len = got;
    return 1;
}

static void send_all(int fd, const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, buf + off, len - off, 0);
        if (n <= 0) return;
        off += (size_t)n;
    }
}

/* respond -- one response, no keep-alive (Connection: close). `extra` is a newline-terminated header
 * block or "". HEAD gets the same headers and no body, which is what a cache expects. */
static void respond(int fd, int code, const char *reason, const char *ctype, const char *extra,
                    const char *body, size_t len, int head_only) {
    char header[2048];
    int n = snprintf(header, sizeof header,
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %zu\r\n"
                     "Access-Control-Allow-Origin: *\r\n"
                     "Access-Control-Allow-Methods: GET, HEAD, OPTIONS\r\n"
                     "Access-Control-Allow-Headers: If-None-Match\r\n"
                     "Connection: close\r\n"
                     "%s"
                     "\r\n",
                     code, reason, ctype, len, extra ? extra : "");
    if (n > 0) send_all(fd, header, (size_t)n);
    if (!head_only && body && len) send_all(fd, body, len);
}

static void json_escape_into(char *dst, size_t cap, const char *src) {
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p && o + 2 < cap; p++) {
        if (*p == '"' || *p == '\\') { dst[o++] = '\\'; dst[o++] = (char)*p; }
        else if (*p < 0x20 || *p > 0x7e) dst[o++] = '?';
        else dst[o++] = (char)*p;
    }
    dst[o] = 0;
}

/* header_value -- the value of `name` in the request, into `out`. Returns 1 if present. */
static int header_value(const char *req, const char *name, char *out, size_t cap) {
    size_t nlen = strlen(name);
    const char *line = strstr(req, "\r\n");
    while (line) {
        line += 2;
        if (strncasecmp(line, name, nlen) == 0 && line[nlen] == ':') {
            const char *v = line + nlen + 1;
            while (*v == ' ' || *v == '\t') v++;
            size_t i = 0;
            while (*v && *v != '\r' && *v != '\n' && i + 1 < cap) out[i++] = *v++;
            out[i] = 0;
            return 1;
        }
        line = strstr(line, "\r\n");
    }
    return 0;
}

static void serve(int fd, char *req) {
    char method[16] = "", path[1024] = "", proto[16] = "";
    const char *eol = strstr(req, "\r\n");
    if (!eol) { respond(fd, 400, "Bad Request", "application/json", "",
                        "{\"error\":\"malformed request line\"}", 35, 0); return; }
    char line[1200];
    size_t llen = (size_t)(eol - req);
    if (llen >= sizeof line) llen = sizeof line - 1;
    memcpy(line, req, llen);
    line[llen] = 0;
    if (sscanf(line, "%15s %1023s %15s", method, path, proto) != 3) {
        respond(fd, 400, "Bad Request", "application/json", "",
                "{\"error\":\"malformed request line\"}", 35, 0);
        return;
    }
    char *q = strchr(path, '?');   /* the query is never read: nothing here is parameterised */
    if (q) *q = 0;

    int head_only = strcmp(method, "HEAD") == 0, get = strcmp(method, "GET") == 0;

    if (strcmp(method, "OPTIONS") == 0) { respond(fd, 204, "No Content", "text/plain", "", "", 0, 1); return; }
    if (!get && !head_only) {
        respond(fd, 405, "Method Not Allowed", "application/json", "Allow: GET, HEAD, OPTIONS\r\n",
                "{\"error\":\"this feed is read-only\"}", 33, 0);
        return;
    }

    /* strip the proxied prefix, then match the two routes */
    const char *route = path;
    const char *prefix = "/api/v1/shankpit-net";
    if (strncmp(route, prefix, strlen(prefix)) == 0) route += strlen(prefix);
    if (*route == 0) route = "/";

    if (strcmp(route, "/health") == 0) {
        struct stat st;
        int have = (stat(g_snapshot, &st) == 0);
        time_t now = time(NULL);
        long age_ms = have ? (long)(now - st.st_mtime) * 1000L : -1;
        char body[512];
        int n = snprintf(body, sizeof body,
                         "{\"ok\":%s,\"snapshot\":\"%s\",\"snapshot_bytes\":%lld,\"snapshot_age_ms\":%ld,\"schema\":1}",
                         have ? "true" : "false", g_snapshot,
                         have ? (long long)st.st_size : 0LL, age_ms);
        respond(fd, 200, "OK", "application/json", "Cache-Control: no-store\r\n", body,
                (size_t)(n > 0 ? n : 0), head_only);
        return;
    }

    if (strcmp(route, "/deadweight") == 0) {
        struct stat st;
        char *body = NULL;
        size_t len = 0;
        if (stat(g_snapshot, &st) != 0 || !read_file(g_snapshot, &body, &len)) {
            char esc[1100], msg[1200];
            json_escape_into(esc, sizeof esc, g_snapshot);
            int n = snprintf(msg, sizeof msg,
                             "{\"error\":\"no snapshot yet\",\"snapshot\":\"%s\","
                             "\"hint\":\"the game writes it when SHANKPIT_NET_SNAPSHOT is set\"}", esc);
            respond(fd, 404, "Not Found", "application/json", "Cache-Control: no-store\r\n", msg,
                    (size_t)(n > 0 ? n : 0), head_only);
            return;
        }
        char etag[64];
        snprintf(etag, sizeof etag, "\"%llx-%llx\"", (unsigned long long)st.st_mtime,
                 (unsigned long long)st.st_size);
        char inm[128];
        if (header_value(req, "If-None-Match", inm, sizeof inm) && strcmp(inm, etag) == 0) {
            char extra[160];
            snprintf(extra, sizeof extra, "ETag: %s\r\nCache-Control: no-cache\r\n", etag);
            free(body);
            respond(fd, 304, "Not Modified", "application/json", extra, "", 0, 1);
            return;
        }
        char extra[256];
        snprintf(extra, sizeof extra, "ETag: %s\r\nCache-Control: no-cache\r\nX-Snapshot-Bytes: %zu\r\n",
                 etag, len);
        respond(fd, 200, "OK", "application/json; charset=utf-8", extra, body, len, head_only);
        free(body);
        return;
    }

    char esc[1100], msg[1200];
    json_escape_into(esc, sizeof esc, path);
    int n = snprintf(msg, sizeof msg,
                     "{\"error\":\"no route\",\"path\":\"%s\",\"routes\":["
                     "\"/api/v1/shankpit-net/deadweight\",\"/api/v1/shankpit-net/health\"]}", esc);
    respond(fd, 404, "Not Found", "application/json", "Cache-Control: no-store\r\n", msg,
            (size_t)(n > 0 ? n : 0), head_only);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);

    const char *env = getenv("SHANKPIT_NET_SNAPSHOT");
    snprintf(g_snapshot, sizeof g_snapshot, "%s", env && *env ? env : "var/deadweight/net.json");

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("shankpit_netd -- serve the DEADWEIGHT-in-SHANKPIT ladder as JSON\n"
                   "  --snapshot FILE   the file the game writes (default $SHANKPIT_NET_SNAPSHOT\n"
                   "                    or var/deadweight/net.json)\n"
                   "  --port N          listen port (default 8791)\n"
                   "  --bind ADDR       bind address (default 127.0.0.1 -- put nginx in front)\n");
            return 0;
        }
        else if (strcmp(argv[i], "--snapshot") == 0 && i + 1 < argc) snprintf(g_snapshot, sizeof g_snapshot, "%s", argv[++i]);
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) g_port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--bind") == 0 && i + 1 < argc) snprintf(g_bind, sizeof g_bind, "%s", argv[++i]);
        else { fprintf(stderr, "shankpit_netd: unknown argument %s (--help)\n", argv[i]); return 2; }
    }
    if (g_port <= 0 || g_port > 65535) { fprintf(stderr, "shankpit_netd: bad port\n"); return 2; }

    signal(SIGPIPE, SIG_IGN);

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }
    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)g_port);
    if (inet_pton(AF_INET, g_bind, &addr.sin_addr) != 1) {
        fprintf(stderr, "shankpit_netd: bad bind address %s\n", g_bind);
        return 2;
    }
    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) != 0) { perror("bind"); return 1; }
    if (listen(srv, 16) != 0) { perror("listen"); return 1; }

    printf("shankpit_netd: http://%s:%d/api/v1/shankpit-net/deadweight\n", g_bind, g_port);
    printf("shankpit_netd: serving %s\n", g_snapshot);

    for (;;) {
        struct sockaddr_in peer;
        socklen_t plen = sizeof peer;
        int fd = accept(srv, (struct sockaddr *)&peer, &plen);
        if (fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            return 1;
        }
        char req[REQ_MAX];
        ssize_t got = recv(fd, req, sizeof req - 1, 0);
        if (got <= 0) { close(fd); continue; }
        req[got] = 0;
        serve(fd, req);
        close(fd);
    }
}
