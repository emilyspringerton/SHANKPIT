/* kit_test.c -- the kit model and its three pages.
 * Build/run: make test-kit (plain gcc, -Wall -Wextra -Werror, no SDL/GL). */
#include "kit.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    KitState s;
    kit_defaults(&s);

    /* ---- slots: 1-9, 0, then the four gadget keys, and F is deliberately absent ---- */
    assert(KIT_SLOTS == 14);
    assert(strcmp(kit_slot_key_name(0), "1") == 0);
    assert(strcmp(kit_slot_key_name(9), "0") == 0);
    assert(strcmp(kit_slot_key_name(KIT_SLOT_BACKTICK), "`") == 0);
    assert(strcmp(kit_slot_key_name(KIT_SLOT_MINUS), "-") == 0);
    assert(strcmp(kit_slot_key_name(KIT_SLOT_EQUALS), "=") == 0);
    assert(strcmp(kit_slot_key_name(KIT_SLOT_C), "C") == 0);
    assert(kit_slot_from_key('7') == 6);
    assert(kit_slot_from_key('0') == 9);
    assert(kit_slot_from_key('`') == KIT_SLOT_BACKTICK);
    assert(kit_slot_from_key('-') == KIT_SLOT_MINUS);
    assert(kit_slot_from_key('=') == KIT_SLOT_EQUALS);
    assert(kit_slot_from_key('c') == KIT_SLOT_C && kit_slot_from_key('C') == KIT_SLOT_C);
    assert(kit_slot_from_key('f') == -1 && kit_slot_from_key('F') == -1);   /* F = vehicle entry */
    assert(kit_slot_from_key('x') == -1);
    printf("PASS: 14 slots -- 1-9, 0, ` - = C, and no F\n");

    /* ---- prefabs: three of them, genuinely different, digits untouched ---- */
    assert(s.kit_count == 3);
    assert(s.selected == 0);
    assert(strcmp(s.kits[0].name, "SCOUT") == 0);
    assert(strcmp(s.kits[1].name, "BREACHER") == 0);
    assert(strcmp(s.kits[2].name, "HUNTER") == 0);
    for (int i = 0; i < KIT_SLOT_FIRST_GADGET; i++) assert(s.kits[0].slot[i] == KIT_ITEM_NONE);
    assert(kit_live_item(&s, KIT_SLOT_C) == KIT_ITEM_PHEROMONE);
    assert(kit_live_item(&s, KIT_SLOT_BACKTICK) == KIT_ITEM_RELOAD);
    assert(kit_live_item(&s, 99) == KIT_ITEM_NONE);        /* out of range is empty, not a crash */
    printf("PASS: three prefabs, and none of them steals a number key\n");

    /* ---- cycling wraps through every item and back to empty ---- */
    kit_clear_slot(&s.kits[0], KIT_SLOT_MINUS);
    int seen_empty = 0;
    for (int i = 0; i < KIT_ITEM_COUNT; i++) {
        if (s.kits[0].slot[KIT_SLOT_MINUS] == KIT_ITEM_NONE) seen_empty++;
        kit_cycle(&s.kits[0], KIT_SLOT_MINUS, 1);
    }
    assert(seen_empty == 1 && s.kits[0].slot[KIT_SLOT_MINUS] == KIT_ITEM_NONE);   /* full loop */
    kit_cycle(&s.kits[0], KIT_SLOT_MINUS, -1);
    assert(s.kits[0].slot[KIT_SLOT_MINUS] == KIT_ITEM_RELOAD);                    /* backwards wraps */
    printf("PASS: cycling wraps through all %d items and returns to empty\n", KIT_ITEM_COUNT);

    /* ---- selecting a kit moves what the gameplay keys read ---- */
    kit_select(&s, 1);
    assert(s.selected == 1);
    assert(kit_live_item(&s, KIT_SLOT_BACKTICK) == KIT_ITEM_PHEROMONE);     /* BREACHER */
    kit_select(&s, 99);                                                     /* ignored, not clamped */
    assert(s.selected == 1);
    printf("PASS: selecting a kit changes what the keys read\n");

    /* ---- the builder edits a DRAFT: nothing lands until it is saved ---- */
    {
        int live_before = kit_live_item(&s, KIT_SLOT_EQUALS);
        kit_cycle_draft(&s, KIT_SLOT_EQUALS, 1);
        assert(kit_live_item(&s, KIT_SLOT_EQUALS) == live_before);           /* live untouched */
        assert((int)s.draft.slot[KIT_SLOT_EQUALS] != live_before);
        kit_save_draft(&s);
        assert(kit_live_item(&s, KIT_SLOT_EQUALS) == (int)s.draft.slot[KIT_SLOT_EQUALS]);
        kit_select(&s, 2);                                                  /* a kit switch resets the draft */
        assert((int)s.draft.slot[KIT_SLOT_EQUALS] == kit_live_item(&s, KIT_SLOT_EQUALS));
    }
    printf("PASS: the draft is separate until SAVE, and a save becomes the live kit\n");

    /* ---- the three pages: real HTML, CSS, and the rows they claim ---- */
    static char page[8192];
    int n = kit_page_select(page, (int)sizeof page, &s);
    assert(n > 0 && strstr(page, "<html") && strstr(page, "<style") && strstr(page, "HUNTER"));
    n = kit_page_map(page, (int)sizeof page, &s, KIT_SLOT_C);
    assert(n > 0 && strstr(page, "PHEROMONE") && strstr(page, "class=\"on\""));
    n = kit_page_build(page, (int)sizeof page, &s, KIT_SLOT_MINUS);
    assert(n > 0 && strstr(page, "KIT BUILDER"));
    /* a buffer too small is refused, never truncated into broken markup */
    {
        char tiny[64];
        assert(kit_page_map(tiny, (int)sizeof tiny, &s, 0) == -1);
    }
    printf("PASS: three HTML+CSS pages (%d bytes for the map), and a short buffer is refused\n", n);

    printf("ALL PASS\n");
    return 0;
}
