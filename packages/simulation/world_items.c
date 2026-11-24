/* world_items.c -- see world_items.h. Decisions are PARENA (item_rules.c); this is pool + pockets + message text. */
#include "world_items.h"
#include "item_rules.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define DESK_HEIGHT 0.75f

static WorldItem g_items[WORLD_ITEM_MAX];
static ItemState g_carry[WORLD_CARRY_MAX];
static int g_ncarry = 0, g_held = -1;

void world_items_reset(void) { memset(g_items, 0, sizeof g_items); memset(g_carry, 0, sizeof g_carry); g_ncarry = 0; g_held = -1; }

int world_items_drop(int id, int aux, int aux2, float x, float y, float z) {
    if (!item_valid(id)) return -1;
    for (int i = 0; i < WORLD_ITEM_MAX; i++) if (!g_items[i].active) {
        g_items[i].active = 1; g_items[i].s.id = id;
        g_items[i].s.aux = aux < 0 ? item_init_aux(id) : aux;
        g_items[i].s.aux2 = aux2 < 0 ? item_init_aux2(id) : aux2;
        g_items[i].x = x; g_items[i].y = y; g_items[i].z = z;
        return i;
    }
    return -1;
}
int world_items_active_count(void) { int n = 0; for (int i = 0; i < WORLD_ITEM_MAX; i++) n += g_items[i].active; return n; }
const WorldItem *world_items_get(int slot) { return (slot >= 0 && slot < WORLD_ITEM_MAX) ? &g_items[slot] : NULL; }

static int in_reach_slot(float px, float py, float pz) {
    int best = -1; float best_d2 = 1e30f;
    for (int i = 0; i < WORLD_ITEM_MAX; i++) {
        const WorldItem *w = &g_items[i];
        if (!w->active) continue;
        float r = (float)item_pickup_radius_mm(w->s.id) * 0.001f, hh = (float)item_pickup_height_mm(w->s.id) * 0.001f;
        float dx = w->x - px, dz = w->z - pz, d2 = dx * dx + dz * dz;
        if (d2 <= r * r && fabsf(w->y - py) <= hh && d2 < best_d2) { best = i; best_d2 = d2; }
    }
    return best;
}
int world_items_nearest(float px, float py, float pz) { return in_reach_slot(px, py, pz); }

int world_items_prompt(float px, float py, float pz, int *slot_out) {
    int s = in_reach_slot(px, py, pz);
    if (slot_out) *slot_out = s;
    return s < 0 ? 0 : item_prompt(g_items[s].s.id, g_ncarry);
}

int world_items_pickup(float px, float py, float pz) {
    int s = in_reach_slot(px, py, pz);
    if (s < 0) return -1;
    if (item_can_pickup(g_items[s].s.id, g_ncarry) != 1 || g_ncarry >= WORLD_CARRY_MAX) return -2;
    g_carry[g_ncarry] = g_items[s].s;
    g_items[s].active = 0;
    g_held = g_ncarry++;
    return g_held;
}

int world_items_carried_count(void) { return g_ncarry; }
const ItemState *world_items_carried(int i) { return (i >= 0 && i < g_ncarry) ? &g_carry[i] : NULL; }
int world_items_held(void) { return g_held; }
void world_items_cycle_held(void) { if (g_ncarry > 0) g_held = (g_held + 1) % g_ncarry; }

int world_items_drop_held(float x, float y, float z) {
    if (g_held < 0 || g_held >= g_ncarry) return -1;
    ItemState s = g_carry[g_held];
    int slot = world_items_drop(s.id, s.aux, s.aux2, x, y, z);
    if (slot < 0) return -1;
    for (int i = g_held; i < g_ncarry - 1; i++) g_carry[i] = g_carry[i + 1];
    g_ncarry--;
    g_held = g_ncarry == 0 ? -1 : (g_held >= g_ncarry ? g_ncarry - 1 : g_held);
    return slot;
}

const char *world_item_name(int id) {
    static const char *n[] = {"?", "NOTEBOOK", "PENCIL", "PENCIL SHARPENER", "ERASER", "PEN", "RULER", "SCISSORS"};
    return (id >= 1 && id <= 7) ? n[id] : n[0];
}

void world_item_describe(const ItemState *s, char *out, int cap) {
    if (!s) { snprintf(out, (size_t)cap, "-"); return; }
    switch (s->id) {
    case 1: snprintf(out, (size_t)cap, "NOTEBOOK %d/%d pages (%d pencil, %d pen)", s->aux + s->aux2, item_notebook_pages(), s->aux, s->aux2); break;
    case 2: snprintf(out, (size_t)cap, "PENCIL point %d len %dmm", s->aux, s->aux2); break;
    case 3: snprintf(out, (size_t)cap, "PENCIL SHARPENER shavings %d/20", s->aux); break;
    case 4: snprintf(out, (size_t)cap, "ERASER %d%% left", s->aux); break;
    case 5: snprintf(out, (size_t)cap, "PEN ink %d%%", s->aux); break;
    default: snprintf(out, (size_t)cap, "%s", world_item_name(s->id)); break;
    }
}

