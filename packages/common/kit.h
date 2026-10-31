/* kit.h -- the player's KIT: which gadget sits on which key, which prefab kit is live, and the
 * three HTML/CSS pages the phone renders for it.
 *
 * Founder 2026-10-31: "we need to add kit affordances for these items have it be so that you can
 * ` and - and = so 3 additional gadgets in additon to 1-9 or -0 that we already have if we dont
 * have 0 yet allow us 4 additional gadgets and we can also set f and c i suppose so thats a total
 * of 5 or 6 gadgets so that maps to what the affordances in the fphone can help us support for now
 * give us 2 apps one to map the current kit to keys and another app to select the kit from the kits
 * menenu (we can select a kit that is already pre built) and then another app for building kits
 * keep it simple while including all the affordances use basic html and CSS and PELLEGRINO (clone
 * it down) tech for rendering the ui - oh wait f is already taken for vehicle entry thats fine we
 * can dont need f same spec"
 *
 * So: the ten number keys PLUS four gadget keys -- ` (backtick), - (minus), = (equals) and C.
 * F is deliberately NOT one of them (it already enters vehicles). That is 14 bind slots.
 *
 * Items are not a new inventory: each one is an input this engine ALREADY acts on, so a kit is a
 * key -> real-action map rather than a parallel universe of fake gadgets. That is the same
 * "compose what exists" discipline the BIG_O merge work used instead of standing up another
 * standalone system.
 *
 * Pure C99, no GL/SDL/network: the pages are strings (rendered by PELLEGRINO's own layout/paint
 * engine through packages/pellegrino_embed), the model is plain data, and every decision here is
 * unit-tested (packages/common/kit_test.c). */
#ifndef SHANKPIT_KIT_H
#define SHANKPIT_KIT_H

#include <stdio.h>
#include <string.h>

#define KIT_SLOTS 14        /* 1..9, 0, `, -, =, C */
#define KIT_MAX_KITS 6
#define KIT_NAME_LEN 24

typedef enum {
    KIT_ITEM_NONE = 0,
    KIT_ITEM_PHEROMONE,   /* in_pheromone / BTN_PHEROMONE -- the canister throw */
    KIT_ITEM_ABILITY,     /* input_ability   -- the weapon's own ability */
    KIT_ITEM_SECONDARY,   /* input_secondary -- the weapon's secondary */
    KIT_ITEM_RELOAD,      /* input_reload */
    KIT_ITEM_COUNT
} KitItem;

static const char *const KIT_ITEM_NAMES[KIT_ITEM_COUNT] = {
    "-- EMPTY --", "PHEROMONE", "ABILITY", "SECONDARY", "RELOAD"
};

/* The four gadget keys come AFTER the ten digits, in the founder's own order. */
static const char *const KIT_SLOT_KEY_NAMES[KIT_SLOTS] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "`", "-", "=", "C"
};
#define KIT_SLOT_FIRST_GADGET 10   /* index of ` */

/* Fixed indexes so callers never hard-code 10/11/12/13. */
#define KIT_SLOT_BACKTICK 10
#define KIT_SLOT_MINUS 11
#define KIT_SLOT_EQUALS 12
#define KIT_SLOT_C 13

typedef struct {
    char name[KIT_NAME_LEN];
    unsigned char slot[KIT_SLOTS];
} Kit;

typedef struct {
    Kit kits[KIT_MAX_KITS];
    int kit_count;
    int selected;   /* the LIVE kit -- what the gameplay keys read */
    Kit draft;      /* the KITBUILD app's working copy, so building never trashes the live kit */
} KitState;

static inline const char *kit_item_name(int item) {
    return (item > 0 && item < KIT_ITEM_COUNT) ? KIT_ITEM_NAMES[item] : KIT_ITEM_NAMES[0];
}

static inline const char *kit_slot_key_name(int slot) {
    return (slot >= 0 && slot < KIT_SLOTS) ? KIT_SLOT_KEY_NAMES[slot] : "?";
}

/* kit_slot_from_key -- ASCII key ('1'..'9', '0', '`', '-', '=', 'c'/'C') -> slot, or -1. The host
 * maps its own keycodes to a character and asks this, so this header never needs SDL. */
static inline int kit_slot_from_key(int key) {
    if (key >= '1' && key <= '9') return key - '1';
    if (key == '0') return 9;
    if (key == '`') return KIT_SLOT_BACKTICK;
    if (key == '-') return KIT_SLOT_MINUS;
    if (key == '=') return KIT_SLOT_EQUALS;
    if (key == 'c' || key == 'C') return KIT_SLOT_C;
    return -1;
}

/* kit_set_slot -- the only writer. Out-of-range slots and items are ignored rather than clamped
 * into some other key the player never asked for. */
