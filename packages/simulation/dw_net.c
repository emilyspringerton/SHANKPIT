/* dw_net.c -- see dw_net.h. The producer half of the internet-of-SHANKPIT shim: a pure JSON export
 * of the ladder, and the file write that the HTTP daemon (tools/shankpit_netd.c) serves. */

#include "dw_net.h"
#include "citizen_life.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifdef _WIN32
  #include <direct.h>
  #define dw_mkdir(p) _mkdir(p)
#else
  #include <sys/stat.h>
  #include <sys/types.h>
  #define dw_mkdir(p) mkdir((p), 0775)
#endif

static const char *(*g_name_lookup)(int pid);
static const DwStage *g_stage;
static char g_path[1024];
static int g_path_ready;
static unsigned g_last_write_ms;
static int g_have_last_write;
static int g_dirty;

static DwNetMatch g_recent[DW_NET_MAX_RECENT];
static int g_recent_head;   /* next slot to write */
static int g_recent_count;

void dw_net_set_name_lookup(const char *(*fn)(int pid)) { g_name_lookup = fn; }
const char *(*dw_net_name_lookup(void))(int pid) { return g_name_lookup; }
void dw_net_set_stage(const DwStage *s) { g_stage = s; }
const DwStage *dw_net_stage(void) { return g_stage; }

void dw_net_set_path(const char *path) {
    snprintf(g_path, sizeof g_path, "%s", path ? path : "");
    g_path_ready = 1;
}

const char *dw_net_path(void) {
    if (!g_path_ready) {
        /* one read, on first ask: the env is the switch, and a missing one means "do not publish" */
        const char *env = getenv("SHANKPIT_NET_SNAPSHOT");
        snprintf(g_path, sizeof g_path, "%s", env ? env : "");
        g_path_ready = 1;
    }
    return g_path;
}

void dw_net_reset(void) {
    memset(g_recent, 0, sizeof g_recent);
    g_recent_head = 0;
    g_recent_count = 0;
    g_have_last_write = 0;
    g_dirty = 0;
}

void dw_net_note_match(int kind, int pid0, int pid1, int winner_seat, int rounds,
                       int hull0, int hull1, unsigned now_ms) {
    DwNetMatch *m = &g_recent[g_recent_head];
    m->kind = kind;
    m->pid0 = pid0;
    m->pid1 = pid1;
    m->winner = winner_seat;
    m->rounds = rounds;
    m->hull0 = hull0;
    m->hull1 = hull1;
    m->ms = now_ms;
    g_recent_head = (g_recent_head + 1) % DW_NET_MAX_RECENT;
    if (g_recent_count < DW_NET_MAX_RECENT) g_recent_count++;
    g_dirty = 1;
}

int dw_net_recent(DwNetMatch *out, int max) {
    if (!out || max <= 0) return 0;
    int n = g_recent_count < max ? g_recent_count : max;
    for (int i = 0; i < n; i++) {
        int idx = (g_recent_head - 1 - i + DW_NET_MAX_RECENT * 2) % DW_NET_MAX_RECENT;
        out[i] = g_recent[idx];
    }
    return n;
}

/* ---- JSON ----------------------------------------------------------------------------------- */

typedef struct { char *buf; size_t cap; size_t len; int ok; } Jw;

static void jw_put(Jw *w, const char *fmt, ...) {
    if (!w->ok) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(w->buf + w->len, w->cap - w->len, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= w->cap - w->len) { w->ok = 0; return; }
    w->len += (size_t)n;
}

static void jw_str(Jw *w, const char *s) {
    jw_put(w, "\"");
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
        switch (*p) {
            case '"':  jw_put(w, "\\\""); break;
            case '\\': jw_put(w, "\\\\"); break;
            case '\n': jw_put(w, "\\n"); break;
            case '\r': jw_put(w, "\\r"); break;
            case '\t': jw_put(w, "\\t"); break;
            default:
                if (*p < 0x20) jw_put(w, "\\u%04x", (unsigned)*p);
                else jw_put(w, "%c", (char)*p);
        }
    }
    jw_put(w, "\"");
}

/* The catalogue's three kinds, in the rules' own order. */
static const char *dw_net_kind_name(int kind) {
    switch (kind) {
        case 0: return "OFFENSE";
        case 1: return "OPERATIONS";
        case 2: return "DEFENSE";
        default: return "NONE";
    }
}

