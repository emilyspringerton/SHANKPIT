/* editor_widget_stub.c -- EDITOR.GAME's own widget API, for a build that does not have it.
 *
 * The real panel is linked from a sibling checkout (../EDITOR.GAME, see the Makefile's own
 * EDITOR_GAME_* block). That checkout is optional, but the link rule used to be unconditional:
 * `make lobby` on a clean box died on a missing gen/editor_stdlib_gen.c, so the client could not be
 * produced -- and therefore could not be RUN -- on any box without it. This file exists so the
 * dependency is a feature switch rather than a build wall: with the sibling present the real five
 * objects are linked and NOTES works; without it these fifteen symbols are linked instead and the
 * app reports itself unavailable.
 *
 * Deliberately not a fake editor: editor_widget_create returns 0, which is the same answer the real
 * widget gives when it genuinely cannot open its own hidden window, and editor_widget_bridge.c
 * already handles that answer ("panel disabled"). So the lobby's top menu keeps its NOTES tile, the
 * tile does nothing, and nothing pretends to have saved a note. There is no half-working panel here.
 */
#include <stdio.h>
#include <stddef.h>

static int g_stub_reported = 0;

int editor_widget_create(const char *path_arg, const char *argv0, int hidden) {
    (void)path_arg; (void)argv0; (void)hidden;
    if (!g_stub_reported) {
        g_stub_reported = 1;
        fprintf(stderr, "editor_widget_bridge: built without ../EDITOR.GAME -- NOTES is unavailable in this build\n");
    }
    return 0;
}

void editor_widget_begin_frame(void) {}
void editor_widget_set_key(int key) { (void)key; }
void editor_widget_set_mouse_pos(int x, int y) { (void)x; (void)y; }
void editor_widget_set_wheel_delta(int delta) { (void)delta; }
void editor_widget_set_text(const char *text) { (void)text; }
void editor_widget_inject(int code) { (void)code; }
void editor_widget_render_frame(void) {}
void editor_widget_capture_rgba(unsigned char *out_rgba) { (void)out_rgba; }
void editor_widget_present(void) {}
void editor_widget_tick_autosave(double dt_seconds) { (void)dt_seconds; }
int editor_widget_should_close(void) { return 0; }
void editor_widget_reset_close_flag(void) {}
int editor_widget_width(void) { return 0; }
int editor_widget_height(void) { return 0; }