static inline void kit_set_slot(Kit *k, int slot, int item) {
    if (!k || slot < 0 || slot >= KIT_SLOTS) return;
    if (item < 0 || item >= KIT_ITEM_COUNT) return;
    k->slot[slot] = (unsigned char)item;
}

/* kit_cycle -- step one slot's item forward/back through the real item list, wrapping. This is
 * what the phone's LEFT/RIGHT do; the empty slot is part of the cycle so a key can always be
 * cleared without a separate "clear" gesture. */
static inline void kit_cycle(Kit *k, int slot, int dir) {
    if (!k || slot < 0 || slot >= KIT_SLOTS) return;
    int item = (int)k->slot[slot] + (dir >= 0 ? 1 : -1);
    if (item >= KIT_ITEM_COUNT) item = 0;
    if (item < 0) item = KIT_ITEM_COUNT - 1;
    k->slot[slot] = (unsigned char)item;
}

static inline void kit_clear_slot(Kit *k, int slot) { kit_set_slot(k, slot, KIT_ITEM_NONE); }

static inline void kit_named(Kit *k, const char *name) {
    if (!k) return;
    memset(k, 0, sizeof(*k));
    snprintf(k->name, sizeof(k->name), "%s", name ? name : "KIT");
}

/* Three prefabs, deliberately different in shape rather than three flavours of the same thing: one
 * fights at range, one breaks things and runs, one is built around the canister. Digit slots 1-3
 * are left empty on purpose -- those are the weapons a player already carries, and a kit that
 * silently rebound them would be hostile. */
static inline void kit_defaults(KitState *s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    kit_named(&s->kits[0], "SCOUT");
    kit_set_slot(&s->kits[0], KIT_SLOT_BACKTICK, KIT_ITEM_RELOAD);
    kit_set_slot(&s->kits[0], KIT_SLOT_MINUS, KIT_ITEM_ABILITY);
    kit_set_slot(&s->kits[0], KIT_SLOT_EQUALS, KIT_ITEM_SECONDARY);
    kit_set_slot(&s->kits[0], KIT_SLOT_C, KIT_ITEM_PHEROMONE);

    kit_named(&s->kits[1], "BREACHER");
    kit_set_slot(&s->kits[1], KIT_SLOT_BACKTICK, KIT_ITEM_PHEROMONE);
    kit_set_slot(&s->kits[1], KIT_SLOT_MINUS, KIT_ITEM_SECONDARY);
    kit_set_slot(&s->kits[1], KIT_SLOT_EQUALS, KIT_ITEM_RELOAD);
    kit_set_slot(&s->kits[1], KIT_SLOT_C, KIT_ITEM_ABILITY);

    kit_named(&s->kits[2], "HUNTER");
    kit_set_slot(&s->kits[2], KIT_SLOT_BACKTICK, KIT_ITEM_ABILITY);
    kit_set_slot(&s->kits[2], KIT_SLOT_MINUS, KIT_ITEM_PHEROMONE);
    kit_set_slot(&s->kits[2], KIT_SLOT_EQUALS, KIT_ITEM_PHEROMONE);
    kit_set_slot(&s->kits[2], KIT_SLOT_C, KIT_ITEM_SECONDARY);
    s->kit_count = 3;
    s->selected = 0;
    s->draft = s->kits[0];
}

static inline void kit_select(KitState *s, int index) {
    if (!s || index < 0 || index >= s->kit_count) return;
    s->selected = index;
    s->draft = s->kits[index];   /* building always starts from what is live */
}

/* The live kit is the only thing gameplay reads. Returns KIT_ITEM_NONE for anything out of range. */
static inline int kit_live_item(const KitState *s, int slot) {
    if (!s || s->selected < 0 || s->selected >= s->kit_count) return KIT_ITEM_NONE;
    if (slot < 0 || slot >= KIT_SLOTS) return KIT_ITEM_NONE;
    return (int)s->kits[s->selected].slot[slot];
}

static inline void kit_cycle_draft(KitState *s, int slot, int dir) {
    if (!s) return;
    kit_cycle(&s->draft, slot, dir);
}

/* kit_save_draft -- KITBUILD's SELECT: the draft becomes the live kit. Copy, not swap, so the
 * draft keeps working as the thing you are still editing afterwards. */
static inline void kit_save_draft(KitState *s) {
    if (!s || s->selected < 0 || s->selected >= s->kit_count) return;
    s->kits[s->selected] = s->draft;
}

/* --- the three pages, as basic HTML + CSS ------------------------------------------------------
 * Deliberately one layout with different copy: PELLEGRINO's CSS surface is unverified beyond the
 * OS menu's own tiles, so nothing here depends on flexbox/grid/animations -- a table, a background,
 * a highlighted row, and a hint line. All return the length written, or -1 if it would not fit
 * (the caller keeps its previous page rather than rendering a truncated one). */

