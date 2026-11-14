/* packages/ui/gear_board.h -- the gear board: every World of Warcraft character on the account -- level, gold, bags,
 * gear by slot -- and what the account needs, on a big screen in SHANKPIT (founder, 2026-11-14: "shankit needs to start
 * tracking the gear so i can see it on a big screen in game").
 *
 * The data comes from the plugin forge (PARENA runtime/wow/forge/forge.lua, `forge board` / `forge watch`), which reads
 * the WoW SavedVariables and writes board.tsv: one record per line, tab-separated, the first field its kind --
 *   SHANKPIT_GEAR_BOARD <version> <generated unix time>
 *   C character class level role gold(copper) zone subzone bags_free bags_total bags_worn bag_slots ilvl xp_pct
 *   G character slot slot_name item ilvl quality
 *   N urgency character kind what detail from
 * This file finds it ($SHANKPIT_GEAR_BOARD, else $FORGE_DIR/board.tsv), parses it, and re-reads it when the forge
 * rewrites it (it checks the file's time and size at most once a second). No GL here: apps/lobby draws it (draw_gear_board),
 * paging with PARENA's linen UI layer (stdlib/ui/menu.prn menu-page-*). Header-only, like ui_bridge.c.
 */
#ifndef GEAR_BOARD_H
#define GEAR_BOARD_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define GB_MAX_CHARS 32
#define GB_MAX_GEAR (GB_MAX_CHARS * 19)
#define GB_MAX_NEEDS 128

typedef struct {
    char name[48], class_[16], role[16], zone[48], subzone[48];
    int level, ilvl, xp_pct;
    long long gold;                       /* copper */
    int bags_free, bags_total, bags_worn, bag_slots;   /* -1: not read yet */
} GearBoardChar;

typedef struct {
    int ch;                               /* index into chars */
    int slot;
    char slot_name[16], item[64];
    int ilvl, quality;                    /* -1: unknown */
} GearBoardItem;

typedef struct {
    int urgency;
    char character[48], kind[24], what[64], detail[64], from[160];
} GearBoardNeed;

typedef struct {
    int loaded;                           /* 1 once a board has been parsed */
    long long generated;
    GearBoardChar chars[GB_MAX_CHARS];   int nchars;
    GearBoardItem gear[GB_MAX_GEAR];     int ngear;
    GearBoardNeed needs[GB_MAX_NEEDS];   int nneeds;
    /* the file being watched */
    char path[512];
    long long mtime, size;
    unsigned last_check_ms;
    char status[160];                     /* what the screen says when there is nothing to show */
    /* the screen */
    int open, page;
} GearBoard;

/* Copy field `i` (0-based) of a tab-separated line into out. Returns 1 when the field exists. */
static inline int gear_board_field(const char *line, int i, char *out, size_t outsz) {
    const char *p = line;
    for (int k = 0; k < i; k++) {
        p = strchr(p, '\t');
        if (!p) { if (outsz) out[0] = 0; return 0; }
        p++;
    }
    size_t n = strcspn(p, "\t\r\n");
    if (outsz == 0) return 1;
    if (n >= outsz) n = outsz - 1;
    memcpy(out, p, n);
    out[n] = 0;
    return 1;
}

static inline long long gear_board_num(const char *line, int i, long long dflt) {
    char tmp[32];
    if (!gear_board_field(line, i, tmp, sizeof tmp) || tmp[0] == 0) return dflt;
    char *end = NULL;
    long long v = strtoll(tmp, &end, 10);
    return (end && end != tmp) ? v : dflt;
}

static inline int gear_board_char_index(const GearBoard *b, const char *name) {
    for (int i = 0; i < b->nchars; i++) if (strcmp(b->chars[i].name, name) == 0) return i;
    return -1;
}

