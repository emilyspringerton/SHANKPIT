/* magnum_spray_test.c -- the magnum's secondary fire (right mouse / controller LT), through the REAL
 * engine path (physics.h's update_weapons, no stubs around it).
 *
 * Founder real-time: "can we have it so that the right click for the magnum (pistol) does rapid fire
 * high jitter full auto? it should basically turn into a spray and pray empty the mag ril quick".
 *
 * What is actually provable headlessly: holding the secondary bit fires the magnum with no left
 * mouse at all, at an interval far below its own 25-tick ROF, so the 8-round magazine is empty in
 * about half a second and the normal auto-reload takes over; the stock left-click cadence is
 * untouched; the rounds genuinely scatter (a target you are pointing at 5 units away is hit while
 * one 200 units away on the same line is never hit); and the bit spends no ability cooldown and no
 * storm charges, because for the magnum it is a firing mode, not an ability.
 *
 * NOT provable here: the feel of it. There is no window, no muzzle flash and no audio on this box,
 * so "spray and pray" is asserted as rate + spread + mag drain, not watched on screen.
 *
 * make test-magnum-spray
 */
#include "../common/physics.h"
#include <stdio.h>
#include <string.h>

/* physics.h's phys_respawn calls into local_game.h's bot evolution; no respawn here. */
PlayerState *get_best_bot(void) { return NULL; }
void evolve_bot(PlayerState *a, PlayerState *b) { (void)a; (void)b; }

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static Projectile g_proj[MAX_PROJECTILES];

static void reset(PlayerState *ps, int weapon) {
    memset(ps, 0, sizeof(PlayerState) * MAX_CLIENTS);
    memset(g_proj, 0, sizeof g_proj);
    for (int i = 0; i < 3; i++) {
        ps[i].active = 1; ps[i].state = STATE_ALIVE; ps[i].health = 100;
        ps[i].id = i; ps[i].scene_id = SCENE_CITY; ps[i].team_id = -1;
    }
    ps[0].current_weapon = weapon;
    ps[0].ammo[weapon] = WPN_STATS[weapon].ammo_max;
    phys_set_scene(SCENE_CITY);
}

/* Hold the secondary bit for `ticks` ticks (16 ms each, like a live match). */
static void hold_secondary(PlayerState *ps, int ticks, unsigned int t0) {
    for (int t = 0; t < ticks; t++)
        update_weapons(&ps[0], ps, g_proj, 0, 0, 0, 1, t0 + (unsigned)t * 16u, 3000u);
}

/* Hold the left mouse button for `ticks` ticks, as a control. */
static void hold_fire(PlayerState *ps, int ticks, unsigned int t0) {
    for (int t = 0; t < ticks; t++)
        update_weapons(&ps[0], ps, g_proj, 1, 0, 0, 0, t0 + (unsigned)t * 16u, 3000u);
}

