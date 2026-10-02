#ifndef EDIT_SYNC_H
#define EDIT_SYNC_H
/* edit_sync.h -- SHANKPIT's side of a live NOCK edit session (kanban #518/#519/#520/#531, EDIT-3).
 * Talks to IDUNA's /api/v1/shankpit-edit-sessions (IDUNA/internal/http/handlers/shankpit_edit_sessions.go)
 * over the same curl-popen transport level_boxes.h uses. Blocking calls: run them on a worker
 * thread. Pure parsing (edit_sync_parse) is separate so it unit-tests without a network. */
#include "level_boxes.h"

#define EDIT_SYNC_MAX_EVENTS 64

typedef struct {
    int seq;
    int from_nock;        /* source == "nock" */
    char kind[16];
    float x, y, z, yaw, pitch;
    int has_pos;
    int mode_crosshair;   /* spawn_mode: 1 = crosshair, 0 = spawner */
} EditSyncEvent;

/* Parse a GET poll response into events + the session's latest seq. Returns events parsed. */
static inline int edit_sync_parse(const char *json, EditSyncEvent *out, int max, int *out_seq) {
    int n = 0;
    const char *end = json + strlen(json);
    const char *p = json;
    while (n < max && (p = strstr(p, "{\"seq\":")) != NULL) {
        const char *next = strstr(p + 1, "{\"seq\":");
        const char *seg_end = next ? next : end;
        EditSyncEvent *e = &out[n];
        memset(e, 0, sizeof *e);
        float f = 0;
        level_boxes_parse_number(p + 7, &f); e->seq = (int)f;
        char src[16] = "";
        const char *v = level_boxes_find_key(p, seg_end, "source");
        if (v) level_boxes_parse_string(v, src, sizeof src);
        e->from_nock = strcmp(src, "nock") == 0;
        v = level_boxes_find_key(p, seg_end, "kind");
        if (v) level_boxes_parse_string(v, e->kind, sizeof e->kind);
        v = level_boxes_find_key(p, seg_end, "x");
        if (v && level_boxes_parse_number(v, &e->x)) {
            e->has_pos = 1;
            if ((v = level_boxes_find_key(p, seg_end, "y"))) level_boxes_parse_number(v, &e->y);
            if ((v = level_boxes_find_key(p, seg_end, "z"))) level_boxes_parse_number(v, &e->z);
            if ((v = level_boxes_find_key(p, seg_end, "yaw"))) level_boxes_parse_number(v, &e->yaw);
            if ((v = level_boxes_find_key(p, seg_end, "pitch"))) level_boxes_parse_number(v, &e->pitch);
        }
        char mode[16] = "";
        if ((v = level_boxes_find_key(p, seg_end, "mode"))) level_boxes_parse_string(v, mode, sizeof mode);
        e->mode_crosshair = strcmp(mode, "crosshair") == 0;
        n++;
        p = seg_end;
    }
    /* the response's own top-level "seq" is the last "seq" key (Go marshals map keys sorted) */
    if (out_seq) {
        const char *last = NULL, *q = json;
        while ((q = strstr(q, "\"seq\":")) != NULL) { last = q; q++; }
        float f = 0;
        if (last && level_boxes_parse_number(last + 6, &f)) *out_seq = (int)f;
    }
    return n;
}

/* POST helper: body -> url. Optionally captures the response (malloc'd, caller frees). */
static inline int edit_sync_http(const char *url, const char *body, char **resp) {
    char cmd[640];
    snprintf(cmd, sizeof cmd, "curl -s -f --max-time 30 -X POST -H \"Content-Type: application/json\" --data-binary @- \"%s\"", url);
    FILE *p = (body) ? level_boxes_popen_write(cmd) : NULL;
    (void)resp;
    if (!p) return 0;
    fwrite(body, 1, strlen(body), p);
    int st = LEVEL_BOXES_PCLOSE(p);
#ifdef _WIN32
    return st == 0;
#else
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
#endif
}

/* Create a session; writes the 32-hex id into out_id (>=33 bytes). Needs the response, so it
 * reads via level_boxes_fetch_url-style popen with POST. */
static inline int edit_sync_create(const char *base, int level_id, const char *level_name, char *out_id) {
    char cmd[768];
    snprintf(cmd, sizeof cmd,
             "curl -s -f --max-time 10 -X POST -H \"Content-Type: application/json\" -d \"{\\\"level_id\\\":%d,\\\"level_name\\\":\\\"%.60s\\\"}\" \"%s\"",
             level_id, level_name, base);
    FILE *p = level_boxes_popen_read(cmd);
    if (!p) return 0;
    char buf[512]; size_t n = fread(buf, 1, sizeof buf - 1, p); buf[n] = 0;
    LEVEL_BOXES_PCLOSE(p);
    const char *v = level_boxes_find_key(buf, buf + n, "id");
    char id[48] = "";
    if (!v || !level_boxes_parse_string(v, id, sizeof id) || strlen(id) != 32) return 0;
    memcpy(out_id, id, 33);
    return 1;
}

static inline int edit_sync_post_event(const char *base, const char *id, const char *kind, const char *data_json) {
    char url[320], body[640];
    snprintf(url, sizeof url, "%s/%s/events", base, id);
    snprintf(body, sizeof body, "{\"source\":\"shankpit\",\"kind\":\"%s\",\"data\":%s}", kind, data_json);
    return edit_sync_http(url, body, NULL);
}

/* Long-poll for events after `since`. Returns events parsed (>=0), -1 on transport error / 404. */
static inline int edit_sync_poll(const char *base, const char *id, int since, int wait_s, EditSyncEvent *out, int max, int *out_seq) {
    char url[320]; char *buf = NULL;
    snprintf(url, sizeof url, "%s/%s?since=%d&wait=%d", base, id, since, wait_s);
    long n = level_boxes_fetch_url(url, &buf);
    if (n < 0) return -1;
    int c = edit_sync_parse(buf, out, max, out_seq);
    free(buf);
    return c;
}
#endif
