/* phone_chromium.c -- STOPGAP: the SHANKPHONE's web games in headless Chromium (see phone_chromium.h for why, and
 * EMILY/BACKLOG.md for the PARENA replacement: Pellegrino running the page's WebAssembly itself).
 *
 * One translation unit holds PARENA's generated WebDriver client + base64 + PNG decoder (webdriver_gen.inc, from
 * scripts/gen_chromium_bridge.sh), PARENA's runtime, and the phc_* API. wd_rename.h (generated) renames every global
 * they define to phw_<name> for this TU only, so it links beside the lobby's own narrower runtime. System headers come
 * BEFORE the renames so libc is never rewritten.
 *
 * What is decided lives in PARENA (SHANKPIT packages/simulation/arcade_rules.c, from stdlib/shankpit/arcade.prn): how
 * often to screenshot, the window size, Chromium's arguments, which SDL key is which WebDriver key. This file is the
 * process, the thread and the hand-off. */
#include "phone_chromium.h"

#ifdef _WIN32
/* Windows: no stopgap yet (it would need CreateProcess + a Windows chromedriver). The phone says the game needs
   Chromium. */
int phc_available(void) { return 0; }
void phc_open(const char *url) { (void)url; }
void phc_key(int sym, int down) { (void)sym; (void)down; }
int phc_frame(uint8_t *dst, int cap, int *w, int *h, int *seq) { (void)dst; (void)cap; (void)w; (void)h; (void)seq; return 0; }
void phc_status(char *buf, int cap) { if (cap > 0) buf[0] = 0; }
void phc_close(void) {}
#else

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <math.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <pty.h>

extern char **environ;

/* the rules (arcade_rules.c), declared before the renames so they keep their own names */
int arcade_shot_ms(void);
int arcade_chromium_w(void);
int arcade_chromium_h(void);
char *arcade_chromium_args(void);
char *arcade_wd_key_special(int);
int arcade_wd_key_char(int);

#define PARENA_NO_TTF 1
#ifdef WD_RENAME_HEADER
#include WD_RENAME_HEADER
#else
#include "wd_rename.h"
#endif
#include "runtime/parena_runtime.h"
#include "webdriver_gen.inc"
#include "runtime/parena_runtime.c"

/* json.prn's json-unescape is a host function (a per-host companion, the same body as PARENA tests/test_webdriver.c's):
   decode a JSON string's escapes from s[start, end) into out, NUL-terminated. A screenshot is plain base64, so this
   mostly copies; the session id and error text are where an escape could appear. */
static int phc_hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}
char *host_json_unescape(char *s, int start, int end, char *out) {
    int oi = 0;
    for (int i = start; i < end; i++) {
        if (s[i] == '\\' && i + 1 < end) {
            i++;
            switch (s[i]) {
            case 'b': out[oi++] = '\b'; break;
            case 'f': out[oi++] = '\f'; break;
            case 'n': out[oi++] = '\n'; break;
            case 'r': out[oi++] = '\r'; break;
            case 't': out[oi++] = '\t'; break;
            case 'u':
                if (i + 4 < end) {
                    int cp = 0;
                    for (int k = 0; k < 4; k++) { i++; cp = (cp << 4) | phc_hex_nibble(s[i]); }
                    if (cp < 0x80) out[oi++] = (char)cp;
                    else if (cp < 0x800) { out[oi++] = (char)(0xC0 | (cp >> 6)); out[oi++] = (char)(0x80 | (cp & 0x3F)); }
                    else {
                        out[oi++] = (char)(0xE0 | (cp >> 12));
                        out[oi++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        out[oi++] = (char)(0x80 | (cp & 0x3F));
                    }
                }
                break;
            default: out[oi++] = s[i];   /* \" \\ \/ */
            }
        } else {
            out[oi++] = s[i];
        }
    }
    out[oi] = 0;
    return out;
}

/* ---- the shared state (the worker writes the frame and status, the game reads them; the game queues keys) ---- */
#define PHC_KEYQ 64
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_thread;
static int g_running = 0;        /* a worker exists (joined on close) */
static volatile int g_quit = 0;
static char g_url[512];
static char g_status[192];
static uint8_t *g_fb = NULL;
static int g_fb_w = 0, g_fb_h = 0, g_fb_seq = 0;
static int g_keys[PHC_KEYQ][2];
static int g_nkeys = 0;
static pid_t g_driver = 0;

static void set_status(const char *fmt, ...) {
    char buf[sizeof g_status];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    pthread_mutex_lock(&g_mu);
    memcpy(g_status, buf, sizeof g_status);
    pthread_mutex_unlock(&g_mu);
}

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* A program on PATH (or the path itself when it has a '/'), into out. */
static int find_program(const char *name, char *out, size_t cap) {
    if (!name || !name[0]) return 0;
    if (strchr(name, '/')) {
        if (access(name, X_OK) == 0) { snprintf(out, cap, "%s", name); return 1; }
        return 0;
    }
    const char *path = getenv("PATH");
    if (!path) return 0;
    char dir[1024];
    while (*path) {
        size_t n = strcspn(path, ":");
        if (n > 0 && n < sizeof dir) {
            memcpy(dir, path, n); dir[n] = 0;
            char full[1200];
            snprintf(full, sizeof full, "%s/%s", dir, name);
            if (access(full, X_OK) == 0) { snprintf(out, cap, "%s", full); return 1; }
        }
        path += n + (path[n] == ':');
    }
    return 0;
}

static int find_driver(char *out, size_t cap) {
    const char *env = getenv("SHANKPIT_CHROMEDRIVER");
    if (env && env[0]) return find_program(env, out, cap);
    return find_program("chromedriver", out, cap);
}

/* the browser binary, or "" to let chromedriver find its own */
static void find_browser(char *out, size_t cap) {
    out[0] = 0;
    const char *env = getenv("SHANKPIT_CHROMIUM");
    if (env && env[0]) { find_program(env, out, cap); return; }
    const char *names[] = { "chromium", "chromium-browser", "google-chrome", "google-chrome-stable" };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) if (find_program(names[i], out, cap)) return;
}

