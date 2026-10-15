/* os_menu_embed.c -- SHANKPIT OS menu on HTML + CSS, painted by PELLEGRINO (kanban #627, 2026-10-15).
 *
 * One translation unit holds PELLEGRINO's generated engine (pellegrino_gen.inc), PARENA's runtime and the thin
 * C API in os_menu_embed.h. The engine defines hundreds of plain global functions (`add`, `init`, `length`...)
 * and the runtime defines arena_*; SHANKPIT already links a different, narrower arena_* from
 * packages/world/parena_runtime.c. pg_rename.h (generated) `#define`s every one of them to pgm_<name> for this
 * TU only, so this file links like any other LOBBY_SRC entry with no objcopy step. System headers are pulled in
 * BEFORE the renames so libc/SDL declarations are never rewritten. */
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
#include <termios.h>
#include <sys/ioctl.h>
#include <pty.h>
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
#include "os_menu_embed.h"

/* tile widths in 12 px glyph cells: (760 - 24 margin - 24 gap) / 2 and 760 - 24, less border */
#define OS_MENU_TILE_CELLS_HALF 28
#define OS_MENU_TILE_CELLS_FULL 59

static int g_ren = -1;
static int g_w = 0, g_h = 0;
static int g_dirty = 0;
static int g_n = 0;
static char g_page[OS_MENU_PAGE_MAX];
static char g_action[96];

/* The whole look of the menu: basic CSS, the same properties any web page would use. The tile colours are the
 * ones draw_lobby_buttons used before this moved to HTML, so the menu keeps its palette. */
static const char OS_MENU_CSS[] =
    "body{background:#05050d;color:#ffffff}"
    ".row{display:flex;gap:24px;margin:0}"
    ".b{margin:6px 0;border:3px solid #05050d}"
    ".b h3{margin:0}"
    "a{text-decoration:none}"
    ".b a{color:#ffffff}"
    ".sel{border:3px solid #ffe600}"
    ".c0{background:#1abf33}.c1{background:#1a73f2}.c2{background:#bf33bf}.c3{background:#00bfbf}"
    ".c4{background:#e6bf1a}.c5{background:#40b34d}.c6{background:#d93333}.c7{background:#339999}"
    ".c4 a,.c3 a{color:#05050d}";

int os_menu_init(int w, int h) {
    if (g_ren >= 0) return 0;
    if (w <= 0 || h <= 0) return -1;
    g_ren = embed_open(w, h);
    if (g_ren < 0) return -1;
    g_w = w; g_h = h; g_dirty = 1; g_n = 0; g_page[0] = 0; g_action[0] = 0;
    return 0;
}

static int put(char *out, int cap, int *n, const char *s) {
    int l = (int)strlen(s);
    if (*n + l >= cap) return -1;
    memcpy(out + *n, s, (size_t)l);
    *n += l;
    out[*n] = 0;
    return 0;
}

static int put_escaped(char *out, int cap, int *n, const char *s) {
    for (; *s; s++) {
        const char *rep = *s == '&' ? "&amp;" : *s == '<' ? "&lt;" : *s == '>' ? "&gt;" : NULL;
        char one[2] = { *s, 0 };
        if (put(out, cap, n, rep ? rep : one) < 0) return -1;
    }
    return 0;
}

