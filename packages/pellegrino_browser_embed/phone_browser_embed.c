/* phone_browser_embed.c -- the real in-phone PELLEGRINO browser (SHANKPIT-PELLEGRINO-APP-02). One
 * translation unit holds PELLEGRINO's generated engine (pellegrino_gen.inc, built with the HTTP/3 +
 * TLS stdlib deps -- see scripts/gen_pellegrino_browser_embed.sh), PARENA's runtime and the thin C API
 * in phone_browser_embed.h. pg_rename.h (generated) #defines every engine/runtime global to pgb_<name>
 * for this TU only, so this links alongside packages/pellegrino_embed (pgm_<name>) and SHANKPIT's own
 * packages/world/parena_runtime.c (plain arena_*) with zero collisions. System headers are pulled in
 * BEFORE the renames so libc/SDL/mbedTLS declarations are never rewritten. */
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
#include <SDL2/SDL.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#endif

#define PARENA_NO_TTF 1
#ifdef PG_RENAME_HEADER
#include PG_RENAME_HEADER
#else
#include "pg_rename.h"
#endif
#include "runtime/parena_runtime.h"
#include "pellegrino_gen.inc"
#include "runtime/parena_runtime.c"
#include "phone_browser_embed.h"

static int g_ren = -1;
static int g_w = 0, g_h = 0;
static Arena g_arena;
static int g_arena_ready = 0;
static char g_home_page[] = "<html><body>PELLEGRINO -- type an address and press Enter.</body></html>";

int phone_browser_open(int w, int h) {
    if (g_ren >= 0) return 0;
    if (w <= 0 || h <= 0) return -1;
    if (!g_arena_ready) { arena_init(&g_arena); g_arena_ready = 1; }
    g_ren = embed_nav_open(w, h);
    if (g_ren < 0) return -1;
    g_w = w; g_h = h;
    return 0;
}

int phone_browser_close(void) {
    if (g_arena_ready) { arena_free_all(&g_arena); g_arena_ready = 0; }
    g_ren = -1; g_w = 0; g_h = 0;
    return 0;
}

int phone_browser_navigate(const char *url) {
    if (g_ren < 0 || !url) return -1;
    return embed_nav_start((char *)url, &g_arena);
}

const unsigned int *phone_browser_step(int win_w, int win_h) {
    if (g_ren < 0) return NULL;
    (void)embed_nav_step(g_ren, g_home_page, (int)(sizeof g_home_page - 1), win_w, win_h, &g_arena);
    return (const unsigned int *)sdl2_surface_renderer_pixels_impl(g_ren);
}

int phone_browser_click(int x, int y) {
    if (g_ren < 0) return 0;
    return embed_nav_click(x, y);
}

int phone_browser_wheel(int delta) {
    if (g_ren < 0) return 0;
    return embed_nav_wheel(delta);
}

int phone_browser_text(char c) {
    char buf[2];
    if (g_ren < 0 || c < 32 || c > 126) return 0;
    buf[0] = c; buf[1] = '\0';
    return embed_nav_text(buf);
}

int phone_browser_key(int sdl_keycode) {
    if (g_ren < 0) return 0;
    return embed_nav_key(sdl_keycode, &g_arena);
}

#ifdef PHONE_BROWSER_DEBUG_PEEK
/* Verification-only: exposes the real, internal page/stream bytes PELLEGRINO fetched, so a test
 * harness can assert on real page content instead of just "pixels changed." Never compiled into a
 * real SHANKPIT build (no caller defines PHONE_BROWSER_DEBUG_PEEK outside tests). */
int phone_browser_debug_peek(char *out, int cap) {
    int n = stream_len();
    char *buf = stream_buf();
    if (n > cap) n = cap;
    memcpy(out, buf, (size_t)n);
    return n;
}
#endif
