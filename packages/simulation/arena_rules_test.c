/* arena_rules_test.c -- the combat + slippery-physics half of the NOOBTOPIA merge, headless.
 *
 * Same source file as NOOBTOPIA's Luau module (PARENA/stdlib/noobtopia/arena_rules.prn), so this test is also the
 * reference for what the Roblox side is supposed to do:
 *   * combat: damage/knockback/cooldown per weapon, to-hit falling off with distance but never impossible or
 *     certain;
 *   * slippery physics: traction ordering (ice < wet metal < grass < mud < concrete), no-input slides that lose
 *     speed per the surface, and the property the whole thing exists for -- RELEASING on ice travels further than
 *     releasing on concrete.
 *
 * make test-noobtopia-arena
 */
#include <stdio.h>

#include "arena_rules.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

int main(void) {
    /* 1. combat numbers are sane and ordered the way a player expects */
    CHECK(weapon_damage(3) > weapon_damage(1));          /* rocket > bat */
    CHECK(weapon_damage(1) > weapon_damage(0));          /* bat > fists */
    CHECK(weapon_damage(99) == weapon_damage(0));        /* unknown weapon is the baseline, not a crash */
    CHECK(weapon_knockback_permille(3) > weapon_knockback_permille(1));
    CHECK(weapon_cooldown_ticks(3) > weapon_cooldown_ticks(1));   /* a rocket is a commitment */
    CHECK(weapon_cooldown_ticks(0) == 8);

    /* 2. to-hit falls off with distance, floors at 50, clamps at 950, and a negative distance is "point blank" */
    int prev = 2000;
    for (int dist = 0; dist < 200; dist++) {
        int ch = weapon_hit_chance_permille(2, dist);
        CHECK(ch >= 50 && ch <= 950);
        CHECK(ch <= prev);
        prev = ch;
    }
    CHECK(weapon_hit_chance_permille(2, 0) > weapon_hit_chance_permille(2, 20));
    CHECK(weapon_hit_chance_permille(3, 200) == 50);
    CHECK(weapon_hit_chance_permille(2, -1) == 950);

    /* 3. traction ordering: ice is the slippery one, concrete grips */
    CHECK(surface_friction_permille(1) < surface_friction_permille(3));
    CHECK(surface_friction_permille(3) < surface_friction_permille(2));
    CHECK(surface_friction_permille(2) < surface_friction_permille(0));
    CHECK(surface_friction_permille(77) == 850);         /* unknown surface is concrete, not frictionless */

    /* 4. acceleration on ice is almost nothing, on concrete it is the most */
    CHECK(surface_accel(1) < surface_accel(0));
    CHECK(surface_jump_impulse(1) < surface_jump_impulse(2));   /* can't plant on ice, grass is springy */

    /* 5. one released tick: ice keeps most of its speed, concrete loses most of it */
    CHECK(slide_decay(100, 1) >= 80);
    CHECK(slide_decay(100, 0) <= 20);
    CHECK(slide_decay(0, 1) == 0);
    CHECK(slide_decay(1, 1) == 0);                       /* a one-unit slide stops rather than creeping forever */

    /* 6. a slide actually terminates (no infinite creep) */
    int sp = 120, ticks = 0;
    while (sp > 0 && ticks < 5000) { sp = slide_decay(sp, 1); ticks++; }
    CHECK(sp == 0 && ticks < 5000);

    /* 7. step-speed: accelerates toward the wish and clamps to it, never past it */
    int v = 0;
    for (int i = 0; i < 100; i++) {
        v = step_speed(v, 10, 0);
        CHECK(v <= 10);
    }
    CHECK(v == 10);

    /* 8. THE property this file exists for: with the same wish and the same ticks, releasing on ice travels
       measurably further than releasing on concrete */
    int ice = 40, stone = 40, ice_dist = 0, stone_dist = 0;
    for (int i = 0; i < 60; i++) {
        ice = step_speed(ice, -1, 1);
        stone = step_speed(stone, -1, 0);
        ice_dist += ice;
        stone_dist += stone;
    }
    CHECK(ice_dist > stone_dist * 3);
    printf("slide: 60 released ticks travelled %d on ice vs %d on concrete\n", ice_dist, stone_dist);

    /* 9. determinism: same inputs, same answers, every time */
    CHECK(step_speed(37, 20, 1) == step_speed(37, 20, 1));
    CHECK(slide_decay(37, 1) == slide_decay(37, 1));

    if (g_fail) { printf("arena_rules_test: %d FAILED\n", g_fail); return 1; }
    printf("arena_rules_test OK\n");
    return 0;
}
