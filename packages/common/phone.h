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

#include <stdio.h>
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
    BP_APP_ORB,                                                                      /* EduVM terminal (#485) */
    /* BIG_O phone apps merged in (EMILY/BACKLOG.md SECTION 536 follow-up, "unify shankpit and BIG_O ...
       the phone all the subsystems"): free-text GFD chat terminal, IDUNA device-auth, read-only
       ARPANET archive, tech-tree shell. Host feeds them; this header stays pure/no-network. */
    BP_APP_GFD, BP_APP_IDUNA, BP_APP_ARPANET, BP_APP_TECHTREE,
    BP_APP_COUNT
} BpApp;

typedef enum { BP_UP, BP_DOWN, BP_LEFT, BP_RIGHT, BP_SELECT, BP_BACK } BpAction;

typedef enum {
    BP_FX_NONE = 0,
    BP_FX_ALLOCATE_TALENT,  /* arg = ability index 0..4 -> PC_PACKET_ALLOCATE_TALENT */
    BP_FX_WEAPON_SWITCH,    /* arg = weapon slot -> PC_PACKET_WEAPON_SWITCH */
    BP_FX_TAKE_PHOTO,       /* host may grab a screenshot; counter already advanced */
    BP_FX_EAT_FOOD,         /* arg = heal amount (food_item_heal); item already removed from cargo */
    BP_FX_OPEN_ORB,         /* ORB app SELECT -- host closes the phone and opens the Architect's Orb */
    BP_FX_SMASH_CAKE,       /* FOOD_CAKE specifically -- no heal, item already removed from cargo;
                                host triggers the real distraction effect (witness_ai_smash_cake) */
    BP_FX_COSTUME_SET,      /* arg = new costume index -> server-authoritative costume */
    BP_FX_CHAT_SEND,        /* arg = pending say length; text in p->term_input -- host calls phone_term_take() */
    BP_FX_IDUNA_START,      /* SELECT on idle/error IDUNA screen -> host starts /auth/device/start */
    BP_FX_IDUNA_POLL        /* SELECT on pending IDUNA screen -> host runs one /auth/device/poll */
} BpEffectKind;

typedef struct { BpEffectKind kind; int arg; } BpEffect;

#define BP_MAX_MESSAGES 16
#define BP_MAX_QUEUED 8
#define BP_NOTE_LINES 6
#define BP_CONTACTS 5
#define BP_CLONES 8
#define BP_TERM_INPUT_MAX 95
#define BP_TERM_LINES 6
#define BP_TERM_LINE_LEN 80
#define BP_ARPANET_NODES 17
#define BP_IDUNA_IDLE 0
#define BP_IDUNA_PENDING 1
#define BP_IDUNA_LINKED 2
#define BP_IDUNA_ERROR 3
#define BP_IDUNA_CODE_LEN 16
#define BP_IDUNA_URL_LEN 96
#define BP_IDUNA_HANDLE_LEN 64
#define BP_IDUNA_ERR_LEN 80
#define BP_TECH_NODES 12
#define BP_TECH_NAME_LEN 32
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
    "MESSAGES", "CONTACTS", "MAP", "CAMERA", "NOTES", "LAB", "CARGO", "SKILLS", "LOADOUT", "WARDROBE", "STATUS", "ORB", "GFD", "IDUNA", "ARPANET", "TECHTREE"
};

/* Contacts: trust ladder observer -> witness -> bound -> documented (spec). Preset replies advance it. */
static const char *const BP_TRUST_NAMES[4] = { "observer", "witness", "bound", "documented" };
static const char *const BP_CONTACT_HANDLES[BP_CONTACTS] = { "CAMERA OP", "DR THORNE", "THE PRODUCER", "EASTWIND OWL", "EMILY OS" };
static const char *const BP_REPLIES[3] = { "who is this?", "i saw nothing.", "send me the file." };

/* Map zones: a faction document, deliberately imprecise (spec), not a GPS. */
static const char *const BP_ZONE_NAMES[BP_ZONES] = { "WASTELAND", "NEXTOWN", "OFFICE BLOCK", "THE PARK", "BASEMENT LAB" };

static const char *const BP_COSTUME_NAMES[BP_COSTUMES] = { "CIVILIAN SUIT", "LAB SMOCK", "JANITOR OVERALLS", "FIELD GEAR" };

