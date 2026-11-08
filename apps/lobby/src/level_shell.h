/* apps/lobby/src/level_shell.h -- the lobby's level shell: SDL events in, one GL quad out (T0 step 4,
 * docs/T0_LEVEL_WASM_CONTRACT.md). Founder real-time 2026-11-08: "Boot the SHANKPIT world plus BRAWLPIT as WASM,
 * BRAWLPIT in the foreground. Shift+M toggles to the world view and pauses BRAWLPIT."
 *
 * Everything that happens to the level -- load, the Shift+M / Shift+X chords, the pause on hide, close, reload,
 * traps, the fixed step, the lifecycle rows in REFLUX -- is packages/wasm_host/level_host (no SDL, tested
 * headless), and every decision in it is the PARENA level manager. This file only turns SDL events into
 * level_host calls, uploads the framebuffer to a texture, and draws it, or the world-view hint.
 *
 * main.c calls, in game states only (the lobby menu never shows a level):
 *   level_shell_event()  first thing in the game-state event branch: 1 = consumed, skip the world's handling
 *   level_shell_frame()  once per frame, before the world reads its own input
 *   level_shell_draw()   last thing before SDL_GL_SwapWindow: the level over the world, or the banner
 *   level_shell_world_input() the world reads keyboard, mouse and pad and grabs the mouse only while 1
 * Ctrl+Shift+J (Cmd+Option+J on macOS) opens the level's PARENA source in EDITOR.GAME's overlay over the world
 * (dev boxes: it needs ../PARENA); Escape closes it, back into the level. */
#ifndef SHANKPIT_LEVEL_SHELL_H
#define SHANKPIT_LEVEL_SHELL_H

#include <SDL2/SDL.h>

/* the world's letterboxed 1280x720 viewport (main.c g_vp_* / g_win_*), for pointer mapping */
typedef struct {
    int vp_x, vp_y, vp_w, vp_h;
    int win_w, win_h;
} level_shell_view;

/* boot_level = 1: BRAWLPIT loads on the first game frame, in the foreground (the T0 default);
 * 0: nothing loaded until Shift+M (--no-level). */
void level_shell_init(int boot_level, unsigned int seed);

/* text_focus = chat, the orb, a phone text app, a lobby text field or the editor overlay has the keyboard. */
int level_shell_event(const SDL_Event *e, int text_focus, const level_shell_view *v);

void level_shell_frame(double frame_seconds);
void level_shell_draw(void);
int level_shell_owns_input(void);
/* 1 = the world reads its own keys, mouse and pad and may grab the mouse: nothing (no level, no source
 * overlay) is in front of it (PARENA lm-world-input). main.c gates every polled input and mouse grab on this. */
int level_shell_world_input(void);
int level_shell_rejected_dispatches(void);  /* the level's dispatches the REFLUX bridge refused */
void level_shell_shutdown(void);

#endif /* SHANKPIT_LEVEL_SHELL_H */
