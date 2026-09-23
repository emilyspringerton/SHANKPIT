/* phone.h -- BIG_O engine merge phase 6 (EMILY/BACKLOG.md SECTION 536, founder real-time: "the
 * phone in story mode everything", "all the events and messages on the phone"). Ported in
 * verbatim from BIG_O's day/packages/common/bigo_phone.h -- the in-game smartphone: the ONE way
 * every BIG_O menu is reached. Renamed BigoPhone -> Phone / bigo_phone_* -> phone_*; the `Bp`/
 * `BP_` prefix on the enums/constants is kept as-is (not overtly BIG_O-branded, and renaming
 * dozens of call sites for a purely cosmetic reason would be mechanical risk for no real value).
 *
 * Source spec: TYLER/engine/tyler_phone_mechanics.md (Messages, Contacts, Map, Camera, Notes,
 * notification anti-spam "max 2 per 30s, excess batched into a summary"). PAPERCRAFT only shipped
 * the notification banner; this is the real browsable phone. BIG_O adds the apps its loops need:
 * Lab, Cargo, Skills, Loadout, Wardrobe, Status.
 *
 * Pure C99, no GL/SDL/network: input in (abstract actions), state + "effects" out (things the host
 * must send to the server). The client renders the state and turns effects into packets.
 * Deterministic and unit-tested.
 *
 * Real, honest scope of this port: the state machine + notification system only (this IS "the
 * events and messages on the phone"). Deliberately NOT wired into apps/lobby's actual SDL2 render
 * loop yet -- rendering a phone screen needs a real 2D text/menu drawing path this repo's client
 * doesn't have wired up for this purpose, and the world-feed inputs (phone_set_world) have no live
 * source yet either (day_night_clock.c, phase 1, doesn't dispatch REFLUX events the way BIG_O's
 * own world.c does -- a named, separate gap, see this doc's own phase 1 scope-cut note). Verified
 * standalone instead, same discipline every phase 2-5 primitive already used. */
#ifndef PHONE_H
#define PHONE_H

#include <string.h>

/* BIG_O basic food system (EMILY/BACKLOG.md SECTION 536 follow-up, founder real-time,
 * 2026-09-22): CARGO's own real content + the "use" half of "pickup and use (cargo)". The
 * pickup half lives in packages/simulation/food_pickup.h -- this header only needs the pure
 * data table (names/points/heal), same layering every other BIG_O-apps field below already
 * uses (e.g. BP_COSTUME_NAMES for WARDROBE). */
#include "food_items.h"

typedef enum {
    BP_APP_MESSAGES = 0, BP_APP_CONTACTS, BP_APP_MAP, BP_APP_CAMERA, BP_APP_NOTES,   /* TYLER spec apps */
    BP_APP_LAB, BP_APP_CARGO, BP_APP_SKILLS, BP_APP_LOADOUT, BP_APP_WARDROBE, BP_APP_STATUS, /* BIG_O apps */
    BP_APP_COUNT
} BpApp;

typedef enum { BP_UP, BP_DOWN, BP_LEFT, BP_RIGHT, BP_SELECT, BP_BACK } BpAction;

typedef enum {
    BP_FX_NONE = 0,
    BP_FX_ALLOCATE_TALENT,  /* arg = ability index 0..4 -> PC_PACKET_ALLOCATE_TALENT */
    BP_FX_WEAPON_SWITCH,    /* arg = weapon slot -> PC_PACKET_WEAPON_SWITCH */
    BP_FX_TAKE_PHOTO,       /* host may grab a screenshot; counter already advanced */
    BP_FX_EAT_FOOD,         /* arg = heal amount (food_item_heal); item already removed from cargo */
    BP_FX_SMASH_CAKE        /* FOOD_CAKE specifically -- no heal, item already removed from cargo;
                                host triggers the real distraction effect (witness_ai_smash_cake) */
} BpEffectKind;

typedef struct { BpEffectKind kind; int arg; } BpEffect;

#define BP_MAX_MESSAGES 16
#define BP_MAX_QUEUED 8
#define BP_NOTE_LINES 6
#define BP_CONTACTS 5
#define BP_CLONES 8
#define BP_INV_SLOTS 8
#define BP_ZONES 5
#define BP_COSTUMES 4
#define BP_BANNER_MS 5000
#define BP_SPAM_WINDOW_MS 30000
#define BP_SPAM_MAX 2