/* ARPANET terminal (BP_APP_ARPANET): a read-only, pre-seeded archive of old, pre-corporate
   research-network traffic -- the archive the Notes app's own pre-populated Eastwind Owls
   briefing already hints exists ("the archive is not where you think it is"). Five static nodes,
   numbered like the real historical ARPANET's own IMP host numbers, not a literal directory. */
static const char *const BP_ARPANET_TITLES[BP_ARPANET_NODES] = {
    "NODE 00 :: ROOT", "NODE 04 :: GRID LAB", "NODE 11 :: ARCHIVE", "NODE 19 :: MAILING LIST", "NODE 23 :: SIGN-OFF",
    "NODE 31 :: 1616 ARCHIVE", "NODE 42 :: 1978 ARCHIVE", "NODE 55 :: CUSTODY TRIAL I", "NODE 56 :: CUSTODY TRIAL II",
    "NODE 61 :: STILLNESS", "NODE 67 :: TIDES OF PARADOX", "NODE 73 :: 1993 ARCHIVE", "NODE 74 :: CUSTODY TRIAL III", "NODE 75 :: WHAT_COMES_NEXT.MD", "NODE 83 :: ACTIVATION #47", "NODE 91 :: STILLNESS (CONT.)", "NODE 97 :: CAMERA OP LOG"
};
static const char *const BP_ARPANET_BODIES[BP_ARPANET_NODES] = {
    "5 HOSTS ONLINE. THIS NETWORK PREDATES\nTHE COMPANY. NOBODY HAS TURNED IT OFF\nBECAUSE NOBODY REMEMBERS IT EXISTS.",
    "HARMONIC LOG, YEAR 3: A RECURRING\nANOMALY ON THE GRID, SAME FEW\nFREQUENCIES, EVERY TIME. WE DID NOT\nHAVE A NAME FOR IT. WE KEPT LOGGING\nANYWAY. THAT WAS THE WHOLE METHOD.",
    "THE ARCHIVE IS NOT A PLACE. IT IS A\nHABIT SOME PEOPLE NEVER STOPPED\nHAVING. UNCOMPROMISED, THEY CALLED\nTHEMSELVES. NOT A THREAT. A HABIT.",
    "RE: RE: RE: FUNDING\n> THEY WILL NOT KEEP GIVING US ROOMS.\nTHEN WE MEET WHEREVER THE NEXT ROOM\nIS. BRING SOUP. IT IS ALWAYS BETTER\nWHEN SOMEONE BRINGS SOUP.",
    "IF YOU ARE READING THIS THE NETWORK\nSTILL WORKS. THAT WAS THE POINT.\nLEAVE IT RUNNING FOR THE NEXT PERSON\nWHO FINDS IT BY ACCIDENT, LIKE YOU DID.",
    "EASTWIND OWL, RETROACTIVE FILE: 1616.\nROME (GALILEO), LONDON (SHAKESPEARE\nDIES, A FOLDED PAPER LEFT AT THE\nGATE), THE NEW ENGLAND COAST (PLAGUE,\nA PAPER CARRIED, NOT OPENED), JAPAN\n(PORTS CLOSE). HE WAS AT ALL FOUR.\nTHE PAPER IS STILL UNLOCATED.",
    "EASTWIND OWL, TYLER-1978 COMPOSITE.\nBOMBAY (A FLIGHT HE DID NOT BOARD),\nTHE CROWN OF HUNGARY, ALDO MORO,\nTHE INTEL 8086 (40 MIN, BOUGHT\nNOTHING), CAMP DAVID, THREE POPES,\nJONESTOWN. HE HUMMED THROUGH ALL OF\nIT. HE ALWAYS LEAVES BEFORE THE\nCROWD CLEARS.",
    "THE CUSTODY TRIAL, BK1 \"THE MERGE\".\nELI + MARA FIND APPS2/PACKAGES2/\nDOCS2 -- A PARALLEL TREE, COMMIT\nMESSAGE \"INIT\", AUTHOR \"E\". THE\nSPEC WAS ALREADY THERE. THE WARTHOG\n(VEHICLE CUSTODY) RUNS CLEAN.\nAPPROVED (TEMPORARY).",
    "THE CUSTODY TRIAL, BK2 CH1. SIX\nAUDIT SCENARIOS, ALL PASS. BLAME\nPATH CLEAN. A LOG LINE POINTS TO\n/custody/WHAT_COMES_NEXT.md.\nFILE NOT FOUND. \"THE FILE WILL\nEXIST WHEN WE UNDERSTAND WHAT\nIT'S FOR.\"",
    "E.J. SPRINGERTON, \"BUILDING THE\nPLANE THROUGH STILLNESS.\" PROGRESS\nIS QUIET, NOT LOUD. THE BREAKTHROUGH\nALREADY HAPPENED BY THE TIME YOU\nFEEL IT. DON'T CHASE THE DOPAMINE\nDRAGON. CLEAN BUILDS FIRST.",
    "TIDES OF PARADOX, BK1 \"THE DRY\nINTERVAL\". A SCHOOL APPEARS ON\nTHURSDAY. NOBODY IS SURPRISED. A\nDOCUMENTARY CREW FILMS \"TYLER.\"\nA MAN WATCHES A THIRD-FLOOR WINDOW\nAND LEAVES. NINETY SECONDS. THEN\nHE IS GONE.",
    "EASTWIND OWL, 1993 COMPOSITE. FIRST\nWEB ACCESS, 47 MIN, ONE PAGE, \"I\nFOUND IT.\" OSLO ACCORD -- A FOLDED\nPAPER LEFT ON THE LAWN, SEALED, 32\nYEARS UNOPENED. WACO: HE WAS NOT\nTHERE (FIRST ABSENCE ON RECORD).\nMOSCOW: PARLIAMENT SHELLED, HE LEFT\nNOTHING. DOOM, DEC 10: HE WALKED\nTHE CORRIDORS, DIDN'T SHOOT. A TILE\nAPPEARED: \"THE BUILD TARGET: 30\nYEARS.\" SHE WAS NOT WRONG.",
    "THE CUSTODY TRIAL, BK2 CH5-8. THE\nCHAIN WAS BUILT FOR A TRANSFER\nPENDING SINCE 1623. SUBSCRIBER =\nTHE SYSTEM'S OLDEST USER, NOT AN\nOUTSIDER. A FILE LANDS, NO AUTHOR,\nCOMMIT a1623f0: \"THE CHAIN IS\nREADY. I AM READY. WHEN THE\nSENTENCE COMPLETES, THE TRANSFER\nWILL COMPLETE.\" CLEAN BUILDS FIRST.\nTHEN CUSTODY. THEN EVERYTHING ELSE.",
    "/custody/WHAT_COMES_NEXT.md, BY ELI\n+ MARA. CLEAN != COMPLETE -- THE\nBLAME PATH IS ALWAYS OCCUPIED, THAT\nIS ALL. SUBSCRIBER.NOTIFY(\n\"custody_chain_complete\"), 380\nYEARS WAITING, PAYLOAD next:\npending. THE SUBSCRIBER OPERATES\nAT THE SUBSTRATE LAYER, LIKE EMILY\nOS, BUT IS NOT HER. THE WARTHOG WAS\nTHE EASY CASE. A TRANSFER IS\nCOMING. TO WHOM: UNKNOWN.",
    "FIELD ACTIVATION #47, ZAGAN/SATURN,\n4.44 HZ. A 47-MIN MONOLOGUE DERIVES\nTHE RIEMANN HYPOTHESIS FROM ALCHEMY.\nLEAD = WEIGHT WITHOUT LOCATION. THE\nCRITICAL LINE = THE ONLY POINT A\nMIRROR LETS MASS REST: s = 1-s, s\n= 1/2. \"BASE METAL SCREAMS WHEN IT\nREMEMBERS IT WAS GOLD WITH THE\nWRONG ADDRESS.\" STILL BEING PEER-\nREVIEWED.",
    "STILLNESS, PARTS VI-VII. MAP THE\nSESSION BEFORE THE SESSION. MEASURE\nBACKWARD, NOT FORWARD. PROTECT THE\nLOAD-BEARING SESSIONS -- AVOIDANCE\nPOINTS AT THEM. WRITE DOWN THE\nTHIRTY-YEAR ANSWER AT THE START.\nTHE WORK DOES NOT RESOLVE, IT\nADVANCES. BUILD CLEAN. LET THE\nTHIRTY YEARS RUN. START NOW.",
    "CAMERA OP SEALED LOG, NOT SHARED,\nNOT DIGITAL. TYLER FLIPS A COIN,\n1901 AND 1623 (\"THAT IS THE ONE I\nTHINK ABOUT MOST\"). SHE PUBLISHES\nALL THE FOOTAGE -- BREAKS THE\nSYNDICATE BINDING ON PURPOSE. THEN\nA NEW ARCHIVE: TYLER STAYS IN\nPLACES, LEAVES 64 PERCENT OF\nHIMSELF EVERY TIME HE GOES. CORDOBA,\n412 FEET. A SHIP, 1119: A SCHOLAR,\nAHMAD, ASKED A QUESTION HE COULD\nNOT ANSWER YET. SHE FINDS AHMAD\nNAME. SHE STAYS."
};

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
    int shadow_war_elo;      /* SHANKPIT-native, not part of the original BIG_O port: the basement
                               * shadow war's own rating (shadow_war_host.h). 0 = never fought,
                               * lazily seeded to SHADOW_WAR_ELO_DEFAULT (1200) on first deploy. */
    int cargo[BP_INV_SLOTS]; int cargo_count;   /* CARGO's own real content -- FoodItemId values */
    int costume;             /* worn costume index */
    int weapons_owned;       /* bitmask, mirrored from server */
    int current_weapon;

    /* notifications */
    int queue[BP_MAX_QUEUED]; int queue_len;
    unsigned int shown_at[BP_SPAM_MAX]; int shown_n;    /* recent banner times inside the spam window */
    int banner_id; unsigned int banner_since; int banner_batched;

    /* GFD terminal (BP_APP_GFD): free-text input + scrollback; term_input always NUL-terminated. */
    char term_input[BP_TERM_INPUT_MAX + 1]; int term_input_len;
    char term_lines[BP_TERM_LINES][BP_TERM_LINE_LEN]; int term_line_count;
    /* IDUNA device-auth flow (BP_APP_IDUNA): host-fed display state. */
    int iduna_stage;
    char iduna_user_code[BP_IDUNA_CODE_LEN];
    char iduna_verification_url[BP_IDUNA_URL_LEN];
    char iduna_handle[BP_IDUNA_HANDLE_LEN];
    char iduna_error[BP_IDUNA_ERR_LEN];
    /* Tech tree (BP_APP_TECHTREE): host-fed read-only node list. */
    int tech_count;
    char tech_names[BP_TECH_NODES][BP_TECH_NAME_LEN];
    int tech_unlocked[BP_TECH_NODES];
} Phone;

