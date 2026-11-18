/* groove_host_test.c -- groove combat inside the engine, end to end: the real update_weapons (physics.h) with the
 * groove hooks installed, the real generated PARENA rules. `make test-groove-combat`. */
#include "physics.h"
#include <stdio.h>
#include <string.h>
#include "../simulation/groove_host.h"

PlayerState *get_best_bot(void) { return NULL; }
void evolve_bot(PlayerState *a, PlayerState *b) { (void)a; (void)b; }
static int passed = 0, failed = 0;
#define EQ(got, want, what) do { long g_ = (long)(got), w_ = (long)(want); \
    if (g_ == w_) passed++; else { failed++; printf("FAIL: %s (got %ld, want %ld)\n", what, g_, w_); } } while (0)

static int log_n = 0, log_t[1024];
void reflux_dispatch(int t, int a, int b, int c) { (void)a; (void)b; (void)c; if (log_n < 1024) log_t[log_n++] = t; }
static int count_of(int t) { int n = 0, i; for (i = 0; i < log_n; i++) if (log_t[i] == t) n++; return n; }

static PlayerState ps[MAX_CLIENTS];
static unsigned int now = 100000;

static void setup(void) {
    int i;
    memset(ps, 0, sizeof ps);
    for (i = 0; i < 2; i++) {
        ps[i].active = 1; ps[i].state = STATE_ALIVE; ps[i].health = 100; ps[i].shield = 0; ps[i].id = i;
        ps[i].scene_id = SCENE_CITY; ps[i].team_id = -1; ps[i].on_ground = 1;
    }
    ps[0].current_weapon = WPN_KNIFE;
    ps[0].x = 0; ps[0].z = 0; ps[0].yaw = 0;      /* yaw 0 faces -z */
    ps[1].x = 0; ps[1].z = -5;                     /* five units in front */
}

/* one engine tick for player 0 with these buttons */
static void tick(int shoot, int ability, int ability2) {
    now += GROOVE_MS_PER_TICK;
    update_weapons(&ps[0], ps, NULL, shoot, 0, ability, ability2, now, 3000);
}

static int stats_rof_x2(const PlayerState *p, int base) { (void)p; return base * 2; }

int main(void) {
    int i, h, before;
    static Box floor_only[1];
    phys_set_scene(SCENE_CITY);
    map_geo = floor_only; map_count = 1;

    /* ---- installed but off: the knife is the plain knife ---- */
    setup();
    groove_host_install(ps, NULL);
    groove_host_set_enabled(0);
    tick(1, 0, 0);
    EQ(ps[1].health < 100, 1, "disabled: the plain knife stab still lands");
    EQ(count_of(REFLUX_ACTION_COMBAT_MOVE_STARTED), 0, "disabled: no groove move");

    /* ---- on: left mouse is a jab that lands after its startup ---- */
    setup();
    groove_host_set_enabled(1);
    log_n = 0;
    now = 100000;                                  /* on the fallback beat: 100000 % 500 == 0 */
    tick(1, 0, 0);
    EQ(count_of(REFLUX_ACTION_COMBAT_MOVE_STARTED), 1, "left mouse starts a groove move");
    EQ(groove_host_fighter(0)->move, GC_MOVE_JAB, "a jab");
    EQ(ps[1].health, 100, "nothing lands during startup");
    for (i = 0; i < 6; i++) tick(1, 0, 0);         /* held: no new move, the jab runs on */
    EQ(ps[1].health < 100, 1, "the jab lands on the target in front");
    EQ(count_of(REFLUX_ACTION_COMBAT_HIT), 1, "once, on the REFLUX log");
    EQ(count_of(REFLUX_ACTION_COMBAT_MOVE_STARTED), 1, "holding the button does not repeat the move");

    /* ---- right mouse + back: the glide; a target behind is untouched by a jab ---- */
    setup(); groove_host_set_enabled(0); groove_host_set_enabled(1); log_n = 0;
    ps[0].in_fwd = -1.0f;
    tick(0, 0, 1);
    EQ(groove_host_fighter(0)->move, GC_MOVE_GLIDE, "right mouse + back glides");
    ps[0].in_fwd = 0.0f;

    /* ---- E is the sync device: four steady taps at 120 BPM lock ---- */
    setup(); groove_host_set_enabled(0); groove_host_set_enabled(1); log_n = 0;
    for (i = 0; i < 4; i++) {
        tick(0, 1, 0);                              /* press */
        h = 0; while (h++ < 30) tick(0, 0, 0);      /* release: 31 ticks x 16 ms = 496 ms per beat */
    }
    EQ(groove_host_sync(0)->locked, 1, "four taps of E on the knife lock the beat");
    EQ(count_of(REFLUX_ACTION_COMBAT_SYNC_LOCKED), 1, "the lock is on the REFLUX log");
    EQ(ps[1].health, 100, "tapping E never stabs");

    /* ---- switch to the AR (key 3): its cooldown snaps to the beat, its hits are stronger ---- */
    ps[0].current_weapon = WPN_AR;
    before = WPN_STATS[WPN_AR].rof;
    EQ(groove_host_rof_hook(&ps[0], before) * GROOVE_MS_PER_TICK <= groove_host_sync(0)->interval_ms, 1,
       "the synced AR fires at least once a beat");
    EQ(groove_host_weapon_hit_hook(&ps[0], &ps[1], WPN_AR, 100, now) > 100, 1, "a synced AR hit is stronger");
    EQ(groove_host_weapon_hit_hook(&ps[0], &ps[1], WPN_SHOTGUN, 100, now), 100, "the shotgun is not synced");

    /* ---- the AR's real fire path goes through the hit hook ---- */
    log_n = 0;
    ps[0].ammo[WPN_AR] = WPN_STATS[WPN_AR].ammo_max;
    ps[0].attack_cooldown = 0;
    ps[0].pitch = 0;
    ps[1].health = 100;
    tick(1, 0, 0);
    EQ(ps[1].health < 100 - WPN_STATS[WPN_AR].dmg, 1, "a real AR shot through update_weapons does the synced damage");

    /* ---- a GFD job's ROF still applies (the hook chains) ---- */
    groove_host_set_enabled(0);
    phys_set_player_stats_hooks(NULL, stats_rof_x2);
    groove_host_install(ps, NULL);
    groove_host_set_enabled(1);
    ps[0].current_weapon = WPN_SHOTGUN;
    EQ(groove_host_rof_hook(&ps[0], 10), 20, "a job's ROF is kept under the groove hook");

    /* ---- another weapon out: the hook ticks the fighter but leaves the weapon alone ---- */
    EQ(groove_host_weapons_hook(&ps[0], ps, WPN_SHOTGUN, 1, 0, 0, now, 3000), 0, "the shotgun is not taken over");
    EQ(groove_host_weapons_hook(&ps[0], ps, WPN_KNIFE, 0, 0, 0, now, 3000), 1, "the knife is");

    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