int dw_net_export(const DwLadder *l, char *buf, size_t cap, unsigned now_ms) {
    if (!buf || cap == 0) return -1;
    buf[0] = 0;
    if (!l) return -1;
    Jw w = { buf, cap, 0, 1 };

    int citizens = 0, players = 0;
    for (int i = 0; i < DW_LADDER_MAX_CITIZENS; i++) {
        if (!l->members[i].active) continue;
        if (l->members[i].is_player) players++; else citizens++;
    }

    jw_put(&w, "{\"schema\":%d,\"generated_ms\":%u,", DW_NET_SCHEMA, now_ms);
    jw_put(&w, "\"population\":{\"citizens\":%d,\"players\":%d},", citizens, players);

    /* the meta: what the population has actually been playing, which is what every citizen's deck
       is crafted against */
    jw_put(&w, "\"meta\":{\"kind\":%d,\"kind_name\":", dw_ladder_meta_kind(l));
    jw_str(&w, dw_net_kind_name(dw_ladder_meta_kind(l)));
    jw_put(&w, ",\"share_permille\":%d,\"plays\":%ld,\"kinds\":[%d,%d,%d]},",
           dw_ladder_meta_share(l), l->meta_plays,
           l->meta_kind[0], l->meta_kind[1], l->meta_kind[2]);

    /* the ladder itself: every seat, its record, and the sixteen cards it built for itself */
    jw_put(&w, "\"records\":[");
    int first = 1;
    for (int i = 0; i < DW_LADDER_MAX_CITIZENS; i++) {
        const DwLadderMember *m = &l->members[i];
        if (!m->active) continue;
        const char *name = g_name_lookup ? g_name_lookup(m->pid) : "";
        jw_put(&w, "%s{\"pid\":%d,\"name\":", first ? "" : ",", m->pid);
        jw_str(&w, name ? name : "");
        jw_put(&w, ",\"player\":%d,\"hero\":%d,\"path\":", m->is_player,
               (l->hero_pid >= 0 && m->pid == l->hero_pid) ? 1 : 0);
        jw_str(&w, citizen_life_path_name(m->path));
        /* `rating` is the number a leaderboard climbs, and it comes from the SAME field the game ranks on
           (dw_rating.c <- PARENA/stdlib/deadweight/rating.prn), not from a second calculation here. */
        jw_put(&w, ",\"wins\":%d,\"games\":%d,\"rating\":%d,\"winrate_permille\":%d,\"aggression\":%d,\"deck\":[",
               m->wins, m->games, m->rating, dw_ladder_citizen_winrate(l, i), m->aggression);
        for (int c = 0; c < DW_DECK; c++) jw_put(&w, "%s%d", c ? "," : "", m->deck[c]);
        jw_put(&w, "]}");
        first = 0;
    }
    jw_put(&w, "],");

    /* open challenges -- who is waiting on whom, and how long they have left */
    jw_put(&w, "\"challenges\":[");
    first = 1;
    for (int i = 0; i < DW_LADDER_MAX_CHALLENGES; i++) {
        const DwChallenge *c = &l->challenges[i];
        if (!c->active) continue;
        int left = (int)(c->expires_ms - now_ms);
        if (left < 0) left = 0;
        jw_put(&w, "%s{\"id\":%u,\"from_pid\":%d,\"to_pid\":%d,\"kind\":", first ? "" : ",",
               c->id, c->from_pid, c->to_pid);
        jw_str(&w, dw_challenge_kind_name(c->kind));
        jw_put(&w, ",\"kind_id\":%d,\"expires_in_ms\":%d}", c->kind, left);
        first = 0;
    }
    jw_put(&w, "],");

    /* finished matches by direction. Sum > 0 is the proof the citizens play without a player. */
    jw_put(&w, "\"results\":[");
    first = 1;
    for (int k = DW_CHAL_CITIZEN_CITIZEN; k <= DW_CHAL_PLAYER_PLAYER; k++) {
        jw_put(&w, "%s{\"kind\":", first ? "" : ",");
        jw_str(&w, dw_challenge_kind_name(k));
        jw_put(&w, ",\"kind_id\":%d,\"matches\":%d}", k, l->results[k]);
        first = 0;
    }
    jw_put(&w, "],");

    /* the live ticker: newest finished match first */
    DwNetMatch rec[DW_NET_MAX_RECENT];
    int n = dw_net_recent(rec, DW_NET_MAX_RECENT);
    jw_put(&w, "\"recent\":[");
    for (int i = 0; i < n; i++) {
        jw_put(&w, "%s{\"kind\":", i ? "," : "");
        jw_str(&w, dw_challenge_kind_name(rec[i].kind));
        jw_put(&w, ",\"pid0\":%d,\"pid1\":%d,\"winner\":%d,\"rounds\":%d,\"hull0\":%d,\"hull1\":%d,\"ms\":%u}",
               rec[i].pid0, rec[i].pid1, rec[i].winner, rec[i].rounds,
               rec[i].hull0, rec[i].hull1, rec[i].ms);
    }
    jw_put(&w, "],");

    /* THE STAGE: the world's own bracket, or an honest null. `results` carries every decided match in
       bracket order (a bye included, with played=0), so a page can draw the whole bracket instead of only
       the game on the table right now. */
    jw_put(&w, "\"stage\":");
    const DwStage *st = g_stage;
    if (!st || !st->active) {
        jw_put(&w, "null");
    } else {
        int round = 0, slot = 0;
        int live = dw_stage_next(st, &round, &slot);
        jw_put(&w,
               "{\"name\":\"THE PIT\",\"round\":%d,\"rounds\":%d,\"entrants\":%d,\"byes\":%d,"
               "\"over\":%d,\"champion\":%d,\"round_name\":",
               st->round, st->rounds, st->nentrants, st->byes, st->over ? 1 : 0,
               st->over ? st->champion : -1);
        jw_str(&w, dw_stage_round_name(st));
        jw_put(&w, ",\"match\":");
        if (live) {
            jw_put(&w, "{\"a_pid\":%d,\"b_pid\":%d}", st->matches[round - 1][slot].a,
                   st->matches[round - 1][slot].b);
        } else {
            jw_put(&w, "null");
        }
        jw_put(&w, ",\"results\":[");
        int first_res = 1;
        for (int r = 1; r <= st->rounds && r <= DW_STAGE_MAX_ROUNDS; r++) {
            int n = (st->slots >> (r - 1)) / 2;
            for (int i = 0; i < n && i < DW_STAGE_MAX_MATCHES; i++) {
                const DwStageMatch *m = &st->matches[r - 1][i];
                if (m->winner < 0 || m->a < 0 || m->b < 0) continue;
                jw_put(&w, "%s{\"round\":%d,\"a_pid\":%d,\"b_pid\":%d,\"winner_pid\":%d,\"played\":%d}",
                       first_res ? "" : ",", r, m->a, m->b, m->winner, m->played);
                first_res = 0;
            }
        }
        jw_put(&w, "]}");
    }
    jw_put(&w, "}");

    return w.ok ? (int)w.len : -1;
}

