/* packages/collection/ascend.c -- plumbing for /ascend; see ascend.h. Decisions are ascend_rules.c (PARENA). */
#include "ascend.h"
#include "../reflux/reflux_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ascend_rules.c (generated from PARENA) */
int asc_accepts(int);
int asc_role(int);
int asc_open_max(void);
int asc_code_char_ok(int);
int asc_code_len_ok(int);
int asc_act_opened(void);

static void copy_name(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static const char *name_of(const Ascend *a, int id) { return (id >= 1 && id <= a->nnames) ? a->names[id] : ""; }

static int code_ok(const char *code) {
    int n = (int)strlen(code);
    if (!asc_code_len_ok(n)) return 0;
    for (int i = 0; i < n; i++) if (!asc_code_char_ok((unsigned char)code[i])) return 0;
    return 1;
}

void ascend_init(Ascend *a, const char *path) {
    memset(a, 0, sizeof(*a));
    a->path = path ? path : getenv("COLLECTION_GUILD_FEED");
    if (a->path && !a->path[0]) a->path = NULL;
    a->cursor_abs = reflux_host_total_dispatched();
}

static AscendGuild *find_guild(Ascend *a, const char *name) {
    for (int i = 0; i < a->ng; i++) if (!strcmp(a->g[i].name, name)) return &a->g[i];
    return NULL;
}

/* The mod: read every new feed row from REFLUX into the model. */
static void poll_rows(Ascend *a, AscendGuild *keep, int nkeep) {
    int total = reflux_host_total_dispatched(), oldest = reflux_host_oldest_abs();
    if (a->cursor_abs < oldest) a->cursor_abs = oldest;
    for (; a->cursor_abs < total; a->cursor_abs++) {
        int i = reflux_host_index_for_abs(a->cursor_abs);
        int t = reflux_host_action_type_at(i);
        if (!asc_accepts(t)) continue;
        int ra = reflux_host_action_a_at(i), rb = reflux_host_action_b_at(i);
        int role = asc_role(t);
        if (role == 1 && a->ng < ASCEND_MAX_GUILDS && name_of(a, ra)[0]) {
            AscendGuild *g = &a->g[a->ng++];
            memset(g, 0, sizeof(*g));
            copy_name(g->name, sizeof(g->name), name_of(a, ra));
            copy_name(g->realm, sizeof(g->realm), name_of(a, rb));
        } else if (role == 2) {
            AscendGuild *g = find_guild(a, name_of(a, ra));
            const char *code = name_of(a, rb);
            if (g && g->ncodes < ASCEND_MAX_CODES && code_ok(code)) {
                copy_name(g->codes[g->ncodes], sizeof(g->codes[0]), code);
                /* an invite already opened this session stays opened across reloads */
                for (int k = 0; k < nkeep; k++)
                    if (!strcmp(keep[k].name, g->name))
                        for (int c = 0; c < keep[k].ncodes; c++)
                            if (!strcmp(keep[k].codes[c], code)) g->opened[g->ncodes] = keep[k].opened[c];
                g->ncodes++;
            }
        } else if (role == 3) {
            a->guilds_total = ra;
            a->invites_total = rb;
            a->loaded = 1;
        }
    }
}

int ascend_load_text(Ascend *a, const char *text) {
    if (strncmp(text, "collection-feed 1\n", 18) != 0) return -1;
    static AscendGuild keep[ASCEND_MAX_GUILDS];
    int nkeep = a->ng;
    memcpy(keep, a->g, sizeof(AscendGuild) * (size_t)nkeep);
    a->nnames = 0;
    a->ng = 0;
    a->loaded = 0;
    a->cursor_abs = reflux_host_total_dispatched();
    int batch = 0;
    const char *p = text + 18;
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        char line[ASCEND_NAME + 32];
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';
        if (len && line[len - 1] == '\r') line[len - 1] = '\0';
        int id, t, ra, rb, rc, off = 0;
        if (sscanf(line, "name %d %n", &id, &off) == 1 && off > 0) {
            if (id >= 1 && id <= ASCEND_MAX_NAMES) {
                copy_name(a->names[id], sizeof(a->names[id]), line + off);
                if (id > a->nnames) a->nnames = id;
            }
        } else if (sscanf(line, "row %d %d %d %d", &t, &ra, &rb, &rc) == 4) {
            reflux_host_dispatch(t, ra, rb, rc);
            if (++batch >= 64) { poll_rows(a, keep, nkeep); batch = 0; }   /* drain well inside the 256-row ring */
        }
        p = end ? end + 1 : p + len;
    }
    poll_rows(a, keep, nkeep);
    return a->loaded ? 1 : -1;
}