int main(void) {
    static PlayerState ps[MAX_CLIENTS];

    /* 0. the tuning itself: a real fan-fire, not a slightly faster revolver, and a wild spread. */
    CHECK(MAGNUM_SPRAY_TICKS > 0);
    CHECK(MAGNUM_SPRAY_TICKS * 2 < WPN_STATS[WPN_MAGNUM].rof);          /* much faster than 25 ticks */
    CHECK(MAGNUM_SPRAY_TICKS * WPN_STATS[WPN_MAGNUM].ammo_max <= 40);   /* the mag goes in < ~0.7 s */
    CHECK(MAGNUM_SPRAY_SPREAD > WPN_STATS[WPN_AR].spr);                 /* looser than the rifle */
    CHECK(MAGNUM_SPRAY_SPREAD >= WPN_STATS[WPN_SHOTGUN].spr);           /* the widest in the roster */

    /* 1. a held right click fires by itself: no left mouse, one round, the spray interval. */
    reset(ps, WPN_MAGNUM);
    update_weapons(&ps[0], ps, g_proj, 0, 0, 0, 1, 1000u, 3000u);
    CHECK(ps[0].ammo[WPN_MAGNUM] == WPN_STATS[WPN_MAGNUM].ammo_max - 1);
    CHECK(ps[0].attack_cooldown == MAGNUM_SPRAY_TICKS);
    CHECK(ps[0].is_shooting > 0);
    CHECK(ps[0].ability_cooldown == 0);        /* it is a firing mode, not an ability */
    CHECK(ps[0].storm_charges == 0);           /* and it must not hand out the sniper's ultimate */

    /* 2. sustained, it empties the whole magazine in 37 ticks -- then starts the normal full reload
     *    on the very tick the last round leaves (rounds land on ticks 1, 6, 11 ... 36). */
    reset(ps, WPN_MAGNUM);
    hold_secondary(ps, MAGNUM_SPRAY_TICKS * WPN_STATS[WPN_MAGNUM].ammo_max + 1, 2000u);
    CHECK(ps[0].ammo[WPN_MAGNUM] == 0);
    CHECK(ps[0].reload_timer == RELOAD_TIME_FULL);
    hold_secondary(ps, 10, 4000u);
    CHECK(ps[0].reload_timer == RELOAD_TIME_FULL - 10); /* and it counts down like any other reload */

    /* 3. and that is much faster than the same revolver on the left mouse: 8 rounds vs 3 in 60
     *    ticks (the stock 25-tick cadence fires at ticks 1, 26 and 51). */
    reset(ps, WPN_MAGNUM);
    hold_fire(ps, 60, 4000u);
    int stock_shots = WPN_STATS[WPN_MAGNUM].ammo_max - ps[0].ammo[WPN_MAGNUM];
    CHECK(stock_shots == 3);
    CHECK(WPN_STATS[WPN_MAGNUM].ammo_max >= stock_shots * 2);

    /* 4. the stock left-click cadence itself is untouched by any of this. */
    reset(ps, WPN_MAGNUM);
    update_weapons(&ps[0], ps, g_proj, 1, 0, 0, 0, 5000u, 3000u);
    CHECK(ps[0].ammo[WPN_MAGNUM] == WPN_STATS[WPN_MAGNUM].ammo_max - 1);
    CHECK(ps[0].attack_cooldown == WPN_STATS[WPN_MAGNUM].rof);

    /* 5. the jitter is real: aimed dead at two targets on the same line -- one at 5 units, one at
     *    200 -- a held spray hits the near one and never reaches the far one. With the magnum's
     *    authored spread (0.0) both would be hit, so the miss is the spray's doing, not the gun's. */
    reset(ps, WPN_MAGNUM);
    for (int i = 1; i <= 2; i++) {
        ps[i].x = ps[0].x;
        ps[i].z = ps[0].z - (i == 1 ? 5.0f : 200.0f);
        ps[i].y = ps[0].y + EYE_HEIGHT - 2.0f;   /* body centre exactly at eye height, yaw 0, pitch 0 */
    }
    hold_secondary(ps, 60, 6000u);
    CHECK(ps[0].ammo[WPN_MAGNUM] == 0);          /* eight rounds went out */
    CHECK(ps[1].health != 100);                  /* point blank: the near target eats them */
    CHECK(ps[2].health == 100);                  /* 200 units out: nothing lands, it is a spray */

    /* 6. the same shots on the left mouse at that range keep the revolver's accuracy. */
    reset(ps, WPN_MAGNUM);
    for (int i = 1; i <= 2; i++) {
        ps[i].x = ps[0].x;
        ps[i].z = ps[0].z - (i == 1 ? 5.0f : 200.0f);
        ps[i].y = ps[0].y + EYE_HEIGHT - 2.0f;
    }
    update_weapons(&ps[0], ps, g_proj, 1, 0, 0, 0, 7000u, 3000u);
    CHECK(ps[2].health != 100);                  /* aimed single shot still reaches 200 units */

    /* 7. the bit belongs to the magnum: a knife on the right mouse does nothing at all. */
    reset(ps, WPN_KNIFE);
    hold_secondary(ps, 10, 8000u);
    CHECK(ps[0].attack_cooldown == 0);
    CHECK(ps[0].ability_cooldown == 0);

    /* 8. and the AR keeps its grenade on the same bit -- a projectile, not a spray of bullets. */
    reset(ps, WPN_AR);
    update_weapons(&ps[0], ps, g_proj, 0, 0, 0, 1, 9000u, 3000u);
    int projectiles = 0;
    for (int i = 0; i < MAX_PROJECTILES; i++) if (g_proj[i].active) projectiles++;
    CHECK(projectiles == 1);
    CHECK(ps[0].ability_cooldown == AR_GRENADE_COOLDOWN);
    CHECK(ps[0].ammo[WPN_AR] == WPN_STATS[WPN_AR].ammo_max);   /* the grenade is not an AR round */

    if (g_fail) { printf("magnum_spray_test: %d FAILED\n", g_fail); return 1; }
    printf("magnum_spray_test OK (%d-tick spray, spread %.2f, %d rounds -> empty in %d ticks, reload %d)\n",
           MAGNUM_SPRAY_TICKS, (double)MAGNUM_SPRAY_SPREAD, WPN_STATS[WPN_MAGNUM].ammo_max,
           MAGNUM_SPRAY_TICKS * WPN_STATS[WPN_MAGNUM].ammo_max, RELOAD_TIME_FULL);
    return 0;
}