/* ---- file ----------------------------------------------------------------------------------- */

/* ensure_parent -- create the directory part of `path`, one level deep. "var/deadweight/net.json"
 * gives "var/deadweight"; a bare file name has no parent and needs nothing. Not recursive on
 * purpose: the caller controls the path, and silently mkdir-ing a deep tree from a game tick is how
 * a stray SHANKPIT_NET_SNAPSHOT turns into junk on disk. */
static void ensure_parent(const char *path) {
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
#ifdef _WIN32
    char *bslash = strrchr(dir, '\\');
    if (!slash || (bslash && bslash > slash)) slash = bslash;
#endif
    if (!slash || slash == dir) return;
    *slash = 0;
    dw_mkdir(dir);
}

int dw_net_write(const DwLadder *l, const char *path, unsigned now_ms) {
    if (!path || !*path || !l) return 0;
    size_t cap = 4096 + (size_t)DW_LADDER_MAX_CITIZENS * (DW_DECK * 5 + 256);
    char *json = (char *)malloc(cap);
    if (!json) return 0;
    int len = dw_net_export(l, json, cap, now_ms);
    if (len < 0) { free(json); return 0; }

    ensure_parent(path);
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) { free(json); return 0; }
    size_t wrote = fwrite(json, 1, (size_t)len, f);
    int ok = (wrote == (size_t)len);
    if (fclose(f) != 0) ok = 0;
    free(json);
    if (!ok) { remove(tmp); return 0; }
    remove(path);
    if (rename(tmp, path) != 0) { remove(tmp); return 0; }
    return 1;
}

int dw_net_publish(const DwLadder *l, unsigned now_ms) {
    const char *path = dw_net_path();
    if (!path || !*path || !l) return 0;
    unsigned gap = g_have_last_write ? (now_ms - g_last_write_ms) : DW_NET_PUBLISH_INTERVAL_MS;
    /* a finished match wants out promptly; everything else waits for the heartbeat */
    if (gap < (g_dirty ? DW_NET_MIN_INTERVAL_MS : DW_NET_PUBLISH_INTERVAL_MS)) return 0;
    if (!dw_net_write(l, path, now_ms)) return 0;
    g_last_write_ms = now_ms;
    g_have_last_write = 1;
    g_dirty = 0;
    return 1;
}