int phc_available(void) {
    static int cached = -1;
    if (cached < 0) { char p[1200]; cached = find_driver(p, sizeof p); }
    return cached;
}

/* Is something answering on 127.0.0.1:port? */
static int port_open(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int ok = connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0;
    close(fd);
    return ok;
}

static void stop_driver(void) {
    if (g_driver > 0) {
        kill(g_driver, SIGTERM);
        for (int i = 0; i < 50; i++) { if (waitpid(g_driver, NULL, WNOHANG) == g_driver) { g_driver = 0; return; } usleep(20000); }
        kill(g_driver, SIGKILL);
        waitpid(g_driver, NULL, 0);
        g_driver = 0;
    }
}

/* Start chromedriver on a free local port; returns the port, 0 on failure. */
static int start_driver(void) {
    char drv[1200];
    if (!find_driver(drv, sizeof drv)) { set_status("NO CHROMEDRIVER (set SHANKPIT_CHROMEDRIVER)"); return 0; }
    int port = 0;
    for (int p = 9600 + (int)(getpid() % 200); p < 9900; p++) if (!port_open(p)) { port = p; break; }
    if (!port) { set_status("NO FREE PORT FOR CHROMEDRIVER"); return 0; }
    char parg[32];
    snprintf(parg, sizeof parg, "--port=%d", port);
    char *argv[] = { drv, parg, "--silent", NULL };
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    int rc = posix_spawn(&g_driver, drv, &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) { g_driver = 0; set_status("CHROMEDRIVER WOULD NOT START (%s)", strerror(rc)); return 0; }
    for (int i = 0; i < 100 && !g_quit; i++) {   /* up to 5 s */
        if (port_open(port)) return port;
        usleep(50000);
    }
    set_status("CHROMEDRIVER DID NOT ANSWER");
    stop_driver();
    return 0;
}

static const char *wd_error(Result r) {
    if (!r.value) return "no answer";
    WebDriverError *e = (WebDriverError *)r.value;
    if (e->tag == WebDriverError_TAG_ConnectFailed) return "could not connect";
    if (e->tag == WebDriverError_TAG_BadResponse) return "bad reply";
    /* ProtocolError carries the driver's JSON body: pull out its "message" (first line) -- it says why, e.g. a
       chromedriver that does not match the browser's version */
    static __thread char msg[128];
    const char *body = (const char *)e->value, *m = body ? strstr(body, "\"message\":\"") : NULL;
    if (!m) return "refused";
    m += 11;
    size_t n = 0;
    while (m[n] && m[n] != '"' && !(m[n] == '\\' && m[n + 1] == 'n') && n + 1 < sizeof msg) n++;
    memcpy(msg, m, n);
    msg[n] = 0;
    return msg;
}

/* the WebDriver key value for an SDL key into out ("" for a key the page does not get) */
static void wd_key(int sym, char *out, size_t cap) {
    const char *named = arcade_wd_key_special(sym);
    if (named && named[0]) { snprintf(out, cap, "%s", named); return; }
    int c = arcade_wd_key_char(sym);
    out[0] = 0;
    if (c > 0) snprintf(out, cap, "%c", (char)c);
}

