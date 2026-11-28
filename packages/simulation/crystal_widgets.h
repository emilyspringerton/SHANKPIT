#ifndef SHANKPIT_CRYSTAL_WIDGETS_H
#define SHANKPIT_CRYSTAL_WIDGETS_H
/* crystal_widgets.h -- telecrystals as NOCK widgets (machine_type 9, LevelEduWidget dest_zone/cost/cast_*_ms/prompt).
 *
 * A level lists its crystals in edu_widgets like any other widget; this module collects them into the TcCrystal table packages/common/telecrystal.h
 * casts over (shared WORLDMAP rules), keeping the widget's metadata beside it for the HUD and for the request the client sends the GFD server.
 * GFD (server/telecrystal) owns the registry: the client PREDICTS the cast and the server decides whether to honour cost and destination.
 * Pure and headless: no SDL, no GL. The lobby feeds it the player position, the G edge and the clock, and draws the rings from crystal_widgets_ring().
 */
#include "level_boxes.h"
#include "telecrystal.h"

#define CRYSTAL_WIDGETS_MAX 16
typedef struct {
    TcCrystal cast[CRYSTAL_WIDGETS_MAX];
    int dest_zone[CRYSTAL_WIDGETS_MAX], dest_spawner[CRYSTAL_WIDGETS_MAX], cost[CRYSTAL_WIDGETS_MAX];
    char id[CRYSTAL_WIDGETS_MAX][32];
    char prompt[CRYSTAL_WIDGETS_MAX][LEVEL_BOXES_CRYSTAL_PROMPT_LEN];
    int n;
    TcState st;
} CrystalWidgets;

/* A crystal with no cast times is given the lobby's old defaults (1000 total / 600 commit); commit is never after total. */
static inline void crystal_widgets_load(CrystalWidgets *cw, const CustomLevelData *lvl) {
    memset(cw, 0, sizeof *cw);
    for (int i = 0; i < lvl->edu_widget_count && cw->n < CRYSTAL_WIDGETS_MAX; i++) {
        const LevelEduWidget *w = &lvl->edu_widgets[i];
        if (w->machine_type != 9) continue;
        int k = cw->n++;
        int total = w->cast_total_ms > 0 ? w->cast_total_ms : 1000, commit = w->cast_commit_ms > 0 ? w->cast_commit_ms : 600;
        if (commit > total) commit = total;
        cw->cast[k].x = w->x; cw->cast[k].z = w->z; cw->cast[k].r = w->radius > 0 ? w->radius : 10.0f;
        cw->cast[k].total_ms = total; cw->cast[k].commit_ms = commit;
        cw->dest_zone[k] = w->dest_zone; cw->dest_spawner[k] = w->dest_spawner; cw->cost[k] = w->cost;
        snprintf(cw->id[k], sizeof cw->id[k], "%s", w->entity_id);
        snprintf(cw->prompt[k], sizeof cw->prompt[k], "%s", w->prompt[0] ? w->prompt : "G: TELEPORT");
    }
    tc_init(&cw->st);
}

/* What the client should do this frame. On TC_JUMP, *crystal is the index whose dest_zone / dest_spawner / cost go in the request to the server. */
static inline TcEvent crystal_widgets_tick(CrystalWidgets *cw, float px, float pz, int g_edge, unsigned now_ms, int *crystal) {
    return tc_tick(&cw->st, cw->cast, cw->n, px, pz, g_edge, now_ms, crystal);
}

/* The crystal whose "G: ..." prompt to show (the one in reach, or the one being cast), or -1. */
static inline int crystal_widgets_prompt(const CrystalWidgets *cw, float px, float pz) {
    if (cw->st.phase == TC_CASTING) return cw->st.crystal;
    return tc_near(cw->cast, cw->n, px, pz);
}
#endif