static const char *const BP_PHASE_NAMES[4] = { "DAWN", "DAY", "DUSK", "NIGHT" };
static const char *const BP_WEATHER_NAMES[4] = { "CLEAR", "OVERCAST", "RAIN", "STORM" };
#define BP_MSG_THORNE_BRIEF 6   /* client message table id: Dr. Thorne's A1M1 reprimand; unlocks him in Contacts */

static const char *const BP_APP_NAMES[BP_APP_COUNT] = {
    "MESSAGES", "CONTACTS", "MAP", "CAMERA", "NOTES", "LAB", "CARGO", "SKILLS", "LOADOUT", "WARDROBE", "STATUS"
};

/* Contacts: trust ladder observer -> witness -> bound -> documented (spec). Preset replies advance it. */
static const char *const BP_TRUST_NAMES[4] = { "observer", "witness", "bound", "documented" };
static const char *const BP_CONTACT_HANDLES[BP_CONTACTS] = { "CAMERA OP", "DR THORNE", "THE PRODUCER", "EASTWIND OWL", "EMILY OS" };
static const char *const BP_REPLIES[3] = { "who is this?", "i saw nothing.", "send me the file." };

/* Map zones: a faction document, deliberately imprecise (spec), not a GPS. */
static const char *const BP_ZONE_NAMES[BP_ZONES] = { "WASTELAND", "NEXTOWN", "OFFICE BLOCK", "THE PARK", "BASEMENT LAB" };

static const char *const BP_COSTUME_NAMES[BP_COSTUMES] = { "CIVILIAN SUIT", "LAB SMOCK", "JANITOR OVERALLS", "FIELD GEAR" };

/* Lab terminal: isolate -> align -> splice, base vector x trait, consuming one sample per splice. */
static const char *const BP_BASES[3] = { "SCAVENGER", "HOUND", "BRUTE" };
static const char *const BP_TRAITS[3] = { "SPEED", "ARMOR", "SCENT" };

typedef struct {
    int open;
    int app;                 /* -1 = home grid, else BpApp */
    int home_cursor;
    int cursor;              /* per-app row cursor (reset on entering an app) */
    int cursor2;             /* second axis (lab base / contact reply) */
    int lab_trait;           /* lab: selected trait */

    int messages[BP_MAX_MESSAGES]; int message_count; int unread;
    int trust[BP_CONTACTS]; int replied[BP_CONTACTS]; int contacts_met;
    int zone_current, zone_pinned, zone_alert;   /* zone_alert = zone highlighted by a server event, -1 none */
    int photos;
    int detail;              /* messages: 1 = showing the selected message in full */
    /* world feed (host-fed: clock/weather/zombies). wf_valid 0 = no world source, screens say so. */
    int wf_valid, wf_minute, wf_day, wf_phase, wf_weather, wf_zombies[BP_ZONES], wf_sight;
    char notes[BP_NOTE_LINES][48];
    int samples[3];          /* harvested sample counts per type (fed by the host; 0 until harvesting exists) */
    int clones[BP_CLONES]; int clone_count; int clone_traits[BP_CLONES];
    int cargo[BP_INV_SLOTS]; int cargo_count;   /* CARGO's own real content -- FoodItemId values */
    int costume;             /* worn costume index */
    int weapons_owned;       /* bitmask, mirrored from server */
    int current_weapon;

    /* notifications */
    int queue[BP_MAX_QUEUED]; int queue_len;
    unsigned int shown_at[BP_SPAM_MAX]; int shown_n;    /* recent banner times inside the spam window */
    int banner_id; unsigned int banner_since; int banner_batched;
} Phone;

static inline void phone_init(Phone *p) {
    memset(p, 0, sizeof(*p));
    p->app = -1; p->zone_alert = -1; p->zone_pinned = -1; p->zone_current = 0;
    p->contacts_met = 1;
    p->weapons_owned = 1;
    strcpy(p->notes[0], "The archive is not where you");   /* spec: pre-populated Eastwind Owls briefing */
    strcpy(p->notes[1], "think it is. Start with what the");
    strcpy(p->notes[2], "building smells like.");
}

static inline int bp_wrap(int v, int n) { return n <= 0 ? 0 : ((v % n) + n) % n; }

static inline void bp_enter_app(Phone *p, int app) {
    p->detail = 0;
    p->app = app; p->cursor = 0; p->cursor2 = 0; p->lab_trait = 0;
    if (app == BP_APP_MESSAGES) p->unread = 0;
}

