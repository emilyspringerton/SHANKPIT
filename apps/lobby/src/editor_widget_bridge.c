/* editor_widget_bridge.c -- see editor_widget_bridge.h for the real,
 * full reasoning and the real, honest "not verified end to end" note.
 * This file's own header comments below stay narrow: the specific
 * mechanics each function performs. */
#include "editor_widget_bridge.h"
#include <SDL2/SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Real extern declarations for EDITOR.GAME's own examples/editor_main.c
 * widget API (see that repo's own header comments for each). Declared
 * here rather than via a shared header across repos because EDITOR.GAME
 * has no emitted header of its own for these (same real reason its
 * own generated PARENA structs don't have one yet, see that repo's
 * Makefile comment) -- matching this exact "the caller declares its
 * own extern prototypes for another repo's C functions" pattern this
 * monorepo already uses elsewhere for cross-repo native glue. */
extern int editor_widget_create(const char *path_arg, const char *argv0, int hidden);
extern void editor_widget_begin_frame(void);
extern void editor_widget_set_key(int key);
extern void editor_widget_set_mouse_pos(int x, int y);
extern void editor_widget_set_wheel_delta(int delta);
extern void editor_widget_set_text(const char *text);
extern void editor_widget_inject(int code);
extern void editor_widget_render_frame(void);
extern void editor_widget_capture_rgba(unsigned char *out_rgba);
extern void editor_widget_present(void);
extern void editor_widget_tick_autosave(double dt_seconds);
extern int editor_widget_should_close(void);
extern void editor_widget_reset_close_flag(void);
extern int editor_widget_width(void);
extern int editor_widget_height(void);

/* Real, fixed panel geometry in the SAME 1280x720 logical ortho space
 * setup_lobby_2d() already establishes for the rest of the lobby UI
 * (gluOrtho2D(0,1280,0,720)) -- a real, fixed on-screen rect, not the
 * widget's own native 1300x500, so the top menu (title/page-toggle/
 * app-launch-status/buttons) stays visible and clickable around it,
 * per the founder's own "both screens the notes will stay open" ask. */
#define EDITOR_PANEL_X 20.0f
#define EDITOR_PANEL_Y 60.0f
#define EDITOR_PANEL_W 1000.0f
#define EDITOR_PANEL_H 460.0f

static int g_created = 0;
static int g_open = 0;
static GLuint g_tex = 0;
static unsigned char *g_rgba = NULL;
static int g_widget_w = 0, g_widget_h = 0;

/* remap_mouse -- declared static in main.c (not exported); this file
 * duplicates its exact real math rather than exporting it, matching
 * this codebase's own "small, self-contained bridge file" convention
 * elsewhere rather than widening main.c's own surface for one caller.
 * VIRTUAL_W/VIRTUAL_H are main.c's own #defines (1280/720, matching
 * the ortho space above) -- redefined here identically since this is
 * a separate translation unit. */
#ifndef VIRTUAL_W
#define VIRTUAL_W 1280
#endif
#ifndef VIRTUAL_H
#define VIRTUAL_H 720
#endif

static void editor_widget_ensure_created(void) {
    if (g_created) return;
    g_created = editor_widget_create("shankpit_os_notes.prn", "shankpit-lobby", 1 /* hidden */);
    if (!g_created) {
        fprintf(stderr, "editor_widget_bridge: editor_widget_create failed -- panel disabled\n");
        return;
    }
    g_widget_w = editor_widget_width();
    g_widget_h = editor_widget_height();
    g_rgba = (unsigned char *)malloc((size_t)g_widget_w * (size_t)g_widget_h * 4);
    glGenTextures(1, &g_tex);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
}

void editor_widget_overlay_open(void) {
    editor_widget_ensure_created();
    if (!g_created) return;
    g_open = 1;
    /* Same real SDL_StartTextInput()/SDL_StopTextInput() convention
     * lobby_start_edit/lobby_end_edit already use in main.c for their
     * own text fields -- SDL only delivers SDL_TEXTINPUT events while
     * this is active. */
    SDL_StartTextInput();
}

void editor_widget_overlay_close(void) {
    g_open = 0;
    SDL_StopTextInput();
}

int editor_widget_overlay_is_open(void) {
    return g_open && g_created;
}

int editor_widget_overlay_handle_sdl_event(const SDL_Event *e) {
    if (!editor_widget_overlay_is_open()) return 0;

    /* Real screen -> logical-1280x720 -> panel-local -> widget-local
     * coordinate chain for every mouse event; keyboard/text is always
     * exclusively the panel's while open (same modal convention
     * skin_menu_open/level_select_open already use). */
    switch (e->type) {
        case SDL_KEYDOWN:
            if (e->key.keysym.sym == SDLK_ESCAPE) {
                /* Escape closes the PANEL (hides it, keeps the file's
                 * own state/undo history intact for next time) --
                 * deliberately NOT forwarded into the widget's own
                 * Escape handling (which would instead flip its
                 * internal `running` flag, the same one main()'s own
                 * standalone loop uses to end the whole process --
                 * wrong semantics for an embedded, reopenable panel). */
                editor_widget_overlay_close();
                return 1;
            }
            editor_widget_set_key((int)e->key.keysym.sym);
            editor_widget_inject(2);
            return 1;
        case SDL_TEXTINPUT:
            editor_widget_set_text(e->text.text);
            editor_widget_inject(3);
            return 1;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEMOTION: {
            int sx = (e->type == SDL_MOUSEMOTION) ? e->motion.x : e->button.x;
            int sy = (e->type == SDL_MOUSEMOTION) ? e->motion.y : e->button.y;
            /* logical 1280x720, bottom-left origin -- matches
             * setup_lobby_2d()'s own gluOrtho2D(0,1280,0,720) and
             * remap_mouse()'s own real Y-flip in main.c; recomputed
             * here directly from raw window pixels since g_win_w/h and
             * the viewport globals are main.c's own statics, not
             * exported -- see this file's own VIRTUAL_W/H note above
             * for why the same tradeoff already applies there. SDL's
             * own SDL_GetWindowSize/renderer viewport aren't threaded
             * through here; this assumes the common case (viewport
             * fills the window, no letterboxing) -- a real, honest,
             * narrower approximation of remap_mouse's own full
             * viewport-aware math, good enough for a fixed-size panel
             * hit-test, worth revisiting if the panel ever misaligns
             * on a real letterboxed/ultrawide window. */
            int win_w, win_h;
            SDL_GetWindowSize(SDL_GetMouseFocus(), &win_w, &win_h);
            if (win_w <= 0 || win_h <= 0) { win_w = VIRTUAL_W; win_h = VIRTUAL_H; }
            float lx = (float)sx / (float)win_w * VIRTUAL_W;
            float ly = VIRTUAL_H - ((float)sy / (float)win_h * VIRTUAL_H);

            if (lx < EDITOR_PANEL_X || lx >= EDITOR_PANEL_X + EDITOR_PANEL_W
                || ly < EDITOR_PANEL_Y || ly >= EDITOR_PANEL_Y + EDITOR_PANEL_H) {
                /* Outside the panel -- let lobby's own top menu handle
                 * it (the founder's own "both screens stay open"
                 * ask), except while the panel visually covers that
                 * spot the click still just falls through as a miss,
                 * same as any other empty area of the lobby screen. */
                return 0;
            }
            float px = (lx - EDITOR_PANEL_X) / EDITOR_PANEL_W;
            /* Panel is drawn with its own local (0,0) at the TOP,
             * matching the widget's own screen-space render convention
             * (text drawn downward from y=12); the logical space above
             * is bottom-left-origin, so flip here. */
            float py = 1.0f - (ly - EDITOR_PANEL_Y) / EDITOR_PANEL_H;
            int wx = (int)(px * g_widget_w);
            int wy = (int)(py * g_widget_h);
            editor_widget_set_mouse_pos(wx, wy);
            editor_widget_inject(e->type == SDL_MOUSEBUTTONDOWN ? 5
                                : e->type == SDL_MOUSEBUTTONUP ? 6
                                : 7);
            return 1;
        }
        case SDL_MOUSEWHEEL:
            editor_widget_set_wheel_delta(e->wheel.y);
            editor_widget_inject(9);
            return 1;
        default:
            return 0;
    }
}

void editor_widget_overlay_tick_and_draw(double dt_seconds) {
    if (!editor_widget_overlay_is_open()) return;

    editor_widget_begin_frame();
    editor_widget_render_frame();
    editor_widget_capture_rgba(g_rgba);
    editor_widget_present();
    editor_widget_tick_autosave(dt_seconds);

    if (editor_widget_should_close()) {
        /* The widget's own internal Escape/Quit handling fired (real,
         * unchanged editor behavior) -- treat it the same as our own
         * overlay Escape: hide the panel, reset the flag so the SAME
         * underlying widget (buffer, undo history, open file) can be
         * reopened later rather than needing to be torn down and
         * recreated. */
        editor_widget_reset_close_flag();
        editor_widget_overlay_close();
        return;
    }

    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_widget_w, g_widget_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_rgba);
    glEnable(GL_TEXTURE_2D);
    glColor3f(1, 1, 1);
    glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex2f(EDITOR_PANEL_X, EDITOR_PANEL_Y + EDITOR_PANEL_H);
        glTexCoord2f(1, 0); glVertex2f(EDITOR_PANEL_X + EDITOR_PANEL_W, EDITOR_PANEL_Y + EDITOR_PANEL_H);
        glTexCoord2f(1, 1); glVertex2f(EDITOR_PANEL_X + EDITOR_PANEL_W, EDITOR_PANEL_Y);
        glTexCoord2f(0, 1); glVertex2f(EDITOR_PANEL_X, EDITOR_PANEL_Y);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}
