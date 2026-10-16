/* phone_browser_gl.h -- GL glue for the real embedded PELLEGRINO browser inside the phone's
 * BP_APP_PELLEGRINO panel (SHANKPIT-PELLEGRINO-APP-02). Header-only, static; same texture-upload
 * pattern packages/pellegrino_embed/os_menu_gl.h already establishes for the OS menu (bottom-left-
 * origin 2D ortho, BGRA upload, a textured quad) -- the includer must already have
 * <SDL2/SDL_opengl.h> and <stdio.h>/<stdlib.h>/<string.h>. Used by apps/lobby/src/main.c. */
#ifndef SHANKPIT_PHONE_BROWSER_GL_H
#define SHANKPIT_PHONE_BROWSER_GL_H
#include "phone_browser_embed.h"

#define PBG_W 600
#define PBG_H 460
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
static int g_pbg_state = -1;           /* -1 untried, 0 off, 1 ready */
static GLuint g_pbg_tex = 0;
static int g_pbg_have_tex = 0;

static int pbg_ready(void) {
    if (g_pbg_state < 0) {
        g_pbg_state = (phone_browser_open(PBG_W, PBG_H) == 0) ? 1 : 0;
        if (!g_pbg_state) fprintf(stderr, "[phone-browser] PELLEGRINO embed surface unavailable\n");
        else phone_browser_navigate("https://example.com/");
    }
    return g_pbg_state == 1;
}

/* Pumps one frame of the real browser (TLS/HTTP/HTTP-3 fetch + repaint) and draws it as a textured
 * quad at (x0,top) in a bottom-left-origin 2D ortho, matching osm_draw's own placement convention. */
static void pbg_draw(float x0, float top) {
    const unsigned int *px;
    if (!pbg_ready()) return;
    px = phone_browser_step(PBG_W, PBG_H);
    if (!px) return;
    if (!g_pbg_tex) glGenTextures(1, &g_pbg_tex);
    glBindTexture(GL_TEXTURE_2D, g_pbg_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, PBG_W, PBG_H, 0, GL_BGRA, GL_UNSIGNED_BYTE, px);
    g_pbg_have_tex = 1;
    glEnable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(x0, top);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(x0 + PBG_W, top);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(x0 + PBG_W, top - PBG_H);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(x0, top - PBG_H);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/* click in bottom-left-origin virtual space, same panel placement pbg_draw used -> forwards to the
 * real browser if inside the panel, returns 1 if it landed inside. */
static int pbg_click(float x0, float top, float mx, float my) {
    if (mx < x0 || mx >= x0 + PBG_W || my > top || my <= top - PBG_H) return 0;
    phone_browser_click((int)(mx - x0), (int)(top - my));
    return 1;
}
#endif
