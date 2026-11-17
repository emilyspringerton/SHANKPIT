#ifndef COLLECTION_OVERLAY_H
#define COLLECTION_OVERLAY_H

/* collection_overlay.h -- the Collection of Value inside SHANKPIT: Self Found's characters and vendors on the
 * stream overlay, in a terminal, and as an APPS page (founder, 2026-11-17: "ship all of this into the SHANKPIT
 * stream overlay social meeting terminal ..." / "PARENA FIRST MODS FIRST REFLUX ALL THE THINGS").
 *
 *   WoW SavedVariables --(PARENA scripts/collection-sf-feed.sh, outside the game)--> the feed file (REFLUX rows)
 *     --(CollectionBridge: reads it, dispatches every row into this client's REFLUX log)-->
 *       REFLUX --(polled by CollectionOverlay, a REFLUX mod)--> the model --> text for each surface
 *
 * Every decision (which rows are the Collection's, what each does, colour and order of a status, when the stream
 * shows a banner, lines per surface, reload and banner timing) is PARENA:
 * PARENA/stdlib/shankpit/collection_overlay_rules.prn -> collection_overlay_rules.c (scripts/gen_collection_overlay_rules.sh).
 * This file and collection_overlay.c are plumbing. No SDL, no GL: the lobby draws the text it gets back.
 *
 * The feed path comes from COLLECTION_FEED (the same variable PARENA's feed writer uses). Unset = the bridge is
 * off and every surface says so; there is no default path. */

#define COLLECTION_MAX_CHARS   256
#define COLLECTION_MAX_VENDORS 512
#define COLLECTION_MAX_NAMES   (COLLECTION_MAX_CHARS + COLLECTION_MAX_VENDORS)
#define COLLECTION_NAME_LEN    64
#define COLLECTION_LINE_LEN    96

typedef struct {
    char name[COLLECTION_NAME_LEN];
    int status;        /* 2 self-found since level 1, 1 since tracking, 0 broken */
    int n;             /* transactions */
    int broken_kind;   /* Self Found kind that broke it (selffound.prn), 0 if not broken */
    int broken_at;     /* epoch seconds */
    int tone;          /* co_tone: 1 green, 2 amber, 3 red */
} CollectionChar;

typedef struct {
    char name[COLLECTION_NAME_LEN];
    int npc, items;
} CollectionVendor;

typedef struct {
    CollectionChar chars[COLLECTION_MAX_CHARS];
    int nchars;
    CollectionVendor vendors[COLLECTION_MAX_VENDORS];
    int nvendors;
    int total, self_found, vendors_found; /* the summary row */
} CollectionModel;

typedef struct {
    /* names for the load in progress, by feed id (1-based) */
    char names[COLLECTION_MAX_NAMES + 1][COLLECTION_NAME_LEN];
    int nnames;
    CollectionModel pending;   /* filled by rows until the summary row ends the load */
    CollectionModel shown;     /* the last complete load: what every surface draws */
    int loads;                 /* complete loads applied */
    int cursor_abs;            /* REFLUX: next absolute row to read */
    int lost;                  /* rows REFLUX overwrote before the mod read them */
    char banner[COLLECTION_LINE_LEN];
    unsigned int banner_until_ms;
} CollectionOverlay;

typedef struct {
    const char *path;          /* COLLECTION_FEED, or NULL: off */
    long last_size;
    long long last_mtime;
    unsigned int next_check_ms;
    int loads, errors;
    char error[COLLECTION_LINE_LEN];
} CollectionBridge;

void collection_overlay_init(CollectionOverlay *o);
/* Read every new REFLUX row (the mod's poll). Call after anything dispatched. */
void collection_overlay_poll(CollectionOverlay *o, unsigned int now_ms);

/* path NULL = read COLLECTION_FEED from the environment. */
void collection_bridge_init(CollectionBridge *b, const char *path);
/* Once per frame: every co_reload_ms, if the feed file changed, dispatch it into REFLUX (in batches, letting the
 * overlay drain between them so the 256-row ring never overwrites unread rows). Returns 1 when a load happened. */
int collection_bridge_tick(CollectionBridge *b, CollectionOverlay *o, unsigned int now_ms);
/* Dispatch a feed held in memory (what tick does with the file's contents). Returns rows dispatched, -1 if the
 * text is not a v1 feed. */
int collection_bridge_load_text(CollectionBridge *b, CollectionOverlay *o, const char *text, unsigned int now_ms);

/* The surfaces. Each returns the number of lines written (at most max), one NUL-terminated line per row of out.
 * tones (may be NULL) gets the co_tone of each line: 0 plain, 1 green, 2 amber, 3 red. */
int collection_stream_lines(const CollectionOverlay *o, const CollectionBridge *b, unsigned int now_ms,
                            char out[][COLLECTION_LINE_LEN], int *tones, int max);
/* The terminal: "sf" (everyone), "sf vendors", "sf help". */
int collection_terminal(const CollectionOverlay *o, const CollectionBridge *b, const char *cmd,
                        char out[][COLLECTION_LINE_LEN], int *tones, int max);
/* The APPS page: one HTML document (PELLEGRINO draws the OS pages). Returns its length, or -1 if cap is short. */
int collection_apps_html(const CollectionOverlay *o, const CollectionBridge *b, char *buf, int cap);

#endif