static inline int kit_page_add(char *out, int cap, int len, const char *s) {
    int n;
    if (len < 0 || !s) return -1;
    n = (int)strlen(s);
    if (len + n >= cap) return -1;
    memcpy(out + len, s, (size_t)n + 1);
    return len + n;
}

static inline int kit_page_head(char *out, int cap, const char *title, const char *sub) {
    int n = snprintf(out, (size_t)cap,
        "<html><head><style>"
        "body{background:#0b0f14;color:#cfe3f0;font-family:sans-serif;margin:0;padding:10px}"
        "h1{font-size:17px;color:#8fd0ff;margin:0 0 3px 0}"
        "p.s{color:#6f8ba0;font-size:12px;margin:0 0 8px 0}"
        "table{border-collapse:collapse;width:100%%}"
        "td{padding:2px 7px;font-size:13px}"
        "tr.on{background:#16394f}"
        "td.k{color:#ffd479;width:44px}"
        "td.i{color:#bfe9c8}"
        "td.x{color:#6f8ba0;font-size:11px}"
        "div.h{margin-top:9px;color:#5f7688;font-size:11px}"
        "</style></head><body><h1>%s</h1><p class=\"s\">%s</p><table>", title, sub);
    return (n < 0 || n >= cap) ? -1 : n;
}

static inline int kit_page_row(char *out, int cap, int len, const char *key, const char *item, int on) {
    char buf[160];
    snprintf(buf, sizeof(buf), "<tr%s><td class=\"k\">%s</td><td class=\"i\">%s</td></tr>",
             on ? " class=\"on\"" : "", key, item);
    return kit_page_add(out, cap, len, buf);
}

/* KITS -- pick which prefab kit is live. */
static inline int kit_page_select(char *out, int cap, const KitState *s) {
    int n, i;
    if (!out || !s) return -1;
    n = kit_page_head(out, cap, "KITS", "SELECT A PREBUILT KIT -- ENTER MAKES IT LIVE");
    if (n < 0) return -1;
    for (i = 0; i < s->kit_count; i++) {
        n = kit_page_row(out, cap, n, i == s->selected ? "*" : "", s->kits[i].name, i == s->selected);
        if (n < 0) return -1;
    }
    n = kit_page_add(out, cap, n, "</table><div class=\"h\">");
    if (n < 0) return -1;
    return kit_page_add(out, cap, n, "ARROWS MOVE   ENTER SELECT   HOME/SELECT = LIVE KIT</div></body></html>");
}

/* KITMAP -- what the live kit has on each of the 14 keys. */
static inline int kit_page_map(char *out, int cap, const KitState *s, int cursor) {
    int n, i;
    if (!out || !s) return -1;
    n = kit_page_head(out, cap, "KIT MAP", "WHAT EACH KEY DOES ON THE LIVE KIT");
    if (n < 0) return -1;
    for (i = 0; i < KIT_SLOTS; i++) {
        int item = kit_live_item(s, i);
        n = kit_page_row(out, cap, n, kit_slot_key_name(i), kit_item_name(item), i == cursor);
        if (n < 0) return -1;
    }
    n = kit_page_add(out, cap, n, "</table><div class=\"h\">");
    if (n < 0) return -1;
    return kit_page_add(out, cap, n,
        "UP/DOWN PICK KEY   LEFT/RIGHT CHANGE ITEM   ENTER CLEAR   (F IS TAKEN: VEHICLE ENTRY)</div></body></html>");
}

/* KITBUILD -- edit the draft, then save it over the live kit. Shows both so you can see the diff. */
static inline int kit_page_build(char *out, int cap, const KitState *s, int cursor) {
    int n, i;
    if (!out || !s) return -1;
    n = kit_page_head(out, cap, "KIT BUILDER", "EDIT THE DRAFT, THEN ENTER SAVES IT OVER THE LIVE KIT");
    if (n < 0) return -1;
    for (i = 0; i < KIT_SLOTS; i++) {
        char item[48];
        snprintf(item, sizeof(item), "%s%s", kit_item_name((int)s->draft.slot[i]),
                 (int)s->draft.slot[i] == kit_live_item(s, i) ? "" : " *");
        n = kit_page_row(out, cap, n, kit_slot_key_name(i), item, i == cursor);
        if (n < 0) return -1;
    }
    n = kit_page_add(out, cap, n, "</table><div class=\"h\">");
    if (n < 0) return -1;
    return kit_page_add(out, cap, n,
        "UP/DOWN PICK KEY   LEFT/RIGHT CHANGE ITEM   ENTER SAVE DRAFT   (* = DIFFERS FROM LIVE)</div></body></html>");
}

#endif /* SHANKPIT_KIT_H */
