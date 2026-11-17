/* packages/collection/collection_overlay.c -- plumbing for the Collection overlay mod; see collection_overlay.h.
 * Decisions are collection_overlay_rules.c (PARENA). */
#include "collection_overlay.h"
#include "../reflux/reflux_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* collection_overlay_rules.c (generated from PARENA) */
int co_accepts(int);
int co_role(int);
int co_tone(int);
int co_rank(int);
int co_banner(int, int);
int co_surface_stream(void);
int co_surface_terminal(void);
int co_surface_apps(void);
int co_surface_lines(int);
int co_reload_ms(void);
int co_banner_ms(void);

#define FEED_BATCH 64   /* rows dispatched before the overlay drains REFLUX; well under the 256-row ring */

static const char *KIND[] = { "?", "vendor buy", "vendor sell", "buyback", "repair", "AH scan", "AH buy", "AH post",
                              "trade", "player mail", "AH/NPC mail", "sent mail", "guild bank withdraw",
                              "guild bank deposit", "loot", "quest" };

static void copy_name(char *dst, const char *src) {
    size_t n = strlen(src);
    if (n >= COLLECTION_NAME_LEN) n = COLLECTION_NAME_LEN - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static const char *name_of(const CollectionOverlay *o, int id) {
    return (id >= 1 && id <= o->nnames) ? o->names[id] : "?";
}

void collection_overlay_init(CollectionOverlay *o) {
    memset(o, 0, sizeof(*o));
    o->cursor_abs = reflux_host_total_dispatched();
}

static int prev_status(const CollectionModel *m, const char *name) {
    for (int i = 0; i < m->nchars; i++) if (strcmp(m->chars[i].name, name) == 0) return m->chars[i].status;
    return -1;
}

static int by_rank(const void *a, const void *b) {
    const CollectionChar *x = a, *y = b;
    int rx = co_rank(x->status), ry = co_rank(y->status);
    if (rx != ry) return rx - ry;
    return strcmp(x->name, y->name);
}

static void apply_row(CollectionOverlay *o, int type, int a, int b, int c, unsigned int now_ms) {
    CollectionModel *p = &o->pending;
    switch (co_role(type)) {
    case 1:
        if (p->nchars < COLLECTION_MAX_CHARS) {
            CollectionChar *ch = &p->chars[p->nchars++];
            memset(ch, 0, sizeof(*ch));
            copy_name(ch->name, name_of(o, a));
            ch->status = b; ch->n = c; ch->tone = co_tone(b);
        }
        break;
    case 2:
        for (int i = 0; i < p->nchars; i++)
            if (strcmp(p->chars[i].name, name_of(o, a)) == 0) { p->chars[i].broken_kind = b; p->chars[i].broken_at = c; }
        break;
    case 3:
        if (p->nvendors < COLLECTION_MAX_VENDORS) {
            CollectionVendor *v = &p->vendors[p->nvendors++];
            copy_name(v->name, name_of(o, a));
            v->npc = b; v->items = c;
        }
        break;
    case 4: {
        p->total = a; p->self_found = b; p->vendors_found = c;
        qsort(p->chars, (size_t)p->nchars, sizeof(p->chars[0]), by_rank);
        /* a banner for anyone this load shows newly broken */
        for (int i = 0; i < p->nchars; i++) {
            if (o->loads > 0 && co_banner(prev_status(&o->shown, p->chars[i].name), p->chars[i].status)) {
                int k = p->chars[i].broken_kind;
                snprintf(o->banner, sizeof(o->banner), "%s IS NO LONGER SELF-FOUND (%s)", p->chars[i].name,
                         (k > 0 && k < (int)(sizeof(KIND) / sizeof(KIND[0]))) ? KIND[k] : "?");
                o->banner_until_ms = now_ms + (unsigned int)co_banner_ms();
            }
        }
        o->shown = *p;
        memset(p, 0, sizeof(*p));
        o->loads++;
        break;
    }
    default:
        break;
    }
}

void collection_overlay_poll(CollectionOverlay *o, unsigned int now_ms) {
    int total = reflux_host_total_dispatched();
    int oldest = reflux_host_oldest_abs();
    if (o->cursor_abs < oldest) { o->lost += oldest - o->cursor_abs; o->cursor_abs = oldest; }
    for (; o->cursor_abs < total; o->cursor_abs++) {
        int i = reflux_host_index_for_abs(o->cursor_abs);
        int t = reflux_host_action_type_at(i);
        if (!co_accepts(t)) continue;
        apply_row(o, t, reflux_host_action_a_at(i), reflux_host_action_b_at(i), reflux_host_action_c_at(i), now_ms);
    }
}

/* ---- the bridge: feed file -> REFLUX ----------------------------------------------------------------------- */

void collection_bridge_init(CollectionBridge *b, const char *path) {
    memset(b, 0, sizeof(*b));
    b->path = path ? path : getenv("COLLECTION_FEED");
    if (b->path && !b->path[0]) b->path = NULL;
    b->last_size = -1;
}

int collection_bridge_load_text(CollectionBridge *b, CollectionOverlay *o, const char *text, unsigned int now_ms) {
    if (strncmp(text, "collection-feed 1\n", 18) != 0) {
        b->errors++;
        snprintf(b->error, sizeof(b->error), "not a v1 Collection feed");
        return -1;
    }
    o->nnames = 0;
    memset(&o->pending, 0, sizeof(o->pending));
    int rows = 0, batch = 0;
    const char *p = text + 18;
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        char line[COLLECTION_NAME_LEN + 32];
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';
        if (len && line[len - 1] == '\r') line[len - 1] = '\0';
        int id, t, a, bb, c, off = 0;
        if (sscanf(line, "name %d %n", &id, &off) == 1 && off > 0) {
            if (id >= 1 && id <= COLLECTION_MAX_NAMES) {
                copy_name(o->names[id], line + off);
                if (id > o->nnames) o->nnames = id;
            }
        } else if (sscanf(line, "row %d %d %d %d", &t, &a, &bb, &c) == 4) {
            reflux_host_dispatch(t, a, bb, c);
            rows++;
            if (++batch >= FEED_BATCH) { collection_overlay_poll(o, now_ms); batch = 0; }
        }
        p = end ? end + 1 : p + len;
    }
    collection_overlay_poll(o, now_ms);
    b->loads++;
    b->error[0] = '\0';
    return rows;
}

