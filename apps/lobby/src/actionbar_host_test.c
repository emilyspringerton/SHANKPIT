/* actionbar_host_test.c -- headless checks for the lobby's action-bar host glue (actionbar_host.c) over the real
 * PARENA-generated rules. GL/draw_string are stubbed; the SDL headers supply only scancode constants. */
#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include "actionbar_host.h"
#include "../../../packages/simulation/action_bar_rules.h"

static int g_rects = 0, g_texts = 0;
static char g_last_text[64];
void GLAPIENTRY glEnable(GLenum a) { (void)a; }
void GLAPIENTRY glColor4f(GLfloat a, GLfloat b, GLfloat c, GLfloat d) { (void)a; (void)b; (void)c; (void)d; }
void GLAPIENTRY glColor3f(GLfloat a, GLfloat b, GLfloat c) { (void)a; (void)b; (void)c; }
void GLAPIENTRY glRectf(GLfloat a, GLfloat b, GLfloat c, GLfloat d) { (void)a; (void)b; (void)c; (void)d; g_rects++; }
void GLAPIENTRY glBegin(GLenum a) { (void)a; }
void GLAPIENTRY glEnd(void) {}
void GLAPIENTRY glVertex2f(GLfloat a, GLfloat b) { (void)a; (void)b; }
void draw_string(const char *s, float x, float y, float z) { (void)x; (void)y; (void)z; g_texts++; snprintf(g_last_text, sizeof g_last_text, "%s", s); }

static int fails = 0, checks = 0;
#define CHECK(c, m) do { checks++; if (!(c)) { fails++; printf("FAIL: %s\n", m); } } while (0)

static unsigned char keys[512];
static int s_bind_claim_bank = 0, s_bind_claim_slot = 0;
static int bindfn(int bank, int slot) { return (bank == s_bind_claim_bank && slot == s_bind_claim_slot) ? ab_pack(AB_KIND_MOD, 65535) : 0; }
static int s_activated = -1;
static int prov(int bank, int slot) { return (bank == 3 && slot == 2) ? ab_pack(AB_KIND_MOD, 7) : 0; }
static const char *plab(int id) { return id == 7 ? "SPELL" : NULL; }
static void pact(int id) { s_activated = id; }

static int press(int sc, int sh, int ct, int al, int *req) {
    int r;
    memset(keys, 0, sizeof keys); keys[sc] = 1;
    r = abh_poll(keys, sh, ct, al, req);
    return r;
}

int main(void) {
    int req = -99, d, r;
    /* default bank 1 == the legacy hard-coded block, digit by digit */
    static const int legacy[9] = {0, 1, 2, 3, 4, 5, 7, 8, 9};
    for (d = 1; d <= 9; d++) {
        req = -99; r = press(SDL_SCANCODE_1 + d - 1, 0, 0, 0, &req);
        CHECK(r == 1 && req == legacy[d - 1], "default key 1..9 matches the legacy weapon binding");
    }
    req = -99; r = press(SDL_SCANCODE_0, 0, 0, 0, &req);
    CHECK(r == 0 && req == -99, "key 0 is empty by default (no weapon request)");
    req = -99; r = press(SDL_SCANCODE_1, 1, 0, 0, &req);
    CHECK(r == 0 && req == -99, "Shift+1 empty with no plugin (banks 2-4 default empty)");
    /* no keys -> nothing */
    memset(keys, 0, sizeof keys); req = -99;
    CHECK(abh_poll(keys, 0, 0, 0, &req) == 0 && req == -99, "no key, no request");

    /* plugin: contributes + labelled + activated; example-style precedence */
    CHECK(abh_register_plugin(prov, plab, pact) == 1, "register plugin");
    CHECK(abh_register_plugin(NULL, NULL, NULL) == 0, "null provider refused");
    req = -99; r = press(SDL_SCANCODE_2, 0, 1, 0, &req);
    CHECK(r == 0 && req == -99 && s_activated == 7, "Ctrl+2 runs the plugin's activate, no weapon request");
    CHECK(strcmp(abh_label(abh_resolve(3, 2)), "MOD") == 0, "generic label for kind 4");
    /* plugin cannot override a default weapon key in bank 1? plugin > default per the mod */
    CHECK(ab_kind_of(abh_resolve(1, 1)) == AB_KIND_WEAPON, "bank 1 slot 1 still the default weapon");

    /* player bind > plugin: bind claims Ctrl+2 */
    abh_set_bind_fn(bindfn); s_bind_claim_bank = 3; s_bind_claim_slot = 2; s_activated = -1;
    req = -99; r = press(SDL_SCANCODE_2, 0, 1, 0, &req);
    CHECK(r == 0 && s_activated == -1, "a player bind beats the plugin: plugin NOT activated");
    CHECK(strcmp(abh_label(abh_resolve(3, 2)), "MACRO") == 0, "bound slot is shown as MACRO");
    /* a bind on bank 1 slot 1 suppresses the default weapon (the macro runs from keydown) */
    s_bind_claim_bank = 1; s_bind_claim_slot = 1; req = -99;
    r = press(SDL_SCANCODE_1, 0, 0, 0, &req);
    CHECK(r == 0 && req == -99, "a player bind on key 1 removes the default weapon request");
    abh_set_bind_fn(NULL);

    /* modifier precedence: Alt > Ctrl > Shift */
    CHECK(ab_bank_for_mods(1, 1, 0) == 3 && ab_bank_for_mods(0, 1, 1) == 4, "mod precedence via mod");

    /* scroll: cycles bank 1 over non-empty slots, wraps, starts from the current weapon */
    req = -99; r = abh_scroll(1, 0, &req);
    CHECK(r == 1 && req == 1, "scroll +1 from knife -> magnum");
    req = -99; r = abh_scroll(-1, 0, &req);
    CHECK(r == 1 && req == 9, "scroll -1 from knife wraps over the empty slot 10 to spray can");
    req = -99; r = abh_scroll(1, 9, &req);
    CHECK(r == 1 && req == 0, "scroll +1 from spray can wraps (skipping empty slot 10) to knife");

    /* visibility + click + draw */
    abh_set_visible(0);
    g_rects = 0; abh_draw(0, 0, 0, 1, NULL, 0);
    CHECK(g_rects == 0 && !abh_visible(), "hidden bar draws nothing");
    req = -99;
    CHECK(abh_click(640.0f, 30.0f, 0, 0, 0, &req) == 0 && req == -99, "hidden bar ignores clicks");
    abh_set_visible(1);
    g_rects = 0; abh_draw(0, 0, 0, 1, NULL, 0);
    CHECK(g_rects == 10, "visible bar draws 10 slots");
    CHECK(abh_hit(1.0f, 30.0f) == 0 && abh_hit(640.0f, 400.0f) == 0, "hit test misses outside the strip");
    d = abh_hit(382.0f + 1.0f, 30.0f);
    CHECK(d == 1, "left edge of the strip is slot 1");
    CHECK(abh_hit(1280.0f - 382.0f - 1.0f, 30.0f) == 10, "right edge is slot 10");
    req = -99;
    CHECK(abh_click(382.0f + 52.0f + 2.0f, 30.0f, 0, 0, 0, &req) == 1 && req == 1, "clicking slot 2 requests the magnum");
    req = -99;
    CHECK(abh_click(1280.0f - 382.0f - 1.0f, 30.0f, 0, 0, 0, &req) == 1 && req == -99, "clicking empty slot 10 is consumed, no request");

    printf("actionbar_host: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
