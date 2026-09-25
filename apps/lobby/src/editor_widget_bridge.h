/* editor_widget_bridge.h -- SHANKPIT OS's own single-window compositor
 * integration for EDITOR.GAME's real PARENA editor (2026-09-25, founder
 * real-time: "the editor app fails to launch -- instead of having it
 * launch it should pop a widget up on the screen that is open while the
 * os screen is open ... dont spawn a separate process deeply integrate
 * it as a widget on the screen the notes auto save").
 *
 * EDITOR.GAME's own examples/editor_main.c (see that repo's own
 * EDITOR_WIDGET_TEST_BUILD-guarded split) exposes create/dispatch_event/
 * render_frame/capture_rgba/present/tick_autosave/shutdown as plain C
 * functions operating on its own real, HIDDEN SDL2 window+renderer --
 * never a second on-screen window, never a fork()/exec()'d process.
 * This bridge owns the SHANKPIT-side half: an OpenGL texture holding
 * that widget's captured RGBA frame, drawn as a plain textured quad
 * panel inside lobby's OWN single window (the same real "overlay drawn
 * on top of the lobby's own 2D scene" pattern draw_skin_chooser_overlay/
 * draw_level_select_overlay already establish in apps/lobby/src/
 * main.c), and real coordinate translation from lobby's own captured
 * SDL_Event stream into the widget's local 1300x500 space.
 *
 * Real, honest, NOT verified end to end (2026-09-25): this was built
 * and reasoned through carefully, and EDITOR.GAME's own widget API was
 * compiled AND run headlessly (Xvfb) with real assertions (non-blank
 * captured pixels, event injection, autosave, quit handling all
 * passing -- see EDITOR.GAME/examples/editor_widget_test.c). This
 * bridge's own GL texture-upload/quad-draw code and the cross-repo
 * Bazel wiring (SHANKPIT/MODULE.bazel's own local_path_override onto
 * ../EDITOR.GAME) could NOT be built or visually verified here -- no
 * `bazel` binary exists in this sandbox, and there is no display to
 * look at the panel even if there were. Build and eyeball this on a
 * real machine before calling it finished. */
#ifndef EDITOR_WIDGET_BRIDGE_H
#define EDITOR_WIDGET_BRIDGE_H

#include <SDL2/SDL.h>

/* Opens (lazily creating the underlying hidden editor_widget the first
 * time) or, if already open, brings the panel back to front -- called
 * from lobby_launch_app's own APP_EDITOR_GAME case instead of the old
 * fork()+execl() branch every other app entry still uses. The widget's
 * own state (buffer, undo/redo, scroll position, open file) persists
 * across close/reopen within the same SHANKPIT OS process -- closing
 * the panel is NOT the same as quitting the editor. */
void editor_widget_overlay_open(void);

/* Hides the panel (does not destroy the underlying widget/its state --
 * see editor_widget_overlay_open's own header comment). Safe to call
 * whether or not the panel is currently open. */
void editor_widget_overlay_close(void);

int editor_widget_overlay_is_open(void);

/* Called once per lobby frame, from the SAME single SDL_PollEvent loop
 * apps/lobby/src/main.c already runs -- this is the one and only place
 * that pump runs; there is no second, competing SDL_PollEvent anywhere
 * (the widget's own hidden window never polls the real OS event queue
 * itself, see EDITOR.GAME's own editor_widget_inject header comment).
 * Returns 1 if this event was consumed by the panel (lobby's own event
 * handling should `continue`, skipping its normal processing for this
 * event) or 0 if the panel didn't want it (e.g. a click outside the
 * panel rect while the panel is open -- the TOP MENU stays clickable
 * even with the editor open, per the founder's own "both screens the
 * notes will stay open" ask). Keyboard/text input is captured
 * EXCLUSIVELY by the panel while it's open (same modal convention
 * skin_menu_open/level_select_open already use in this same file) --
 * only mouse events are rect-tested against the panel's own screen
 * position. */
int editor_widget_overlay_handle_sdl_event(const SDL_Event *e);

/* Called once per lobby frame, only while the panel is open, from the
 * SAME place draw_skin_chooser_overlay()/draw_level_select_overlay()
 * are already called (apps/lobby/src/main.c's own STATE_LOBBY render
 * block, after setup_lobby_2d()/draw_lobby_buttons(), before
 * SDL_GL_SwapWindow) -- ticks the widget's own frame (event dispatch
 * already happened via handle_sdl_event above, so this drives render +
 * pixel capture + periodic auto-save) and draws the captured frame as
 * a plain textured GL quad panel, in the SAME single window/GL context
 * lobby's own 2D scene just drew into -- this widget's own hidden
 * SDL_Renderer/window never touch this GL context directly, only the
 * flat RGBA byte buffer crosses that boundary (see EDITOR.GAME's own
 * editor_widget_capture_rgba header comment for why: no raw GL calls
 * ever run against the widget's own renderer, and no SDL_Renderer GL
 * state ever runs against lobby's own context, so there is no GL
 * state to conflict between the two). `dt_seconds` drives the
 * widget's own periodic auto-save timer. */
void editor_widget_overlay_tick_and_draw(double dt_seconds);

#endif
