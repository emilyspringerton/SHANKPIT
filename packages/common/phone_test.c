/* phone_test.c -- real tests for phone.h (BIG_O engine merge, phase 6), a faithful, verbatim port
 * of BIG_O's own real bigo_phone_test.c. Build and run:
 *   gcc -Wall -Wextra -O2 -o /tmp/phone_test packages/common/phone_test.c && /tmp/phone_test
 */
#include <stdio.h>
#include "phone.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static void go(Phone *p, int app) { p->open = 1; p->app = -1; p->home_cursor = app; phone_input(p, BP_SELECT, 0); }
int main(void) {
    Phone p; phone_init(&p);
    CHECK(!p.open); CHECK(phone_input(&p, BP_SELECT, 9).kind == BP_FX_NONE);   /* closed phone ignores input */
    phone_toggle(&p); CHECK(p.open && p.app == -1);
    /* home grid navigation + every app reachable */
    for (int a = 0; a < BP_APP_COUNT; a++) { go(&p, a); CHECK(p.app == a); phone_input(&p, BP_BACK, 0); CHECK(p.app == -1); }
    phone_input(&p, BP_BACK, 0); CHECK(!p.open);
    /* anti-spam: 3rd message in 30s is queued, then released as a batched summary */
    phone_init(&p);
    phone_notify(&p, 1, 1000); phone_notify(&p, 1, 2000); phone_notify(&p, 1, 3000); phone_notify(&p, 1, 4000);
    CHECK(p.banner_id == 1 && p.queue_len == 2 && p.message_count == 4 && p.unread == 4);
    phone_tick(&p, 7500); CHECK(p.banner_id == 0);            /* banner expired, window still full */
    phone_tick(&p, 32500); CHECK(p.banner_id == 1 && p.banner_batched == 2 && p.queue_len == 0);
    /* messages app clears unread */
    go(&p, BP_APP_MESSAGES); CHECK(p.unread == 0);
    /* contacts: preset reply advances trust, capped at documented */
    phone_init(&p); go(&p, BP_APP_CONTACTS);
    for (int i = 0; i < 6; i++) phone_input(&p, BP_SELECT, 0);
    CHECK(p.trust[0] == 3 && p.replied[0] == 1);
    phone_input(&p, BP_RIGHT, 0); phone_input(&p, BP_SELECT, 0); CHECK(p.replied[0] == 2);
    /* map pin toggles; camera counts photos and emits fx */
    go(&p, BP_APP_MAP); phone_input(&p, BP_DOWN, 0); phone_input(&p, BP_SELECT, 0); CHECK(p.zone_pinned == 1);
    phone_input(&p, BP_SELECT, 0); CHECK(p.zone_pinned == -1);
    go(&p, BP_APP_CAMERA); BpEffect fx = phone_input(&p, BP_SELECT, 0); CHECK(fx.kind == BP_FX_TAKE_PHOTO && p.photos == 1);
    /* skills: needs an unspent point, effect carries the ability row */
    go(&p, BP_APP_SKILLS); phone_input(&p, BP_DOWN, 1); phone_input(&p, BP_DOWN, 1);
    fx = phone_input(&p, BP_SELECT, 0); CHECK(fx.kind == BP_FX_NONE);
    fx = phone_input(&p, BP_SELECT, 1); CHECK(fx.kind == BP_FX_ALLOCATE_TALENT && fx.arg == 2);
    /* loadout: only owned weapons (knife always) */
    go(&p, BP_APP_LOADOUT); fx = phone_input(&p, BP_SELECT, 0); CHECK(fx.kind == BP_FX_WEAPON_SWITCH && fx.arg == 0);
    phone_input(&p, BP_DOWN, 0); fx = phone_input(&p, BP_SELECT, 0); CHECK(fx.kind == BP_FX_NONE);
    p.weapons_owned |= 2; fx = phone_input(&p, BP_SELECT, 0); CHECK(fx.kind == BP_FX_WEAPON_SWITCH && fx.arg == 1);
    /* wardrobe */
    go(&p, BP_APP_WARDROBE); phone_input(&p, BP_DOWN, 0); phone_input(&p, BP_SELECT, 0); CHECK(p.costume == 1);
    /* cargo: the BIG_O basic food system's own real "use" half -- eating removes the item, shifts
       the rest down, and hands back a real heal effect derived from food_items.h */
    phone_init(&p); CHECK(phone_cargo_add(&p, FOOD_CHERRY) && phone_cargo_add(&p, FOOD_KEY) && p.cargo_count == 2);
    go(&p, BP_APP_CARGO); fx = phone_input(&p, BP_SELECT, 0);
    CHECK(fx.kind == BP_FX_EAT_FOOD && fx.arg == food_item_heal(FOOD_CHERRY) && p.cargo_count == 1 && p.cargo[0] == FOOD_KEY);
    fx = phone_input(&p, BP_SELECT, 0);
    CHECK(fx.kind == BP_FX_EAT_FOOD && fx.arg == food_item_heal(FOOD_KEY) && p.cargo_count == 0);
    fx = phone_input(&p, BP_SELECT, 0); CHECK(fx.kind == BP_FX_NONE);   /* empty cargo, nothing to eat */
    { Phone full; phone_init(&full); for (int i = 0; i < BP_INV_SLOTS; i++) CHECK(phone_cargo_add(&full, FOOD_APPLE));
      CHECK(!phone_cargo_add(&full, FOOD_APPLE) && full.cargo_count == BP_INV_SLOTS); }   /* real, honest cap */
    /* lab: splice needs a sample; consumes exactly one; base/trait choice recorded */
    go(&p, BP_APP_LAB); phone_input(&p, BP_DOWN, 0); phone_input(&p, BP_DOWN, 0);
    phone_input(&p, BP_SELECT, 0); CHECK(p.clone_count == 0);
    p.samples[0] = 1; phone_input(&p, BP_SELECT, 0); CHECK(p.clone_count == 1 && p.samples[0] == 0 && p.clones[0] == 0);
    phone_input(&p, BP_SELECT, 0); CHECK(p.clone_count == 1);
    p.app = BP_APP_LAB; p.cursor = 0; phone_input(&p, BP_RIGHT, 0); CHECK(p.cursor2 == 1);
    p.cursor = 1; phone_input(&p, BP_RIGHT, 0); CHECK(p.lab_trait == 1);
    p.samples[1] = 1; p.cursor = 2; phone_input(&p, BP_SELECT, 0); CHECK(p.clone_count == 2 && p.clones[1] == 1 && p.clone_traits[1] == 1);
    /* world feed + Thorne unlock + message detail */
    phone_init(&p); CHECK(!p.wf_valid && p.contacts_met == 1);
    int zc[BP_ZONES] = { 12, 4, 0, 2, 0 }; phone_set_world(&p, 450, 1, 1, 2, zc, 75);
    CHECK(p.wf_valid && p.wf_minute == 450 && p.wf_zombies[0] == 12 && p.wf_weather == 2 && p.wf_sight == 75);
    phone_notify(&p, BP_MSG_THORNE_BRIEF, 5000); CHECK(p.contacts_met == 2 && p.unread == 1);
    go(&p, BP_APP_MESSAGES); CHECK(!p.detail);
    phone_input(&p, BP_SELECT, 0); CHECK(p.detail);
    phone_input(&p, BP_BACK, 0); CHECK(!p.detail && p.app == BP_APP_MESSAGES);   /* first back closes the detail */
    phone_input(&p, BP_BACK, 0); CHECK(p.app == -1);
    go(&p, BP_APP_CONTACTS); phone_input(&p, BP_DOWN, 0); phone_input(&p, BP_SELECT, 0); CHECK(p.trust[1] == 1);   /* Thorne replies work */
    printf(fails ? "PHONE TEST FAILED\n" : "PHONE TEST OK\n");
    return fails != 0;
}
