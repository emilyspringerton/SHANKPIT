/* world_items_test.c -- SECTION 643 desk items. Hand-derived numbers. Built by `make test-world-items`. */
#include <stdio.h>
#include <string.h>
#include "world_items.h"
#include "item_rules.h"

static int fails = 0;
#define CHECK(c, m) do { if (c) printf("PASS: %s\n", m); else { printf("FAIL: %s\n", m); fails++; } } while (0)

static char msg[96];
static int pick(int id) { /* stand on an item of this id and pick it up */
    for (int i = 0; i < WORLD_ITEM_MAX; i++) { const WorldItem *w = world_items_get(i); if (w && w->active && w->s.id == id) return world_items_pickup(w->x, w->y, w->z) >= 0; }
    return 0;
}
static int hold(int id) { for (int i = 0; i < world_items_carried_count(); i++) { if (world_items_carried(i)->id == id) { while (world_items_held() != i) world_items_cycle_held(); return 1; } } return 0; }
static ItemState *carried_mut(int id) { for (int i = 0; i < world_items_carried_count(); i++) if (world_items_carried(i)->id == id) return (ItemState *)world_items_carried(i); return NULL; }

int main(void) {
    /* --- rules (PARENA) --- */
    CHECK(item_count() == 7 && item_valid(1) && item_valid(7) && !item_valid(0) && !item_valid(8), "ids 1..7 valid");
    CHECK(item_pickup_radius_mm(2) == 1800 && item_pickup_radius_mm(1) == 2200, "pencil is fiddlier to reach than the notebook");
    CHECK(item_can_pickup(2, 7) == 1 && item_can_pickup(2, 8) == -1 && item_can_pickup(99, 0) == 0, "pocket cap 8");
    CHECK(item_prompt(1, 0) == 1 && item_prompt(1, 8) == 2 && item_prompt(0, 0) == 0, "prompts");
    CHECK(item_init_aux(2) == 100 && item_init_aux2(2) == 190 && item_init_aux(3) == 0, "new pencil sharp 100 / 190mm");
    CHECK(item_use_effect(2, 3, 50, 190, 0, 0) == 1, "pencil on sharpener sharpens");
    CHECK(item_use_effect(2, 3, 100, 190, 0, 0) == -1, "sharp pencil refuses");
    CHECK(item_use_effect(2, 3, 50, 19, 0, 0) == -3, "19mm stub too short");
    CHECK(item_use_effect(2, 3, 50, 190, 20, 0) == -2, "full sharpener refuses");
    CHECK(item_use_effect(2, 1, 100, 190, 0, 0) == 2 && item_use_effect(2, 1, 29, 190, 0, 0) == 3 && item_use_effect(2, 1, 0, 190, 0, 0) == -7, "pencil write clear / faint / blunt");
    CHECK(item_use_effect(2, 1, 100, 190, 40, 40) == -4, "80 pages = full notebook");
    CHECK(item_use_effect(5, 1, 100, 0, 0, 0) == 4 && item_use_effect(5, 1, 0, 0, 0, 0) == -5, "pen writes / out of ink");
    CHECK(item_use_effect(4, 1, 100, 0, 3, 0) == 5 && item_use_effect(4, 1, 100, 0, 0, 2) == -8 && item_use_effect(4, 1, 100, 0, 0, 0) == -9 && item_use_effect(4, 1, 0, 0, 3, 0) == -6, "eraser: erase / pen-only / blank / used up");
    CHECK(item_use_effect(7, 1, 0, 0, 0, 0) == -9 && item_use_effect(7, 1, 0, 0, 1, 0) == 9, "scissors cut a written page only");
    CHECK(item_use_effect(1, 3, 0, 0, 0, 0) == 0 && item_use_effect(3, 2, 0, 0, 0, 0) == 0, "no effect for unrelated pairs");
    CHECK(item_use_apply(1, 0, 50, 190, 0, 0) == 100 && item_use_apply(1, 1, 50, 190, 0, 0) == 182 && item_use_apply(1, 2, 50, 190, 3, 0) == 4, "sharpen: point 100, -8mm, +1 shaving");
    CHECK(item_use_apply(2, 0, 100, 190, 0, 0) == 90 && item_use_apply(3, 0, 3, 190, 0, 0) == 0 && item_use_apply(2, 2, 100, 190, 5, 1) == 6, "write erodes the point, +1 pencil page");
    CHECK(item_use_apply(4, 3, 100, 0, 0, 7) == 8 && item_use_apply(5, 2, 90, 0, 4, 1) == 3, "pen page +1; erase -1 page");
    CHECK(item_use_apply(9, 2, 0, 0, 0, 5) == 0 && item_use_apply(9, 3, 0, 0, 0, 5) == 4 && item_use_apply(9, 2, 0, 0, 2, 5) == 1, "cut takes pencil pages first, then pen pages");

    /* --- pool / pockets --- */
    world_items_reset();
    CHECK(world_items_active_count() == 0 && world_items_carried_count() == 0 && world_items_held() == -1, "empty");
    float dcx, top;
    CHECK(world_items_seed_desk(0, 0, 0, &dcx, &top) == 7 && world_items_active_count() == 7 && top > 0.7f && top < 0.8f, "desk seeds all seven, desk top 0.75");
    const WorldItem *w1 = world_items_get(0);
    int slot = -9; CHECK(world_items_prompt(w1->x, w1->y, w1->z, &slot) == 1 && slot >= 0, "prompt 'pick up' when at an item");
    CHECK(world_items_prompt(100, 0, 100, &slot) == 0 && slot == -1, "no prompt far away");
    CHECK(world_items_pickup(100, 0, 100) == -1, "nothing in reach");
    CHECK(world_items_nearest(w1->x, w1->y + 5.0f, w1->z) == -1, "too high above is out of reach");
    CHECK(world_items_pickup(w1->x, w1->y, w1->z) == 0 && world_items_carried(0)->id == 1 && world_items_held() == 0 && world_items_active_count() == 6, "pick up the notebook, held, desk shrinks");
    CHECK(pick(2) && pick(3) && pick(4) && pick(5) && pick(6) && pick(7) && world_items_carried_count() == 7, "pick the rest up");
    CHECK(world_items_drop(1, -1, -1, 0, 0, 0) >= 0 && world_items_drop(1, -1, -1, 50, 0, 50) >= 0 && pick(1) == 1 && world_items_carried_count() == 8, "8th pocket filled");
    CHECK(world_items_pickup(50, 0, 50) == -2, "pockets full -> -2");
    world_items_cycle_held(); CHECK(world_items_held() == 0 || world_items_carried_count() == 8, "cycle wraps");
    for (int i = 0; i < WORLD_ITEM_MAX; i++) world_items_drop(1, 0, 0, 200 + i, 0, 0);
    CHECK(world_items_drop(1, 0, 0, 0, 0, 0) == -1, "pool full returns -1");

    /* --- using items (a fresh run) --- */
    world_items_reset();
    world_items_drop(1, -1, -1, 0, 0, 0); world_items_drop(2, -1, -1, 0, 0, 0); world_items_drop(3, -1, -1, 0, 0, 0);
    world_items_drop(4, -1, -1, 0, 0, 0); world_items_drop(5, -1, -1, 0, 0, 0); world_items_drop(6, -1, -1, 0, 0, 0); world_items_drop(7, -1, -1, 0, 0, 0);
    for (int id = 1; id <= 7; id++) pick(id);
    CHECK(world_items_carried_count() == 7, "all seven carried");
    CHECK(hold(2) && world_items_use_held(0, 0, 0, msg, sizeof msg) == 2, "held pencil + notebook in pack -> writes (point 100 is clear)");
    CHECK(carried_mut(2)->aux == 90 && carried_mut(1)->aux == 1, "pencil point 90, notebook 1 pencil page");
    for (int i = 0; i < 6; i++) world_items_use_held(0, 0, 0, msg, sizeof msg);   /* point 90 -> 30 after 6 more: still clear */
    CHECK(carried_mut(2)->aux == 30 && carried_mut(1)->aux == 7, "after 7 pages point is 30");
    CHECK(world_items_use_held(0, 0, 0, msg, sizeof msg) == 2 && carried_mut(2)->aux == 20, "point 30 still writes clear (>=30), drops to 20");
    CHECK(world_items_use_held(0, 0, 0, msg, sizeof msg) == 3 && strstr(msg, "FAINT"), "dull pencil writes faintly");
    /* held pencil with a sharpener in the pack: pairs are tried in carry order, notebook first, so drop the notebook to reach it */
    CHECK(hold(5) && world_items_use_held(0, 0, 0, msg, sizeof msg) == 4 && carried_mut(5)->aux == 95 && carried_mut(1)->aux2 == 1, "held pen writes in the notebook (ink 95, 1 pen page)");
    world_items_reset();
    world_items_drop(2, 40, 190, 0, 0, 0); world_items_drop(3, -1, -1, 0, 0, 0); pick(2); pick(3); hold(2);
    CHECK(world_items_use_held(0, 0, 0, msg, sizeof msg) == 1 && carried_mut(2)->aux == 100 && carried_mut(2)->aux2 == 182 && carried_mut(3)->aux == 1, "pencil+sharpener: sharp, 182mm, 1 shaving");
    CHECK(world_items_use_held(0, 0, 0, msg, sizeof msg) == -1 && strstr(msg, "ALREADY SHARP"), "sharp pencil refused with a reason");
    hold(3); CHECK(world_items_use_held(0, 0, 0, msg, sizeof msg) == 6 && carried_mut(3)->aux == 0, "held sharpener: the already-sharp pencil refuses, so it empties itself");
    /* sharpener alone empties */
    world_items_reset();
    world_items_drop(3, 5, 0, 0, 0, 0); pick(3);
    CHECK(world_items_use_held(0, 0, 0, msg, sizeof msg) == 6 && carried_mut(3)->aux == 0, "sharpener alone empties shavings");
    /* world target: pencil in hand, notebook lying on the desk in reach */
    world_items_reset();
    world_items_drop(2, 100, 190, 0, 0, 0); pick(2);
    world_items_drop(1, -1, -1, 1, 0, 0);
    CHECK(world_items_use_held(0, 0, 0, msg, sizeof msg) == 2 && world_items_get(world_items_nearest(1, 0, 0))->s.aux == 1, "pencil writes in a notebook lying on the desk");
    CHECK(world_items_use_held(30, 0, 0, msg, sizeof msg) == 0, "out of reach of the desk: nothing happens");
    /* ruler */
    world_items_reset(); world_items_drop(6, -1, -1, 0, 0, 0); world_items_drop(2, -1, 150, 0, 0, 0); pick(6); pick(2); hold(6);
    CHECK(world_items_use_held(0, 0, 0, msg, sizeof msg) == 8 && strstr(msg, "150"), "ruler measures the pencil (150 mm)");
    /* drop */
    CHECK(world_items_drop_held(3, 0, 3) >= 0 && world_items_carried_count() == 1 && world_items_active_count() == 1, "drop the held ruler back into the world");
    CHECK(world_items_drop_held(0, 0, 0) >= 0 && world_items_drop_held(0, 0, 0) == -1 && world_items_held() == -1, "empty-handed drop is -1");
    CHECK(world_items_use_held(0, 0, 0, msg, sizeof msg) == 0 && strstr(msg, "NOTHING IN HAND"), "using nothing");
    char d[64]; ItemState pc = {2, 80, 150}; world_item_describe(&pc, d, sizeof d);
    CHECK(strstr(d, "point 80") && strstr(d, "150mm"), "describe pencil");
    printf("%s (%d fails)\n", fails ? "FAILED" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
