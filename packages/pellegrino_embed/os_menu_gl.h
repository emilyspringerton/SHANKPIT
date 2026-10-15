/* os_menu_gl.h -- GL glue for the PELLEGRINO-painted OS menu (kanban #627). Header-only, static; the includer
 * must already have <SDL2/SDL_opengl.h> (or GL/gl.h) and <stdio.h>/<stdlib.h>/<string.h>. Used by
 * apps/lobby/src/main.c and by packages/pellegrino_embed/os_menu_gl_test.c (a real GL context under Xvfb). */
#ifndef SHANKPIT_OS_MENU_GL_H
#define SHANKPIT_OS_MENU_GL_H
#include "os_menu_embed.h"

#define OSM_X 100.0f
#define OSM_TOP 560.0f
#define OSM_W 760
#define OSM_H 480
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
static int g_osm_state = -1;            /* -1 untried, 0 off, 1 ready */
static GLuint g_osm_tex = 0;
static int g_osm_have_tex = 0;
static int g_osm_force_upload = 1;

/* Lazily creates the offscreen PELLEGRINO surface. SHANKPIT_OS_MENU=classic|0 forces the old grid. */
static int osm_ready(void) {
    if (g_osm_state < 0) {
        const char *e = getenv("SHANKPIT_OS_MENU");
        if (e && (strcmp(e, "classic") == 0 || strcmp(e, "0") == 0)) {
            g_osm_state = 0;
        } else {
            g_osm_state = (os_menu_init(OSM_W, OSM_H) == 0) ? 1 : 0;
            if (!g_osm_state) fprintf(stderr, "[os-menu] PELLEGRINO surface unavailable -- classic grid\n");
        }
    }
    return g_osm_state == 1;
}

/* Draw the n tiles (2 columns, row-major, `selected` highlighted) at OSM_X/OSM_TOP in a bottom-left-origin
 * 2D ortho. Repaints + re-uploads only when the generated HTML changed. */
static void osm_draw(const char *const *labels, int n, int selected) {
    static char page[OS_MENU_PAGE_MAX];
    const unsigned int *px;
    if (n > 24) n = 24;
    if (os_menu_build_page(page, (int)sizeof page, labels, n, (selected >= 0 && selected < n) ? selected : -1) < 0) return;
    if (os_menu_set_page(page)) g_osm_force_upload = 1;
    px = os_menu_draw();
    if (!px) return;
    if (!g_osm_tex) glGenTextures(1, &g_osm_tex);
    glBindTexture(GL_TEXTURE_2D, g_osm_tex);
    if (g_osm_force_upload) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, OSM_W, OSM_H, 0, GL_BGRA, GL_UNSIGNED_BYTE, px);
        g_osm_force_upload = 0;
        g_osm_have_tex = 1;
    }
    if (!g_osm_have_tex) return;
    glEnable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(OSM_X, OSM_TOP);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(OSM_X + OSM_W, OSM_TOP);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(OSM_X + OSM_W, OSM_TOP - OSM_H);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(OSM_X, OSM_TOP - OSM_H);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/* click in bottom-left-origin virtual space -> tile index, or -1 */
static int osm_hit(float mx, float my, int menu_count) {
    char href[96];
    int idx;
    if (mx < OSM_X || mx >= OSM_X + OSM_W || my > OSM_TOP || my <= OSM_TOP - OSM_H) return -1;
    if (!os_menu_click((int)(mx - OSM_X), (int)(OSM_TOP - my))) return -1;
    if (!os_menu_action(href, (int)sizeof href)) return -1;
    idx = os_menu_parse_action(href);
    return (idx >= 0 && idx < menu_count) ? idx : -1;
}
#endif
