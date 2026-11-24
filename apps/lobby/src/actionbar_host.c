/* actionbar_host.c -- see actionbar_host.h. All decision logic lives in the PARENA mod (action_bar_rules.c). */
#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include "actionbar_host.h"
#include "../../../packages/simulation/action_bar_rules.h"

void draw_string(const char *str, float x, float y, float size);

typedef struct { abh_provider_fn provider; abh_label_fn label; abh_activate_fn activate; } AbhPlugin;
static AbhPlugin g_plug[ABH_MAX_PLUGINS];
static int g_nplug = 0;
static abh_bind_fn g_bind = NULL;
static int g_visible = 1;   /* default ON: the founder asked for the bars; they sit in a free bottom-centre strip */

#define WPN_NAMES_N 10
static const char *const WPN_SHORT[WPN_NAMES_N] = {
    "KNIFE", "MAGN", "AR", "SHOT", "SNPR", "KATA", "MSSL", "LIGHT", "HAMR", "SPRAY"
};

int abh_register_plugin(abh_provider_fn provider, abh_label_fn label, abh_activate_fn activate) {
    if (!provider || g_nplug >= ABH_MAX_PLUGINS) return 0;
    g_plug[g_nplug].provider = provider; g_plug[g_nplug].label = label; g_plug[g_nplug].activate = activate;
    g_nplug++;
    return 1;
}
void abh_reset_plugins(void) { g_nplug = 0; }
void abh_set_bind_fn(abh_bind_fn fn) { g_bind = fn; }
void abh_set_visible(int on) { g_visible = on ? 1 : 0; }
int abh_visible(void) { return g_visible; }

/* Plugins chain in registration order through the mod's own ab_first_nonzero. */
static int plugin_action(int bank, int slot) {
    int i, a = 0;
    for (i = 0; i < g_nplug; i++) a = ab_first_nonzero(a, g_plug[i].provider(bank, slot));
    return a;
}

int abh_resolve(int bank, int slot) {
    int bind = g_bind ? g_bind(bank, slot) : 0;
    return ab_resolve(bank, slot, bind, plugin_action(bank, slot));
}

/* A kind-4 action belongs to whichever plugin claims the slot; ask each in order. */
static const AbhPlugin *owner_of(int bank, int slot, int action) {
    int i;
    for (i = 0; i < g_nplug; i++)
        if (g_plug[i].provider(bank, slot) == action) return &g_plug[i];
    return NULL;
}

const char *abh_label(int action) {
    int kind = ab_kind_of(action), id = ab_id_of(action);
    if (kind == AB_KIND_WEAPON) return (id >= 0 && id < WPN_NAMES_N) ? WPN_SHORT[id] : "WPN";
    if (kind == AB_KIND_ABILITY) return "ABIL";
    if (kind == AB_KIND_ITEM) return "ITEM";
    if (kind == AB_KIND_MOD) return id == 65535 ? "MACRO" : "MOD";
    return "";
}

static const char *label_for(int bank, int slot, int action) {
    if (ab_kind_of(action) == AB_KIND_MOD) {
        const AbhPlugin *o = owner_of(bank, slot, action);
        if (o && o->label) { const char *s = o->label(ab_id_of(action)); if (s && s[0]) return s; }
    }
    return abh_label(action);
}

/* Run an already-resolved action. Returns 1 when it is a weapon request. */
static int run_action(int bank, int slot, int action, int *wpn_req) {
    int kind = ab_kind_of(action), id = ab_id_of(action);
    if (kind == AB_KIND_WEAPON) { if (wpn_req) *wpn_req = id; return 1; }
    if (kind == AB_KIND_MOD && !(g_bind && id == 65535)) {
        const AbhPlugin *o = owner_of(bank, slot, action);
        if (o && o->activate) o->activate(id);
    }
    return 0;
}

int abh_poll(const unsigned char *keys, int shift, int ctrl, int alt, int *wpn_req) {
    /* digit d -> scancode: 1..9 are SDL_SCANCODE_1.., 0 is SDL_SCANCODE_0 (after 9) */
    int bank = ab_bank_for_mods(shift, ctrl, alt), d, got = 0;
    for (d = 1; d <= 10; d++) {
        int digit = d % 10;
        int sc = (digit == 0) ? SDL_SCANCODE_0 : SDL_SCANCODE_1 + (digit - 1);
        int slot = ab_slot_for_digit(digit);
        if (!keys[sc]) continue;
        got |= run_action(bank, slot, abh_resolve(bank, slot), wpn_req);
    }
    return got;
}