/* Parse a whole board. Returns 0, or -1 when the text is not a gear board (the board is left empty). */
static inline int gear_board_parse(GearBoard *b, const char *text) {
    b->nchars = b->ngear = b->nneeds = 0;
    b->loaded = 0;
    if (!text || strncmp(text, "SHANKPIT_GEAR_BOARD\t", 20) != 0) return -1;
    b->generated = gear_board_num(text, 2, 0);
    const char *line = text;
    while (line && *line) {
        const char *next = strchr(line, '\n');
        char kind[4];
        gear_board_field(line, 0, kind, sizeof kind);
        if (strcmp(kind, "C") == 0 && b->nchars < GB_MAX_CHARS) {
            GearBoardChar *c = &b->chars[b->nchars++];
            memset(c, 0, sizeof *c);
            gear_board_field(line, 1, c->name, sizeof c->name);
            gear_board_field(line, 2, c->class_, sizeof c->class_);
            c->level = (int)gear_board_num(line, 3, 0);
            gear_board_field(line, 4, c->role, sizeof c->role);
            c->gold = gear_board_num(line, 5, 0);
            gear_board_field(line, 6, c->zone, sizeof c->zone);
            gear_board_field(line, 7, c->subzone, sizeof c->subzone);
            c->bags_free = (int)gear_board_num(line, 8, -1);
            c->bags_total = (int)gear_board_num(line, 9, -1);
            c->bags_worn = (int)gear_board_num(line, 10, -1);
            c->bag_slots = (int)gear_board_num(line, 11, -1);
            c->ilvl = (int)gear_board_num(line, 12, 0);
            c->xp_pct = (int)gear_board_num(line, 13, -1);
        } else if (strcmp(kind, "G") == 0 && b->ngear < GB_MAX_GEAR) {
            char who[48];
            gear_board_field(line, 1, who, sizeof who);
            int ch = gear_board_char_index(b, who);
            if (ch >= 0) {
                GearBoardItem *g = &b->gear[b->ngear++];
                g->ch = ch;
                g->slot = (int)gear_board_num(line, 2, 0);
                gear_board_field(line, 3, g->slot_name, sizeof g->slot_name);
                gear_board_field(line, 4, g->item, sizeof g->item);
                g->ilvl = (int)gear_board_num(line, 5, -1);
                g->quality = (int)gear_board_num(line, 6, -1);
            }
        } else if (strcmp(kind, "N") == 0 && b->nneeds < GB_MAX_NEEDS) {
            GearBoardNeed *n = &b->needs[b->nneeds++];
            n->urgency = (int)gear_board_num(line, 1, 0);
            gear_board_field(line, 2, n->character, sizeof n->character);
            gear_board_field(line, 3, n->kind, sizeof n->kind);
            gear_board_field(line, 4, n->what, sizeof n->what);
            gear_board_field(line, 5, n->detail, sizeof n->detail);
            gear_board_field(line, 6, n->from, sizeof n->from);
        }
        line = next ? next + 1 : NULL;
    }
    b->loaded = 1;
    return 0;
}

/* Where the board is: $SHANKPIT_GEAR_BOARD, else $FORGE_DIR/board.tsv; empty when neither is set. */
static inline void gear_board_find_path(GearBoard *b) {
    const char *p = getenv("SHANKPIT_GEAR_BOARD");
    const char *dir = getenv("FORGE_DIR");
    if (p && *p) snprintf(b->path, sizeof b->path, "%s", p);
    else if (dir && *dir) snprintf(b->path, sizeof b->path, "%s/board.tsv", dir);
    else b->path[0] = 0;
}

/* Read the file now. Returns 0 when a board was loaded. */
static inline int gear_board_load(GearBoard *b) {
    if (!b->path[0]) {
        snprintf(b->status, sizeof b->status, "SET FORGE_DIR (OR SHANKPIT_GEAR_BOARD) AND RUN: scripts/forge.sh watch");
        return -1;
    }
    FILE *f = fopen(b->path, "rb");
    if (!f) {
        snprintf(b->status, sizeof b->status, "NO BOARD YET AT %.100s -- RUN: scripts/forge.sh board", b->path);
        b->loaded = 0;
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || n > (8L << 20)) { fclose(f); snprintf(b->status, sizeof b->status, "BOARD FILE TOO LARGE"); return -1; }
    char *text = (char *)malloc((size_t)n + 1);
    if (!text) { fclose(f); return -1; }
    size_t got = fread(text, 1, (size_t)n, f);
    fclose(f);
    text[got] = 0;
    int rc = gear_board_parse(b, text);
    free(text);
    if (rc != 0) snprintf(b->status, sizeof b->status, "%.100s IS NOT A GEAR BOARD", b->path);
    else b->status[0] = 0;
    return rc;
}

/* Re-read the board if the forge has rewritten it. Cheap: a stat at most once a second. Returns 1 when it reloaded. */
static inline int gear_board_poll(GearBoard *b, unsigned now_ms) {
    if (b->last_check_ms && now_ms - b->last_check_ms < 1000) return 0;
    b->last_check_ms = now_ms ? now_ms : 1;
    if (!b->path[0]) gear_board_find_path(b);
    struct stat st;
    if (!b->path[0] || stat(b->path, &st) != 0) {
        if (b->loaded || !b->status[0]) gear_board_load(b);   /* sets the status line */
        return 0;
    }
    if (b->loaded && (long long)st.st_mtime == b->mtime && (long long)st.st_size == b->size) return 0;
    b->mtime = (long long)st.st_mtime;
    b->size = (long long)st.st_size;
    return gear_board_load(b) == 0;
}

/* Rows per page on the roster and needs pages; each character then gets a page of its own (its gear). */
#define GB_ROSTER_PER_PAGE 16
#define GB_NEEDS_PER_PAGE 9

#endif