/* Rows the current app's cursor ranges over (for wrap). */
static inline int bp_rows(const Phone *p) {
    switch (p->app) {
    case BP_APP_MESSAGES: return p->message_count > 0 ? p->message_count : 1;
    case BP_APP_CONTACTS: return p->contacts_met;
    case BP_APP_MAP: return BP_ZONES;
    case BP_APP_NOTES: return BP_NOTE_LINES;
    case BP_APP_LAB: return 4;       /* base, trait, SPLICE, clone list */
    case BP_APP_CARGO: return p->cargo_count > 0 ? p->cargo_count : 1;
    case BP_APP_SKILLS: return 5;
    case BP_APP_LOADOUT: return 6;
    case BP_APP_WARDROBE: return BP_COSTUMES;
    default: return 1;
    }
}

/* Notification with the spec's anti-spam rule: <= 2 banners per 30s, extras queued and later shown as a summary. */
static inline void bp_prune(Phone *p, unsigned int now) {
    int k = 0;
    for (int i = 0; i < p->shown_n; i++)
        if (now - p->shown_at[i] < BP_SPAM_WINDOW_MS) p->shown_at[k++] = p->shown_at[i];
    p->shown_n = k;
}

static inline void phone_notify(Phone *p, int msg_id, unsigned int now) {
    if (msg_id <= 0) return;
    if (msg_id == BP_MSG_THORNE_BRIEF && p->contacts_met < 2) p->contacts_met = 2;
    if (p->message_count < BP_MAX_MESSAGES) p->messages[p->message_count++] = msg_id;
    else { memmove(p->messages, p->messages + 1, sizeof(int) * (BP_MAX_MESSAGES - 1)); p->messages[BP_MAX_MESSAGES - 1] = msg_id; }
    if (!(p->open && p->app == BP_APP_MESSAGES)) p->unread++;
    bp_prune(p, now);
    if (p->shown_n < BP_SPAM_MAX) {
        p->shown_at[p->shown_n++] = now;
        p->banner_id = msg_id; p->banner_since = now; p->banner_batched = 0;
    } else if (p->queue_len < BP_MAX_QUEUED) {
        p->queue[p->queue_len++] = msg_id;
    }
}

/* Per-frame: expire the banner; release a batched summary once the window has room. */
static inline void phone_tick(Phone *p, unsigned int now) {
    if (p->banner_id && now - p->banner_since > BP_BANNER_MS) p->banner_id = 0;
    bp_prune(p, now);
    if (!p->banner_id && p->queue_len > 0 && p->shown_n < BP_SPAM_MAX) {
        p->shown_at[p->shown_n++] = now;
        p->banner_id = p->queue[p->queue_len - 1];
        p->banner_batched = p->queue_len;
        p->banner_since = now;
        p->queue_len = 0;
    }
}

static inline void phone_set_world(Phone *p, int minute_of_day, int day, int phase, int weather, const int *zombies, int sight_pct) {
    p->wf_valid = 1; p->wf_minute = minute_of_day; p->wf_day = day; p->wf_phase = phase; p->wf_weather = weather; p->wf_sight = sight_pct;
    for (int i = 0; i < BP_ZONES; i++) p->wf_zombies[i] = zombies ? zombies[i] : 0;
}

static inline void phone_toggle(Phone *p) {
    p->open = !p->open;
    if (p->open) p->app = -1;
}
static inline void phone_open_app(Phone *p, int app) { p->open = 1; bp_enter_app(p, app); }

/* Real, live pickup-side entry point: adds one collected food item to CARGO. Capped at
 * BP_INV_SLOTS, same real "full is full" honesty BP_MAX_MESSAGES's own overflow-shift neighbor
 * demonstrates elsewhere -- here a full cargo just drops the pickup rather than evicting an
 * already-held item (food isn't a priority queue the way notifications are). Returns 1 if added,
 * 0 if cargo was already full. */
static inline int phone_cargo_add(Phone *p, int item_id) {
    if (p->cargo_count >= BP_INV_SLOTS) return 0;
    p->cargo[p->cargo_count++] = item_id;
    return 1;
}

