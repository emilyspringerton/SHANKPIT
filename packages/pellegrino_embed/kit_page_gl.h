/* kit_page_gl.h -- paints one of the kit pages (packages/common/kit.h) with PELLEGRINO's own
 * layout/paint engine, through the same os_menu_embed surface the OS menu uses (kanban #627).
 *
 * Why this surface and not the in-phone browser: phone_browser_embed.h is a REAL navigator (it
 * fetches over TLS/QUIC and can only show URLs), which is the wrong tool for a locally generated
 * page. os_menu_set_page takes an HTML string, lays it out with PELLEGRINO and hands back pixels --
 * exactly what "basic html and CSS and PELLEGRINO tech for rendering the ui" asks for.
 *
 * Deliberately the SAME 760x480 dimensions as os_menu_gl.h's own surface: os_menu_init is a single
 * global surface, and matching its size means re-initialising for a kit page cannot desync the OS
 * menu's own upload geometry. Header-only, static; the includer must already have
 * <SDL2/SDL_opengl.h> and <stdio.h>. */
#ifndef SHANKPIT_KIT_PAGE_GL_H
#define SHANKPIT_KIT_PAGE_GL_H
#include "os_menu_embed.h"

#define KPG_W 760
#define KPG_H 480
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

static int g_kpg_state = -1;   /* -1 untried, 0 off, 1 ready */
static GLuint g_kpg_tex = 0;
static int g_kpg_force = 1;
static int g_kpg_have_tex = 0;

static int kpg_ready(void) {
    if (g_kpg_state < 0) {
        g_kpg_state = (os_menu_init(KPG_W, KPG_H) == 0) ? 1 : 0;
        if (!g_kpg_state) fprintf(stderr, "[kit-page] PELLEGRINO surface unavailable -- kit screens fall back to text\n");
    }
    return g_kpg_state == 1;
}

/* Draw `html` into the rect (x0,bottom)-(x0+w,bottom+h) in a bottom-left-origin 2D ortho.
 * Returns 1 if it painted, 0 if the surface is unavailable (the caller then draws its own text
 * fallback -- a phone app must never be a blank rectangle). */
static int kpg_draw(const char *html, float x0, float bottom, float w, float h) {
    const unsigned int *px;
    if (!html) return 0;
    if (!kpg_ready()) return 0;
    if (os_menu_set_page(html)) g_kpg_force = 1;
    px = os_menu_draw();
    if (!px) return 0;
    if (!g_kpg_tex) glGenTextures(1, &g_kpg_tex);
    glBindTexture(GL_TEXTURE_2D, g_kpg_tex);
    if (g_kpg_force) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, KPG_W, KPG_H, 0, GL_BGRA, GL_UNSIGNED_BYTE, px);
        g_kpg_force = 0;
        g_kpg_have_tex = 1;
    }
    if (!g_kpg_have_tex) return 0;
    glEnable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(x0, bottom);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(x0 + w, bottom);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(x0 + w, bottom + h);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(x0, bottom + h);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    return 1;
}

#endif /* SHANKPIT_KIT_PAGE_GL_H */