static const char *effect_text(int e) {
    switch (e) {
    case 1: return "SHARPENED THE PENCIL";   case 2: return "WROTE A PAGE IN PENCIL";
    case 3: return "WROTE A FAINT PAGE (PENCIL IS DULL)"; case 4: return "WROTE A PAGE IN PEN";
    case 5: return "ERASED A PENCIL PAGE";   case 6: return "EMPTIED THE SHARPENER";
    case 7: return "YOU PAGE THROUGH THE NOTEBOOK"; case 8: return "MEASURED";
    case 9: return "CUT A PAGE OUT";
    case -1: return "PENCIL IS ALREADY SHARP"; case -2: return "SHARPENER IS FULL OF SHAVINGS";
    case -3: return "PENCIL IS TOO SHORT TO SHARPEN"; case -4: return "NOTEBOOK IS FULL";
    case -5: return "PEN IS OUT OF INK"; case -6: return "ERASER IS USED UP";
    case -7: return "PENCIL IS BLUNT - SHARPEN IT"; case -8: return "ONLY PEN ON THE PAGE - RUBBER CANNOT LIFT IT";
    case -9: return "NOTHING TO ERASE OR CUT";
    default: return "NOTHING HAPPENS";
    }
}

/* Try actor a on target b (b may be NULL). On success applies the rules' new state to both and returns the effect. */
static int try_use(ItemState *a, ItemState *b) {
    int tid = b ? b->id : 0, ta = b ? b->aux : 0, tb = b ? b->aux2 : 0;
    int e = item_use_effect(a->id, tid, a->aux, a->aux2, ta, tb);
    if (e <= 0) return e;
    int na = item_use_apply(e, 0, a->aux, a->aux2, ta, tb), nb = item_use_apply(e, 1, a->aux, a->aux2, ta, tb);
    int nta = item_use_apply(e, 2, a->aux, a->aux2, ta, tb), ntb = item_use_apply(e, 3, a->aux, a->aux2, ta, tb);
    a->aux = na; a->aux2 = nb;
    if (b) { b->aux = nta; b->aux2 = ntb; }
    return e;
}

int world_items_use_held(float px, float py, float pz, char *msg, int msg_cap) {
    if (g_held < 0 || g_held >= g_ncarry) { snprintf(msg, (size_t)msg_cap, "NOTHING IN HAND"); return 0; }
    ItemState *h = &g_carry[g_held];
    ItemState *targets[WORLD_CARRY_MAX + 1]; int nt = 0;
    int ws = in_reach_slot(px, py, pz);
    if (ws >= 0) targets[nt++] = &g_items[ws].s;
    for (int i = 0; i < g_ncarry; i++) if (i != g_held) targets[nt++] = &g_carry[i];
    int fail = 0, e = 0;
    for (int i = 0; i < nt && e <= 0; i++) {
        int r = try_use(h, targets[i]);
        if (r > 0) { e = r; break; }
        if (r < 0 && !fail) fail = r;
        r = try_use(targets[i], h);
        if (r > 0) { e = r; break; }
        if (r < 0 && !fail) fail = r;
    }
    if (e <= 0) { int r = try_use(h, NULL); if (r > 0) e = r; else if (r < 0 && !fail) fail = r; }
    if (e <= 0) e = fail;
    if (e == 8) {   /* the ruler reports a real number: the pencil's length if it measured one, else its own 30 cm */
        int len = 300;
        if (h->id == 2) len = h->aux2;
        else for (int i = 0; i < nt; i++) if (targets[i]->id == 2) { len = targets[i]->aux2; break; }
        snprintf(msg, (size_t)msg_cap, "MEASURED %d MM", len);
    } else snprintf(msg, (size_t)msg_cap, "%s", effect_text(e));
    return e;
}

int world_items_seed_desk(float cx, float floor_y, float cz, float *desk_cx, float *desk_top) {
    float dx = cx + 5.0f, top = floor_y + DESK_HEIGHT; int n = 0;
    for (int id = 1; id <= item_count(); id++) {
        float z = cz + ((float)id - 4.0f) * 0.7f;
        if (world_items_drop(id, -1, -1, dx, top, z) >= 0) n++;
    }
    if (desk_cx) *desk_cx = dx;
    if (desk_top) *desk_top = top;
    return n;
}