static void *worker(void *arg) {
    (void)arg;
    set_status("STARTING CHROMEDRIVER");
    int port = start_driver();
    if (!port) return NULL;

    Arena sa;              /* the session's: lives as long as the session */
    arena_init(&sa);
    char browser[1200];
    find_browser(browser, sizeof browser);
    char caps[2048];
    if (browser[0])
        snprintf(caps, sizeof caps, "{\"goog:chromeOptions\":{\"binary\":\"%s\",\"args\":[%s]}}", browser, arcade_chromium_args());
    else
        snprintf(caps, sizeof caps, "{\"goog:chromeOptions\":{\"args\":[%s]}}", arcade_chromium_args());
    set_status("STARTING CHROMIUM");
    Result rs = new_session_with_capabilities("127.0.0.1", port, caps, &sa);
    if (rs.tag != 1) {
        set_status("CHROMIUM WOULD NOT START (%s)", wd_error(rs));
        arena_free_all(&sa); stop_driver(); return NULL;
    }
    Session *sess = (Session *)rs.value;
    set_status("OPENING THE PAGE");
    {
        Arena a; arena_init(&a);
        char url[sizeof g_url];
        pthread_mutex_lock(&g_mu); memcpy(url, g_url, sizeof url); pthread_mutex_unlock(&g_mu);
        Result rn = navigate_to(sess, url, &a);
        arena_free_all(&a);
        if (rn.tag != 1) set_status("THE PAGE DID NOT OPEN (%s)", wd_error(rn));
        else set_status("");
    }

    long last = 0;
    int fails = 0;
    while (!g_quit) {
        int keys[PHC_KEYQ][2], nk;
        pthread_mutex_lock(&g_mu);
        nk = g_nkeys;
        memcpy(keys, g_keys, sizeof(int) * 2 * (size_t)nk);
        g_nkeys = 0;
        pthread_mutex_unlock(&g_mu);
        for (int i = 0; i < nk && !g_quit; i++) {
            char v[16];
            wd_key(keys[i][0], v, sizeof v);
            if (!v[0]) continue;
            Arena a; arena_init(&a);
            perform_key(sess, v, keys[i][1], &a);
            arena_free_all(&a);
        }
        long t = now_ms();
        if (t - last < arcade_shot_ms()) { if (!nk) usleep(4000); continue; }
        last = t;
        Arena a; arena_init(&a);
        Result r = take_screenshot(sess, &a);
        if (r.tag == 1) {
            Bytes png = base64_decode((char *)r.value, &a);
            PngImage img = png_decode(png, &a);
            if (img.ok && img.width > 0 && img.height > 0 && img.pixels.len >= img.width * img.height * 4) {
                pthread_mutex_lock(&g_mu);
                size_t need = (size_t)img.width * (size_t)img.height * 4;
                if (g_fb_w != img.width || g_fb_h != img.height || !g_fb) {
                    free(g_fb);
                    g_fb = (uint8_t *)malloc(need);
                    g_fb_w = img.width; g_fb_h = img.height;
                }
                if (g_fb) { memcpy(g_fb, img.pixels.data, need); g_fb_seq++; }
                pthread_mutex_unlock(&g_mu);
                fails = 0;
            }
        } else if (++fails >= 10) {
            set_status("CHROMIUM STOPPED ANSWERING (%s)", wd_error(r));
            arena_free_all(&a);
            break;
        }
        arena_free_all(&a);
    }

    Arena a; arena_init(&a);
    release_actions(sess, &a);
    quit_session(sess, &a);
    arena_free_all(&a);
    arena_free_all(&sa);
    stop_driver();
    return NULL;
}

void phc_close(void) {
    if (g_running) {
        g_quit = 1;
        pthread_join(g_thread, NULL);
        g_running = 0;
    }
    pthread_mutex_lock(&g_mu);
    free(g_fb); g_fb = NULL; g_fb_w = g_fb_h = 0;
    g_nkeys = 0;
    g_status[0] = 0;
    pthread_mutex_unlock(&g_mu);
}

void phc_open(const char *url) {
    phc_close();
    signal(SIGPIPE, SIG_IGN);   /* a browser that goes away mid-write must not take the game with it */
    pthread_mutex_lock(&g_mu);
    snprintf(g_url, sizeof g_url, "%s", url);
    snprintf(g_status, sizeof g_status, "STARTING CHROMEDRIVER");
    pthread_mutex_unlock(&g_mu);
    g_quit = 0;
    if (pthread_create(&g_thread, NULL, worker, NULL) == 0) g_running = 1;
    else set_status("COULD NOT START THE BRIDGE THREAD");
}

void phc_key(int sym, int down) {
    pthread_mutex_lock(&g_mu);
    if (g_running && g_nkeys < PHC_KEYQ) { g_keys[g_nkeys][0] = sym; g_keys[g_nkeys][1] = down ? 1 : 0; g_nkeys++; }
    pthread_mutex_unlock(&g_mu);
}

int phc_frame(uint8_t *dst, int cap, int *w, int *h, int *seq) {
    int copied = 0;
    pthread_mutex_lock(&g_mu);
    if (g_fb && g_fb_seq != *seq && cap >= g_fb_w * g_fb_h * 4) {
        memcpy(dst, g_fb, (size_t)g_fb_w * (size_t)g_fb_h * 4);
        *w = g_fb_w; *h = g_fb_h; *seq = g_fb_seq;
        copied = 1;
    }
    pthread_mutex_unlock(&g_mu);
    return copied;
}

void phc_status(char *buf, int cap) {
    if (cap <= 0) return;
    pthread_mutex_lock(&g_mu);
    snprintf(buf, (size_t)cap, "%s", g_status);
    pthread_mutex_unlock(&g_mu);
}

#endif