int os_menu_build_page(char *out, int cap, const char *const *labels, int n, int selected) {
    int len = 0;
    char buf[64];
    if (!out || cap < 64) return -1;
    out[0] = 0;
    if (put(out, cap, &len, "<html><head><style>") < 0) return -1;
    if (put(out, cap, &len, OS_MENU_CSS) < 0) return -1;
    if (put(out, cap, &len, "</style></head><body>") < 0) return -1;
    for (int i = 0; i < n; i++) {
        if (i % 2 == 0 && put(out, cap, &len, "<div class=\"row\">") < 0) return -1;
        snprintf(buf, sizeof buf, "<div class=\"b c%d%s\"><h3><a href=\"shankpit:go/%d\">", i % 8,
                 i == selected ? " sel" : "", i);
        if (put(out, cap, &len, buf) < 0) return -1;
        /* PELLEGRINO hit-tests link text per glyph cell, so the link is label + nbsp padding out to the tile
         * width, on two lines: a click anywhere on the tile (not just on the letters) activates it. A tile
         * alone on its row spans the full width. */
        {
            int used = 1 + (int)strlen(labels[i] ? labels[i] : "");
            int alone = (i % 2 == 0 && i == n - 1);
            int want = alone ? OS_MENU_TILE_CELLS_FULL : OS_MENU_TILE_CELLS_HALF;
            if (put(out, cap, &len, "&nbsp;") < 0) return -1;
            if (put_escaped(out, cap, &len, labels[i] ? labels[i] : "") < 0) return -1;
            for (int k = used; k < want; k++) if (put(out, cap, &len, "&nbsp;") < 0) return -1;
            if (put(out, cap, &len, "<br>") < 0) return -1;
            for (int k = 0; k < want; k++) if (put(out, cap, &len, "&nbsp;") < 0) return -1;
        }
        if (put(out, cap, &len, "</a></h3></div>") < 0) return -1;
        if ((i % 2 == 1 || i == n - 1) && put(out, cap, &len, "</div>") < 0) return -1;
    }
    if (put(out, cap, &len, "</body></html>") < 0) return -1;
    return len;
}

int os_menu_set_page(const char *html) {
    size_t l;
    if (!html) return 0;
    l = strlen(html);
    if (l >= OS_MENU_PAGE_MAX) return 0;
    if ((int)l == g_n && memcmp(g_page, html, l) == 0) return 0;
    memcpy(g_page, html, l + 1);
    g_n = (int)l;
    g_dirty = 1;
    if (g_ren >= 0) embed_init(g_w, g_h);
    return 1;
}

const unsigned int *os_menu_draw(void) {
    if (g_ren < 0) return NULL;
    if (g_dirty) {
        embed_draw(g_ren, g_page, g_n, g_w, g_h, 1);
        g_dirty = 0;
    }
    return (const unsigned int *)sdl2_surface_renderer_pixels_impl(g_ren);
}

/* click/key events are resolved by the engine's NEXT paint pass (that is where link rectangles are known), so
 * force one and collect the result. */
static int collect(void) {
    int s, e;
    if (g_ren < 0) return 0;
    embed_draw(g_ren, g_page, g_n, g_w, g_h, 1);
    g_dirty = 0;
    s = embed_action_start();
    e = embed_action_end();
    if (s < 0 || e <= s || e > g_n) { embed_ack(); return 0; }
    {
        int l = e - s;
        if (l >= (int)sizeof g_action) l = (int)sizeof g_action - 1;
        memcpy(g_action, g_page + s, (size_t)l);
        g_action[l] = 0;
    }
    embed_ack();
    return 1;
}

int os_menu_click(int x, int y) {
    if (g_ren < 0) return 0;
    embed_click(x, y);
    return collect();
}

int os_menu_key(int k) {
    if (g_ren < 0) return 0;
    /* settle first: link rectangles / the selected link's href range only exist after a paint pass */
    embed_draw(g_ren, g_page, g_n, g_w, g_h, 1);
    embed_key(k);
    return collect();
}

int os_menu_action(char *out, int cap) {
    if (!g_action[0] || !out || cap < 1) return 0;
    snprintf(out, (size_t)cap, "%s", g_action);
    g_action[0] = 0;
    return 1;
}

int os_menu_parse_action(const char *href) {
    if (!href) return -1;
    if (strcmp(href, "shankpit:back") == 0) return -2;
    if (strncmp(href, "shankpit:go/", 12) == 0 && href[12] >= '0' && href[12] <= '9') return atoi(href + 12);
    return -1;
}
