/* os_menu_embed_test.c -- the SHANKPIT OS menu through PELLEGRINO, headless (SDL_VIDEODRIVER=dummy).
 * Build/run: make test-os-menu   (links packages/world/parena_runtime.c too, to prove no symbol collision) */
#include "os_menu_embed.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", m); fails++; } else printf("ok: %s\n", m); } while (0)

#define W 760
#define H 480

static void dump(const char *path, const unsigned *px) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) { unsigned p = px[i]; fputc((p >> 16) & 255, f); fputc((p >> 8) & 255, f); fputc(p & 255, f); }
    fclose(f);
}

int main(int argc, char **argv) {
    static char page[OS_MENU_PAGE_MAX];
    const char *labels[] = { "SURVIVAL", "STORY", "CAVE-001", "SOLO", "TRAIN", "CUSTOMIZE" };
    const char *settings[] = { "LIVE LEVELS: OFF", "BULLET HOLES: ON", "< GAMES" };
    char act[96];
    int n;
    SDL_Init(SDL_INIT_VIDEO);
    CHECK(os_menu_draw() == NULL, "draw before init is a safe NULL");
    CHECK(os_menu_init(W, H) == 0, "init");
    n = os_menu_build_page(page, sizeof page, labels, 6, 1);
    CHECK(n > 0 && strstr(page, "shankpit:go/5") != NULL, "page built with 6 tiles");
    CHECK(strstr(page, "sel") != NULL, "selected tile marked");
    CHECK(os_menu_set_page(page) == 1, "page installed");
    CHECK(os_menu_set_page(page) == 0, "identical page is a no-op");
    const unsigned *px = os_menu_draw();
    CHECK(px != NULL, "draw returns pixels");
    if (argc > 1) dump(argv[1], px);
    {   /* every tile has a distinct background; count distinct non-background colours */
        unsigned seen[16]; int ns = 0, bg = (int)px[W * H - 1];
        for (int i = 0; i < W * H && ns < 16; i += 7) {
            unsigned c = px[i]; int k, dup = 0;
            if ((int)c == bg) continue;
            for (k = 0; k < ns; k++) if (seen[k] == c) dup = 1;
            if (!dup) seen[ns++] = c;
        }
        printf("distinct colours: %d\n", ns);
        CHECK(ns >= 6, "tile backgrounds painted");
    }
    /* scan for each tile: click a grid of points, record which index responds */
    int found[8] = {0}, hits = 0;
    for (int y = 4; y < H; y += 10) for (int x = 4; x < W; x += 10) {
        if (os_menu_click(x, y) && os_menu_action(act, sizeof act)) {
            int idx = os_menu_parse_action(act);
            if (idx >= 0 && idx < 8 && !found[idx]) { found[idx] = 1; hits++; printf("tile %d at (%d,%d) -> %s\n", idx, x, y, act); }
        }
    }
    CHECK(hits == 6, "all six tiles are clickable and map to their own index");
    CHECK(os_menu_parse_action("shankpit:back") == -2 && os_menu_parse_action("http://x") == -1, "action parser");
    /* keyboard */
    os_menu_key(SDLK_TAB);
    CHECK(os_menu_key(SDLK_RETURN) == 1 && os_menu_action(act, sizeof act) && os_menu_parse_action(act) >= 0, "Tab+Enter activates");
    n = os_menu_build_page(page, sizeof page, settings, 3, 0);
    CHECK(os_menu_set_page(page) == 1 && strstr(page, "&lt;") != NULL, "labels are HTML-escaped");
    px = os_menu_draw();
    CHECK(px != NULL, "second page draws");
    if (argc > 2) dump(argv[2], px);
    printf("%s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
    return fails ? 1 : 0;
}
