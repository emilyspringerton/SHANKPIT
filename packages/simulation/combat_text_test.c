/* combat_text_test.c -- headless checks for floating combat text.
 *   make test-combat-text
 */
#include <stdio.h>
#include <string.h>
#include "shield_regen.h"
#include "combat_text.h"

static int checks = 0;
static int failures = 0;

#define ok(cond, msg) do { \
    checks++; \
    if (cond) { printf("ok   %s\n", msg); } \
    else { printf("FAIL %s\n", msg); failures++; } \
} while (0)

static PlayerState *fresh(void) {
    static PlayerState p;
    memset(&p, 0, sizeof(p));
    p.active = 1; p.state = STATE_ALIVE;
    return &p;
}

int main(void) {
    PlayerState *p = fresh();

    /* Note damage: damage < 0 means a hit; type is set from sign. */
    combat_text_note_damage(p, -12, 1000);
    ok(p->combat_text_time == 1000, "note damage sets time");
    ok(p->combat_text_type == 1, "note damage/negative sets type=1 (heal sign)");
    ok(combat_text_get_damage(p, 2000) == -12, "get damage reads it");
    ok(combat_text_is_active(p, 2000), "damage is active at 2000");

    /* Expired combot text */
    ok(!combat_text_is_active(p, 3000), "damage expired at 3000");
    ok(combat_text_get_age(p, 3000) == 2000, "age reports elapsed ms");

    /* Heal */
    combat_text_note_heal(p, 8, 3100);
    ok(p->combat_text_type == 1, "note heal sets type=1");
    ok(combat_text_get_damage(p, 3200) == -8, "heal read as negative");

    /* Shield note */
    combat_text_note_shield(p, 5, 3300);
    ok(p->combat_text_type == 2, "note shield sets type=2");
    ok(combat_text_get_damage(p, 3400) == 5, "shield read as positive");

    /* Fall-through guards */
    ok(!combat_text_is_active(NULL, 1000), "null guard");
    combat_text_note_damage(p, 0, 4000);
    ok(combat_text_is_active(p, 4000), "zero damage resets to active");

    printf("\ncombat_text tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