int abh_scroll(int delta, int current_weapon, int *wpn_req) {
    int slot = 1, i, step = delta < 0 ? -1 : 1;
    for (i = 1; i <= AB_SLOTS; i++) {
        int a = abh_resolve(1, i);
        if (ab_kind_of(a) == AB_KIND_WEAPON && ab_id_of(a) == current_weapon) { slot = i; break; }
    }
    for (i = 0; i < AB_SLOTS; i++) {
        int a;
        slot = ab_cycle_slot(slot, step);
        a = abh_resolve(1, slot);
        if (a != 0) return run_action(1, slot, a, wpn_req);
    }
    return 0;
}

#define BAR_SLOT_W 48.0f
#define BAR_GAP 4.0f
#define BAR_Y 10.0f
static float bar_x0(void) { return (1280.0f - (AB_SLOTS * BAR_SLOT_W + (AB_SLOTS - 1) * BAR_GAP)) * 0.5f; }

int abh_hit(float mx, float my) {
    int i;
    if (my < BAR_Y || my > BAR_Y + BAR_SLOT_W) return 0;
    for (i = 0; i < AB_SLOTS; i++) {
        float x = bar_x0() + i * (BAR_SLOT_W + BAR_GAP);
        if (mx >= x && mx <= x + BAR_SLOT_W) return i + 1;
    }
    return 0;
}

int abh_click(float mx, float my, int shift, int ctrl, int alt, int *wpn_req) {
    int slot, bank;
    if (!g_visible) return 0;
    slot = abh_hit(mx, my);
    if (!slot) return 0;
    bank = ab_bank_for_mods(shift, ctrl, alt);
    run_action(bank, slot, abh_resolve(bank, slot), wpn_req);
    return 1;   /* the click was ours even on an empty slot -- never fall through to a shot */
}

void abh_draw(int shift, int ctrl, int alt, int current_weapon, const int *ammo, int reloading) {
    int bank = ab_bank_for_mods(shift, ctrl, alt), i;
    static const char *const BANK_NAME[5] = {"", "", "SHIFT", "CTRL", "ALT"};
    if (!g_visible) return;
    glEnable(GL_BLEND);
    for (i = 1; i <= AB_SLOTS; i++) {
        float x = bar_x0() + (i - 1) * (BAR_SLOT_W + BAR_GAP), y = BAR_Y;
        int a = abh_resolve(bank, i), kind = ab_kind_of(a), id = ab_id_of(a);
        int cur = (kind == AB_KIND_WEAPON && id == current_weapon);
        const char *lab = label_for(bank, i, a);
        char num[4];
        snprintf(num, sizeof num, "%d", ab_digit_for_slot(i));
        glColor4f(cur ? 0.30f : 0.06f, cur ? 0.26f : 0.08f, cur ? 0.08f : 0.12f, 0.80f);
        glRectf(x, y, x + BAR_SLOT_W, y + BAR_SLOT_W);
        if (cur) glColor3f(1.0f, 0.88f, 0.25f); else if (a) glColor3f(0.55f, 0.72f, 0.92f); else glColor3f(0.22f, 0.26f, 0.32f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(x, y); glVertex2f(x + BAR_SLOT_W, y); glVertex2f(x + BAR_SLOT_W, y + BAR_SLOT_W); glVertex2f(x, y + BAR_SLOT_W);
        glEnd();
        glColor3f(0.75f, 0.80f, 0.88f);
        draw_string(num, x + 4.0f, y + BAR_SLOT_W - 12.0f, 2.2f);
        if (lab[0]) {
            if (a) glColor3f(cur ? 1.0f : 0.92f, cur ? 0.9f : 0.94f, cur ? 0.4f : 1.0f);
            draw_string(lab, x + 4.0f, y + 22.0f, 2.0f);
        }
        if (kind == AB_KIND_WEAPON && ammo && id >= 0 && id < WPN_NAMES_N && ammo[id] >= 0) {
            char buf[16];
            snprintf(buf, sizeof buf, "%d", ammo[id]);
            if (cur && reloading) glColor3f(0.95f, 0.45f, 0.20f); else glColor3f(1.0f, 0.9f, 0.2f);
            draw_string(buf, x + 4.0f, y + 6.0f, 2.0f);
        }
    }
    if (bank > 1) {
        glColor3f(1.0f, 0.88f, 0.25f);
        draw_string(BANK_NAME[bank], bar_x0(), BAR_Y + BAR_SLOT_W + 6.0f, 2.2f);
    }
}