/* Feed one abstract input. Returns the effect the host must carry out (BP_FX_NONE mostly). */
static inline BpEffect phone_input(Phone *p, BpAction a, int unspent_points) {
    BpEffect fx = { BP_FX_NONE, 0 };
    if (!p->open) return fx;

    if (p->app < 0) {   /* home grid: 3 columns */
        const int cols = 3;
        if (a == BP_LEFT) p->home_cursor = bp_wrap(p->home_cursor - 1, BP_APP_COUNT);
        else if (a == BP_RIGHT) p->home_cursor = bp_wrap(p->home_cursor + 1, BP_APP_COUNT);
        else if (a == BP_UP) p->home_cursor = p->home_cursor - cols >= 0 ? p->home_cursor - cols : p->home_cursor;
        else if (a == BP_DOWN) p->home_cursor = p->home_cursor + cols < BP_APP_COUNT ? p->home_cursor + cols : p->home_cursor;
        else if (a == BP_SELECT) bp_enter_app(p, p->home_cursor);
        else if (a == BP_BACK) p->open = 0;
        return fx;
    }

    if (a == BP_BACK) { if (p->app == BP_APP_MESSAGES && p->detail) p->detail = 0; else p->app = -1; return fx; }
    int n = bp_rows(p);
    if (a == BP_UP) { p->cursor = bp_wrap(p->cursor - 1, n); return fx; }
    if (a == BP_DOWN) { p->cursor = bp_wrap(p->cursor + 1, n); return fx; }

    switch (p->app) {
    case BP_APP_MESSAGES:
        if (a == BP_SELECT && p->message_count > 0) p->detail = !p->detail;
        break;
    case BP_APP_CONTACTS:
        if (a == BP_LEFT || a == BP_RIGHT) p->cursor2 = bp_wrap(p->cursor2 + (a == BP_RIGHT ? 1 : -1), 3);
        else if (a == BP_SELECT && p->cursor < p->contacts_met) {   /* preset reply: the phone's primary agency mechanic */
            p->replied[p->cursor] = p->cursor2 + 1;
            if (p->trust[p->cursor] < 3) p->trust[p->cursor]++;
        }
        break;
    case BP_APP_MAP:
        if (a == BP_SELECT) p->zone_pinned = (p->zone_pinned == p->cursor) ? -1 : p->cursor;
        break;
    case BP_APP_CAMERA:
        if (a == BP_SELECT) { p->photos++; fx.kind = BP_FX_TAKE_PHOTO; fx.arg = p->photos; }
        break;
    case BP_APP_CARGO: /* use the selected food item -- the real "use" half of the pickup-and-use ask */
        if (a == BP_SELECT && p->cargo_count > 0 && p->cursor < p->cargo_count) {
            int item = p->cargo[p->cursor];
            /* FOOD_CAKE is smashed, not eaten -- founder real-time follow-up: "if the cake gets
               smashed it flies everywhere and causes a big distraction." No heal; the host
               triggers the real distraction effect instead (see BP_FX_SMASH_CAKE's own doc
               comment above). */
            if (item == FOOD_CAKE) {
                fx.kind = BP_FX_SMASH_CAKE;
            } else {
                fx.kind = BP_FX_EAT_FOOD;
                fx.arg = food_item_heal(item);
            }
            for (int i = p->cursor; i < p->cargo_count - 1; i++) p->cargo[i] = p->cargo[i + 1];
            p->cargo_count--;
            if (p->cursor >= p->cargo_count && p->cursor > 0) p->cursor--;
        }
        break;
    case BP_APP_LAB:
        if (p->cursor == 0 && (a == BP_LEFT || a == BP_RIGHT)) p->cursor2 = bp_wrap(p->cursor2 + (a == BP_RIGHT ? 1 : -1), 3);
        else if (p->cursor == 1 && (a == BP_LEFT || a == BP_RIGHT)) p->lab_trait = bp_wrap(p->lab_trait + (a == BP_RIGHT ? 1 : -1), 3);
        else if (p->cursor == 2 && a == BP_SELECT) {
            int base = p->cursor2, trait = p->lab_trait;
            if (p->samples[base] > 0 && p->clone_count < BP_CLONES) {
                p->samples[base]--;
                p->clones[p->clone_count] = base;
                p->clone_traits[p->clone_count] = trait;
                p->clone_count++;
            }
        }
        break;
    case BP_APP_SKILLS:
        if (a == BP_SELECT && unspent_points > 0) { fx.kind = BP_FX_ALLOCATE_TALENT; fx.arg = p->cursor; }
        break;
    case BP_APP_LOADOUT:
        if (a == BP_SELECT && (p->cursor == 0 || (p->weapons_owned & (1 << p->cursor)))) { fx.kind = BP_FX_WEAPON_SWITCH; fx.arg = p->cursor; }
        break;
    case BP_APP_WARDROBE:
        if (a == BP_SELECT) p->costume = p->cursor;
        break;
    default: break;
    }
    return fx;
}

#endif