int collection_bridge_tick(CollectionBridge *b, CollectionOverlay *o, unsigned int now_ms) {
    if (!b->path) return 0;
    if ((int)(now_ms - b->next_check_ms) < 0) return 0;
    b->next_check_ms = now_ms + (unsigned int)co_reload_ms();
    struct stat st;
    if (stat(b->path, &st) != 0) { snprintf(b->error, sizeof(b->error), "no feed file yet"); return 0; }
    if ((long)st.st_size == b->last_size && (long long)st.st_mtime == b->last_mtime) return 0;
    FILE *f = fopen(b->path, "rb");
    if (!f) { snprintf(b->error, sizeof(b->error), "cannot open the feed file"); return 0; }
    char *text = malloc((size_t)st.st_size + 1);
    if (!text) { fclose(f); return 0; }
    size_t got = fread(text, 1, (size_t)st.st_size, f);
    fclose(f);
    text[got] = '\0';
    b->last_size = (long)st.st_size;
    b->last_mtime = (long long)st.st_mtime;
    int rows = collection_bridge_load_text(b, o, text, now_ms);
    free(text);
    return rows >= 0;
}

/* ---- the surfaces -------------------------------------------------------------------------------------------- */

static const char *status_word(int s) {
    return s == 2 ? "self-found since level 1" : s == 1 ? "self-found since tracking" : "broken";
}

static int off_line(const CollectionBridge *b, char out[][COLLECTION_LINE_LEN], int *tones, int max) {
    if (max < 1) return 0;
    if (!b || !b->path) snprintf(out[0], COLLECTION_LINE_LEN, "Collection feed off (set COLLECTION_FEED)");
    else snprintf(out[0], COLLECTION_LINE_LEN, "Collection feed: %.70s", b->error[0] ? b->error : "waiting");
    if (tones) tones[0] = 0;
    return 1;
}

int collection_stream_lines(const CollectionOverlay *o, const CollectionBridge *b, unsigned int now_ms,
                            char out[][COLLECTION_LINE_LEN], int *tones, int max) {
    if (o->loads == 0) return off_line(b, out, tones, max);
    int n = 0;
    if (n < max && o->banner[0] && (int)(o->banner_until_ms - now_ms) > 0) {
        snprintf(out[n], COLLECTION_LINE_LEN, "%s", o->banner);
        if (tones) tones[n] = 3;
        n++;
    }
    if (n < max) {
        snprintf(out[n], COLLECTION_LINE_LEN, "SELF FOUND %d/%d  vendors %d", o->shown.self_found, o->shown.total,
                 o->shown.vendors_found);
        if (tones) tones[n] = 0;
        n++;
    }
    int lim = co_surface_lines(co_surface_stream());
    for (int i = 0; i < o->shown.nchars && i < lim && n < max; i++, n++) {
        const CollectionChar *c = &o->shown.chars[i];
        snprintf(out[n], COLLECTION_LINE_LEN, "%s  %s", c->name, c->status == 0 ? "broken" : "self-found");
        if (tones) tones[n] = c->tone;
    }
    return n;
}