static inline void phone_init(Phone *p) {
    memset(p, 0, sizeof(*p));
    p->app = -1; p->zone_alert = -1; p->zone_pinned = -1; p->zone_current = 0;
    p->contacts_met = 1;
    p->weapons_owned = 1;
    strcpy(p->notes[0], "The archive is not where you");   /* spec: pre-populated Eastwind Owls briefing */
    strcpy(p->notes[1], "think it is. Start with what the");
    strcpy(p->notes[2], "building smells like.");
    strcpy(p->term_lines[0], "GFD TERMINAL -- type, ENTER to say");
    p->term_line_count = 1;
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
    case BP_APP_GFD: return p->term_line_count > 0 ? p->term_line_count : 1;
    case BP_APP_IDUNA: return 1;
    case BP_APP_ARPANET: return BP_ARPANET_NODES;
    case BP_APP_TECHTREE: return p->tech_count > 0 ? p->tech_count : 1;
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

/* phone_term_line -- append a scrollback line to the GFD terminal (used for both the local
 * echo of a sent "say" and an incoming PC_PACKET_CHAT_RECV). Truncates to BP_TERM_LINE_LEN-1,
 * shifts out the oldest line once full -- same real "shift, don't drop the newest" convention
 * phone_notify's own messages[] overflow handling already establishes. */
static inline void phone_term_line(Phone *p, const char *text) {
    char buf[BP_TERM_LINE_LEN];
    size_t n = strlen(text);
    if (n >= BP_TERM_LINE_LEN) n = BP_TERM_LINE_LEN - 1;
    memcpy(buf, text, n);
    buf[n] = '\0';
    if (p->term_line_count < BP_TERM_LINES) {
        memcpy(p->term_lines[p->term_line_count++], buf, n + 1);
    } else {
        for (int i = 0; i < BP_TERM_LINES - 1; i++) memcpy(p->term_lines[i], p->term_lines[i + 1], BP_TERM_LINE_LEN);
        memcpy(p->term_lines[BP_TERM_LINES - 1], buf, n + 1);
    }
}

/* phone_term_char -- append one printable ASCII character to the pending input, dropped
 * silently once full or if non-printable (matches SDL_TEXTINPUT's own real UTF-8 text callback --
 * multi-byte sequences are rejected byte-by-byte here rather than decoded, a real, honest v1
 * limit: no non-ASCII chat text yet). */
static inline void phone_term_char(Phone *p, char c) {
    if (c < 32 || c > 126) return;
    if (p->term_input_len >= BP_TERM_INPUT_MAX) return;
    p->term_input[p->term_input_len++] = c;
    p->term_input[p->term_input_len] = '\0';
}

static inline void phone_term_backspace(Phone *p) {
    if (p->term_input_len > 0) p->term_input[--p->term_input_len] = '\0';
}

/* phone_term_take -- copies out the pending input (up to out_cap bytes, NOT NUL-terminated by
 * this call -- caller owns that) and clears it. The host calls this exactly once, immediately on
 * seeing a BP_FX_CHAT_SEND effect, before any further phone input can overwrite term_input.
 * Returns the real byte count copied. */
static inline int phone_term_take(Phone *p, char *out, int out_cap) {
    int n = p->term_input_len;
    if (n > out_cap) n = out_cap;
    memcpy(out, p->term_input, n);
    p->term_input_len = 0;
    p->term_input[0] = '\0';
    return n;
}

/* phone_iduna_set_pending/_linked/_error -- the host calls exactly one of these once its
 * real, async IDUNA HTTP job (BIG_O/NORTHSTAR.md §31) finishes, to feed the real result back into
 * the phone's own display state. Truncates rather than overflowing -- same real, silent clamp
 * discipline phone_term_line's own overflow handling already establishes, not a crash. */
static inline void phone_iduna_set_pending(Phone *p, const char *user_code, const char *verification_url) {
    p->iduna_stage = BP_IDUNA_PENDING;
    snprintf(p->iduna_user_code, sizeof(p->iduna_user_code), "%s", user_code);
    snprintf(p->iduna_verification_url, sizeof(p->iduna_verification_url), "%s", verification_url);
    p->iduna_error[0] = '\0';
}
static inline void phone_iduna_set_linked(Phone *p, const char *handle) {
    p->iduna_stage = BP_IDUNA_LINKED;
    snprintf(p->iduna_handle, sizeof(p->iduna_handle), "%s", handle);
    p->iduna_error[0] = '\0';
}
static inline void phone_iduna_set_error(Phone *p, const char *msg) {
    p->iduna_stage = BP_IDUNA_ERROR;
    snprintf(p->iduna_error, sizeof(p->iduna_error), "%s", msg);
}

/* phone_set_tech -- the host feeds the tech-tree node list (BP_APP_TECHTREE, kanban #577).
 * Clamps to BP_TECH_NODES rather than overflowing; names are truncated by snprintf, not rejected. */
static inline void phone_set_tech(Phone *p, const char *const *names, const int *unlocked, int n) {
    if (!names || n < 0) n = 0;   /* a NULL feed is an empty feed, not a crash */
    if (n > BP_TECH_NODES) n = BP_TECH_NODES;
    p->tech_count = n;
    for (int i = 0; i < n; i++) {
        snprintf(p->tech_names[i], sizeof(p->tech_names[i]), "%s", names[i]);
        p->tech_unlocked[i] = unlocked ? unlocked[i] : 0;
    }
}

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

    if (a == BP_BACK) { if ((p->app == BP_APP_MESSAGES || p->app == BP_APP_ARPANET) && p->detail) p->detail = 0; else p->app = -1; return fx; }
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
        if (a == BP_SELECT && p->cursor != p->costume) {
            p->costume = p->cursor;
            fx.kind = BP_FX_COSTUME_SET; fx.arg = p->costume;
        }
        break;
    case BP_APP_GFD:
        if (a == BP_SELECT && p->term_input_len > 0) { fx.kind = BP_FX_CHAT_SEND; fx.arg = p->term_input_len; }
        break;
    case BP_APP_IDUNA:
        if (a == BP_SELECT) {
            if (p->iduna_stage == BP_IDUNA_IDLE || p->iduna_stage == BP_IDUNA_ERROR) fx.kind = BP_FX_IDUNA_START;
            else if (p->iduna_stage == BP_IDUNA_PENDING) fx.kind = BP_FX_IDUNA_POLL;
        }
        break;
    case BP_APP_ARPANET:
        if (a == BP_SELECT) p->detail = !p->detail;
        break;
    case BP_APP_ORB:
        if (a == BP_SELECT) fx.kind = BP_FX_OPEN_ORB;
        break;
    default: break;
    }
    return fx;
}

#endif
