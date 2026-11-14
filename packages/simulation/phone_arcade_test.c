/* phone_arcade_test.c -- the SHANKPHONE's PELLEGRINO games app, headless, with the REAL BRAWLPIT module on the real
 * wasm3 host. Run from the repository root (the module and its PNGs are relative paths). Build: make test-phone-arcade */
#include "phone_arcade.h"
#include <stdio.h>

static int fails = 0, passes = 0;
#define CHECK(c, msg) do { if (c) passes++; else { fails++; printf("FAIL: %s\n", msg); } } while (0)

#define K_UP 1073741906
#define K_DOWN 1073741905
#define K_ENTER 13
#define K_BACK 8

int main(void) {
    static PhoneArcade a;
    arcade_init(&a);

    /* the mod */
    CHECK(arcade_game_count() == 5, "the mod lists five games");
    CHECK(strcmp(arcade_row_name(0), "BRAWLPIT") == 0 && arcade_game_kind(0) == 1, "BRAWLPIT is a level module");
    CHECK(arcade_game_kind(1) == 2 && strncmp(arcade_game_path(1), "https://", 8) == 0, "the web games are pages");
    CHECK(strcmp(arcade_row_name(arcade_game_count()), "WEB BROWSER") == 0, "the last row is the browser");

    /* the list */
    CHECK(arcade_list_key(&a, K_UP, 1) == ARCADE_DID_NOTHING && a.cursor == 0, "Up at the top stays");
    for (int i = 0; i < 20; i++) arcade_list_key(&a, K_DOWN, 1);
    CHECK(a.cursor == arcade_game_count(), "Down stops at the last row");
    CHECK(arcade_list_key(&a, K_ENTER, 1) == ARCADE_DID_BROWSER && a.screen == ARCADE_BROWSER, "Enter on WEB BROWSER opens it");
    a.screen = ARCADE_LIST;
    CHECK(arcade_list_key(&a, K_BACK, 1) == ARCADE_DID_CLOSE, "Backspace on the list leaves the app");

    /* a web game with no Chromium: said, not started */
    a.cursor = 1; a.chromium_ok = 0;
    CHECK(arcade_list_key(&a, K_ENTER, 1) == ARCADE_DID_NOTHING && a.screen == ARCADE_LIST, "no Chromium: a web game does not start");
    CHECK(strstr(a.msg, "SHANKPIT-460 is a web game") != NULL, "and the app says why");
    a.chromium_ok = 1;
    CHECK(arcade_list_key(&a, K_ENTER, 1) == ARCADE_DID_CHROMIUM && a.screen == ARCADE_CHROMIUM && a.game == 1,
          "with Chromium: the web game goes to the Chromium stopgap");
    a.screen = ARCADE_LIST;

    /* BRAWLPIT, for real */
    a.cursor = 0;
    CHECK(arcade_list_key(&a, K_ENTER, 7) == ARCADE_DID_START && a.screen == ARCADE_WASM, "Enter on BRAWLPIT starts it");
    for (int f = 0; f < 10; f++) arcade_frame(&a, 16667);
    CHECK(a.screen == ARCADE_WASM && a.msg[0] == 0, "the module loads on the phone's own host");
    CHECK(level_host_state(&a.lh) == lm_st_fg(), "and runs in the foreground");
    int w = 0, h = 0, dirty = 0;
    const uint8_t *fb = arcade_fb(&a, &w, &h, &dirty);
    CHECK(fb != NULL && w == 960 && h == 540, "it renders its 960x540 picture");
    printf("      (BRAWLPIT draws %dx%d)\n", w, h);
    int lit = 0;
    for (int i = 0; fb && i < w * h; i += 97) if (fb[i * 4] | fb[i * 4 + 1] | fb[i * 4 + 2]) lit++;
    CHECK(lit > 100, "and the picture is not blank");
    int ticks0 = a.lh.ticks;
    arcade_game_key(&a, 13, 1); arcade_game_key(&a, 13, 0);   /* Enter: the game's own menu */
    for (int f = 0; f < 30; f++) arcade_frame(&a, 16667);
    CHECK(a.lh.ticks > ticks0, "frames keep stepping it");
    CHECK(arcade_game_key(&a, K_BACK, 0) == 0 && a.screen == ARCADE_WASM, "a Backspace release alone does not leave");
    CHECK(arcade_game_key(&a, K_BACK, 1) == 1 && a.screen == ARCADE_LIST && !a.lh_live, "Backspace leaves the game, shut down");
    CHECK(arcade_fb(&a, &w, &h, &dirty) == NULL, "no picture once it is closed");

    /* a module that is not there: back to the list, said */
    arcade_start_wasm(&a, 0, 1);
    a.lh.desc.module_path = "assets/levels/none/missing.wasm";
    for (int f = 0; f < 3; f++) arcade_frame(&a, 16667);
    CHECK(a.screen == ARCADE_LIST && a.msg[0] != 0, "a module that will not load comes back to the list with the reason");
    printf("      (%s)\n", a.msg);

    /* the rules */
    CHECK(arcade_fit_w(640, 360, 300, 400) == 300 && arcade_fit_h(640, 360, 300, 400) == 168, "a wide game fits the width");
    CHECK(arcade_fit_w(360, 640, 400, 300) == 168 && arcade_fit_h(360, 640, 400, 300) == 300, "a tall one fits the height");
    CHECK(arcade_page_first(7) == 6 && arcade_per_page() == 6, "pages of six rows");

    printf("phone arcade: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