int collection_terminal(const CollectionOverlay *o, const CollectionBridge *b, const char *cmd,
                        char out[][COLLECTION_LINE_LEN], int *tones, int max) {
    while (*cmd == ' ') cmd++;
    int n = 0;
#define TLINE(tone_, ...) do { if (n < max) { snprintf(out[n], COLLECTION_LINE_LEN, __VA_ARGS__); if (tones) tones[n] = (tone_); n++; } } while (0)
    if (strcmp(cmd, "sf help") == 0 || strcmp(cmd, "help") == 0) {
        TLINE(0, "sf           every character: self-found or broken, and why");
        TLINE(0, "sf vendors   vendors found, with npc id and item count");
        return n;
    }
    if (o->loads == 0) return off_line(b, out, tones, max);
    int lim = co_surface_lines(co_surface_terminal());
    if (strcmp(cmd, "sf vendors") == 0) {
        TLINE(0, "%d vendors found", o->shown.vendors_found);
        for (int i = 0; i < o->shown.nvendors && i < lim; i++)
            TLINE(0, "  %s (npc %d): %d items", o->shown.vendors[i].name, o->shown.vendors[i].npc, o->shown.vendors[i].items);
        return n;
    }
    TLINE(0, "%d characters, %d self-found, %d vendors found", o->shown.total, o->shown.self_found, o->shown.vendors_found);
    for (int i = 0; i < o->shown.nchars && i < lim; i++) {
        const CollectionChar *c = &o->shown.chars[i];
        int k = c->broken_kind;
        if (c->status == 0)
            TLINE(c->tone, "  %s: broken by %s, %d transactions", c->name,
                  (k > 0 && k < (int)(sizeof(KIND) / sizeof(KIND[0]))) ? KIND[k] : "?", c->n);
        else
            TLINE(c->tone, "  %s: %s, %d transactions", c->name, status_word(c->status), c->n);
    }
    if (o->shown.nchars > lim) TLINE(0, "  ... and %d more", o->shown.nchars - lim);
#undef TLINE
    return n;
}

static int html_escape(char *dst, int cap, const char *s) {
    int n = 0;
    for (; *s; s++) {
        const char *r = NULL;
        char one[2] = { *s, 0 };
        switch (*s) { case '<': r = "&lt;"; break; case '>': r = "&gt;"; break; case '&': r = "&amp;"; break;
                      case '"': r = "&quot;"; break; default: r = one; }
        int len = (int)strlen(r);
        if (n + len >= cap) return -1;
        memcpy(dst + n, r, (size_t)len);
        n += len;
    }
    dst[n] = '\0';
    return n;
}

int collection_apps_html(const CollectionOverlay *o, const CollectionBridge *b, char *buf, int cap) {
    static const char *TONE_CLASS[] = { "", "sf", "tr", "br" };
    int n = 0, w;
#define EMIT(...) do { w = snprintf(buf + n, (size_t)(cap - n), __VA_ARGS__); if (w < 0 || w >= cap - n) return -1; n += w; } while (0)
    EMIT("<!doctype html><html><head><meta charset=\"utf-8\"><title>Collection</title><style>"
         "body{background:#14101c;color:#e8e2f0;font-family:sans-serif;margin:16px}"
         "h1{font-size:20px;margin:0 0 8px}p{margin:4px 0}table{border-collapse:collapse;width:100%%}"
         "td,th{padding:4px 8px;text-align:left;border-bottom:1px solid #2c2440}"
         ".sf{color:#5fd36b}.tr{color:#e8b33a}.br{color:#ff6b6b}</style></head><body>");
    EMIT("<h1>Self Found</h1>");
    if (o->loads == 0) {
        EMIT("<p>%s</p></body></html>", (!b || !b->path) ? "The Collection feed is off: set COLLECTION_FEED to the file "
             "PARENA's scripts/collection-sf-feed.sh writes." : "Waiting for the Collection feed.");
        return n;
    }
    EMIT("<p>%d characters, %d self-found, %d vendors found.</p>", o->shown.total, o->shown.self_found, o->shown.vendors_found);
    EMIT("<table><tr><th>Character</th><th>Status</th><th>Transactions</th></tr>");
    int lim = co_surface_lines(co_surface_apps());
    char esc[COLLECTION_NAME_LEN * 6];
    for (int i = 0; i < o->shown.nchars && i < lim; i++) {
        const CollectionChar *c = &o->shown.chars[i];
        if (html_escape(esc, (int)sizeof(esc), c->name) < 0) return -1;
        EMIT("<tr><td>%s</td><td class=\"%s\">%s</td><td>%d</td></tr>", esc,
             (c->tone >= 0 && c->tone <= 3) ? TONE_CLASS[c->tone] : "", status_word(c->status), c->n);
    }
    EMIT("</table><h1>Vendors</h1><table><tr><th>Vendor</th><th>NPC</th><th>Items</th></tr>");
    for (int i = 0; i < o->shown.nvendors && i < lim; i++) {
        if (html_escape(esc, (int)sizeof(esc), o->shown.vendors[i].name) < 0) return -1;
        EMIT("<tr><td>%s</td><td>%d</td><td>%d</td></tr>", esc, o->shown.vendors[i].npc, o->shown.vendors[i].items);
    }
    EMIT("</table></body></html>");
#undef EMIT
    return n;
}
