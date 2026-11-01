/* ar_grenade_test.c -- the assault rifle's underbarrel grenade, through the REAL engine path
 * (physics.h's update_weapons, no stubs around it).
 *
 * Founder real-time: "add an E for 3 the assault rifle ... the traditional right click thing where
 * it fires a grenade like if its an m4 ... have e and right click both trigger it ... it should be
 * fast like soldier 76 rocket ... a bit of an arch down ... not so fast that its instant like a
 * rail gun."
 *
 * What is actually provable headlessly: the grenade the AR fires has the grenade's own numbers
 * (damage, splash radius, gravity, owning weapon), it is clearly faster than a missile and clearly
 * slower than instant, both input bits fire it against one shared cooldown, and the right-mouse bit
 * is NOT the sniper's E (scoping must not spend the storm charges).
 *
 * NOT provable here: the arc itself. Gravity is integrated in update_projectiles (local_game.h),
 * which needs the whole local-world stack; this asserts the field the integrator reads and the
 * initial upward velocity, and names the gap rather than faking it.
 *
 * make test-ar-grenade
 */
#include "../common/physics.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* physics.h's phys_respawn calls into local_game.h's bot evolution; no respawn here. */
PlayerState *get_best_bot(void) { return NULL; }
void evolve_bot(PlayerState *a, PlayerState *b) { (void)a; (void)b; }

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static Projectile g_proj[MAX_PROJECTILES];

static int active_projectiles(void) {
    int n = 0;
    for (int i = 0; i < MAX_PROJECTILES; i++) if (g_proj[i].active) n++;
    return n;
}

static Projectile *first_active(void) {
    for (int i = 0; i < MAX_PROJECTILES; i++) if (g_proj[i].active) return &g_proj[i];
    return NULL;
}

static void reset(PlayerState *ps, int weapon) {
    memset(ps, 0, sizeof(PlayerState) * MAX_CLIENTS);
    memset(g_proj, 0, sizeof g_proj);
    for (int i = 0; i < 2; i++) {
        ps[i].active = 1; ps[i].state = STATE_ALIVE; ps[i].health = 100;
        ps[i].id = i; ps[i].scene_id = SCENE_CITY; ps[i].team_id = -1;
    }
    ps[0].current_weapon = weapon;
    ps[0].ammo[weapon] = WPN_STATS[weapon].ammo_max;
    phys_set_scene(SCENE_CITY);
}

