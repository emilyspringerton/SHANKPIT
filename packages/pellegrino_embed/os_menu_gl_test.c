/* os_menu_gl_test.c -- the lobby's actual OS-menu GL path (os_menu_gl.h) under a real GL context
 * (run under Xvfb + Mesa software GL). Draws the GAMES page the way the lobby does, reads the framebuffer back,
 * checks tile colours at the tile positions and that osm_hit maps a click to the right index. */
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "os_menu_gl.h"

static int fails;
#define CHECK(c, m) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", m); fails++; } else printf("ok: %s\n", m); } while (0)

static void px_at(const unsigned char *fb, int w, int x, int y_gl, int *r, int *g, int *b) {
    const unsigned char *p = fb + ((size_t)y_gl * w + x) * 4;
    *r = p[0]; *g = p[1]; *b = p[2];
}

int main(int argc, char **argv) {
    const int W = 1280, H = 720;
    const char *labels[] = { "SURVIVAL", "STORY", "CAVE-001", "SOLO", "TRAIN", "CUSTOMIZE" };
    SDL_Window *win;
    SDL_GLContext ctx;
    unsigned char *fb;
    int r, g, b;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 2; }
    win = SDL_CreateWindow("osm", 0, 0, W, H, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!win) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 2; }
    ctx = SDL_GL_CreateContext(win);
    if (!ctx) { fprintf(stderr, "GL ctx: %s\n", SDL_GetError()); return 2; }
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, W, 0, H, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.02f, 0.02f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    CHECK(osm_ready() == 1, "osm_ready (surface created)");
    osm_draw(labels, 6, 1);
    fb = (unsigned char *)malloc((size_t)W * H * 4);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, fb);
    /* tile 0 (green #1abf33) sits at panel (15..360, 30..80) top-left origin -> GL y = TOP - y */
    px_at(fb, W, (int)OSM_X + 300, (int)OSM_TOP - 40, &r, &g, &b);
    printf("tile0 px = %d,%d,%d\n", r, g, b);
    CHECK(abs(r - 0x1a) < 6 && abs(g - 0xbf) < 6 && abs(b - 0x33) < 6, "tile 0 is green at its screen position (texture upright, BGRA ok)");
    px_at(fb, W, (int)OSM_X + 700, (int)OSM_TOP - 40, &r, &g, &b);
    printf("tile1 px = %d,%d,%d\n", r, g, b);
    CHECK(abs(r - 0x1a) < 6 && abs(g - 0x73) < 6 && abs(b - 0xf2) < 6, "tile 1 is blue in the right column");
    px_at(fb, W, 20, 20, &r, &g, &b);
    CHECK(r < 20 && g < 20 && b < 30, "outside the panel stays the lobby clear colour");
    CHECK(osm_hit(OSM_X + 40, OSM_TOP - 50, 6) == 0, "click on tile 0 -> index 0");
    CHECK(osm_hit(OSM_X + 420, OSM_TOP - 50, 6) == 1, "click on tile 1 -> index 1");
    CHECK(osm_hit(OSM_X + 40, OSM_TOP - 172, 6) == 4, "click on tile 4 -> index 4");
    CHECK(osm_hit(20, 20, 6) == -1, "click outside the panel -> -1");
    if (argc > 1) {
        FILE *f = fopen(argv[1], "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", W, H);
            for (int y = H - 1; y >= 0; y--) for (int x = 0; x < W; x++) { const unsigned char *p = fb + ((size_t)y * W + x) * 4; fputc(p[0], f); fputc(p[1], f); fputc(p[2], f); }
            fclose(f);
        }
    }
    printf("%s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
    free(fb);
    return fails ? 1 : 0;
}