int ascend_load(Ascend *a) {
    if (!a->path) return 0;
    FILE *f = fopen(a->path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long size = ftell(f);
    rewind(f);
    if (size < 0 || size > 4 * 1024 * 1024) { fclose(f); return -1; }
    char *text = malloc((size_t)size + 1);
    if (!text) { fclose(f); return -1; }
    size_t got = fread(text, 1, (size_t)size, f);
    fclose(f);
    text[got] = '\0';
    int r = ascend_load_text(a, text);
    free(text);
    return r;
}

int ascend_command(Ascend *a, const char *args, char out[][ASCEND_LINE], int *tones, int max,
                   void (*open_url)(const char *url)) {
    int n = 0;
#define LINE(...) do { if (n < max) { snprintf(out[n], ASCEND_LINE, __VA_ARGS__); if (tones) tones[n] = 5; n++; } } while (0)
    while (args && *args == ' ') args++;
    int join = args && !strncmp(args, "join", 4);
    int r = ascend_load(a);
    if (r == 0) {
        LINE("Ascend: set COLLECTION_GUILD_FEED to the file PARENA's scripts/collection-guild-feed.sh writes.");
        return n;
    }
    if (r < 0) {
        LINE("Ascend: no guild feed yet. Log out of WoW, then run PARENA's scripts/collection-guild-feed.sh.");
        return n;
    }
    if (!join) {
        LINE("Ascend: %d guilds, %d Discord invites from your WoW guilds' messages of the day.", a->guilds_total, a->invites_total);
        for (int i = 0; i < a->ng; i++) {
            const AscendGuild *g = &a->g[i];
            if (!g->ncodes) { LINE("  <%s> (%s): no Discord link in its messages", g->name, g->realm); continue; }
            char codes[ASCEND_LINE] = "";
            size_t used = 0;
            for (int c = 0; c < g->ncodes; c++) {
                int w = snprintf(codes + used, sizeof(codes) - used, "%sdiscord.gg/%s", c ? " " : "", g->codes[c]);
                if (w < 0 || (size_t)w >= sizeof(codes) - used) break;
                used += (size_t)w;
            }
            LINE("  <%s> (%s): %s", g->name, g->realm, codes);
        }
        if (a->invites_total) LINE("Type /ascend join to open them in Discord, %d at a time; Discord shows any you're already in.", asc_open_max());
        return n;
    }
    int opened = 0, left = 0;
    for (int i = 0; i < a->ng; i++) {
        AscendGuild *g = &a->g[i];
        for (int c = 0; c < g->ncodes; c++) {
            if (g->opened[c]) continue;
            if (opened >= asc_open_max()) { left++; continue; }
            if (!code_ok(g->codes[c])) continue;
            char url[96];
            snprintf(url, sizeof(url), "https://discord.gg/%s", g->codes[c]);
            if (open_url) open_url(url);
            g->opened[c] = 1;
            opened++;
            LINE("Opening discord.gg/%s for <%s>", g->codes[c], g->name);
        }
    }
    if (opened) reflux_host_dispatch(asc_act_opened(), opened, 0, 0);
    if (!opened) LINE("Ascend: every invite has been opened this session.");
    else if (left) LINE("%d more: /ascend join again.", left);
#undef LINE
    return n;
}