int main(void) {
    static PlayerState ps[MAX_CLIENTS];

    /* 0. the tuning itself: a real grenade, not a reskinned bullet */
    CHECK(AR_GRENADE_DAMAGE > 0 && AR_GRENADE_DAMAGE < WPN_STATS[WPN_MISSILE].dmg);
    CHECK(AR_GRENADE_SPLASH_RADIUS > 0.0f && AR_GRENADE_SPLASH_RADIUS <= MISSILE_SPLASH_RADIUS);
    CHECK(AR_GRENADE_GRAVITY > 0.0f);                             /* it arcs down, not dead straight */
    CHECK(AR_GRENADE_SPEED_MULT > 1.0f && AR_GRENADE_SPEED_MULT < 6.0f); /* faster than a missile, not instant */
    CHECK(AR_GRENADE_COOLDOWN > 0);

    /* 1. E fires it: one real projectile carrying the grenade's own numbers */
    reset(ps, WPN_AR);
    ps[0].pitch = 10.0f;                                          /* aimed up: the arc starts rising */
    update_weapons(&ps[0], ps, g_proj, 0, 0, 1, 0, 1000u, 3000u);
    Projectile *g = first_active();
    CHECK(active_projectiles() == 1);
    CHECK(g != NULL);
    if (g) {
        CHECK(g->damage == AR_GRENADE_DAMAGE);
        CHECK(fabsf(g->splash_radius - AR_GRENADE_SPLASH_RADIUS) < 1e-4f);
        CHECK(fabsf(g->gravity - AR_GRENADE_GRAVITY) < 1e-4f);
        CHECK(g->weapon == WPN_AR);                              /* blast carries the AR id, not the missile's */
        CHECK(g->owner_id == 0);
        CHECK(g->scene_id == SCENE_CITY);
        float speed = sqrtf(g->vx * g->vx + g->vy * g->vy + g->vz * g->vz);
        CHECK(fabsf(speed - 4.0f * AR_GRENADE_SPEED_MULT) < 1e-3f);
        CHECK(g->vy > 0.3f);                                     /* upward, so the arc can come down */
        CHECK(speed > 4.0f * 1.5f);                              /* clearly faster than a missile (1.0x) */
        CHECK(ps[0].ability_cooldown == AR_GRENADE_COOLDOWN);
    }

    /* 2. the cooldown: holding E does not stream grenades, and it comes back */
    reset(ps, WPN_AR);
    update_weapons(&ps[0], ps, g_proj, 0, 0, 1, 0, 2000u, 3000u);
    update_weapons(&ps[0], ps, g_proj, 0, 0, 1, 0, 2016u, 3000u);
    CHECK(active_projectiles() == 1);
    for (int t = 0; t < AR_GRENADE_COOLDOWN; t++)
        update_weapons(&ps[0], ps, g_proj, 0, 0, 0, 0, (unsigned)(2032 + t * 16), 3000u);
    update_weapons(&ps[0], ps, g_proj, 0, 0, 1, 0, 9000u, 3000u);
    CHECK(active_projectiles() == 2);

    /* 3. the right mouse / controller LT bit fires the same grenade */
    reset(ps, WPN_AR);
    update_weapons(&ps[0], ps, g_proj, 0, 0, 0, 1, 3000u, 3000u);
    CHECK(active_projectiles() == 1);
    if (first_active()) CHECK(first_active()->weapon == WPN_AR);
    CHECK(ps[0].ability_cooldown == AR_GRENADE_COOLDOWN);

    /* 4. and it shares ONE cooldown with E: alternated presses do not double-fire */
    reset(ps, WPN_AR);
    update_weapons(&ps[0], ps, g_proj, 0, 0, 1, 0, 4000u, 3000u);
    update_weapons(&ps[0], ps, g_proj, 0, 0, 0, 1, 4016u, 3000u);
    CHECK(active_projectiles() == 1);

    /* 5. the secondary bit is NOT the sniper's E: scoping (RMB held) must not burn the storm charges */
    reset(ps, WPN_SNIPER);
    update_weapons(&ps[0], ps, g_proj, 0, 0, 0, 1, 5000u, 3000u);
    CHECK(ps[0].storm_charges == 0 && active_projectiles() == 0);
    update_weapons(&ps[0], ps, g_proj, 0, 0, 1, 0, 5016u, 3000u);
    CHECK(ps[0].storm_charges == 5);

    /* 6. a weapon with no secondary does nothing on that bit (no projectile, no cooldown burned).
     *    This case was the magnum until the magnum got a secondary of its own -- the right-mouse
     *    spray, see packages/simulation/magnum_spray_test.c -- so the knife is the honest case now. */
    reset(ps, WPN_KNIFE);
    update_weapons(&ps[0], ps, g_proj, 0, 0, 0, 1, 6000u, 3000u);
    CHECK(active_projectiles() == 0);
    CHECK(ps[0].ability_cooldown == 0);
    CHECK(ps[0].attack_cooldown == 0);

    /* 7. the other splash weapons keep their old shape: no gravity, their own weapon id */
    reset(ps, WPN_MISSILE);
    update_weapons(&ps[0], ps, g_proj, 1, 0, 0, 0, 7000u, 3000u);
    CHECK(active_projectiles() == 1);
    if (first_active()) {
        CHECK(first_active()->gravity == 0.0f);
        CHECK(first_active()->weapon == WPN_MISSILE);
        CHECK(fabsf(first_active()->splash_radius - MISSILE_SPLASH_RADIUS) < 1e-4f);
    }

    if (g_fail) { printf("ar_grenade_test: %d FAILED\n", g_fail); return 1; }
    printf("ar_grenade_test OK (speed %.1f u/tick, splash %.1f, gravity %.2f, cooldown %d ticks)\n",
           4.0f * AR_GRENADE_SPEED_MULT, AR_GRENADE_SPLASH_RADIUS, AR_GRENADE_GRAVITY, AR_GRENADE_COOLDOWN);
    return 0;
}
