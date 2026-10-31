/* stats_apply_test.c -- a job, actually applied to a live player.
 *
 * The stats bridge was tested as a table for months while nothing read it. This covers the wiring instead:
 * a slot with no job keeps the engine's exact numbers, installing the hooks makes a respawn grant the
 * job's health and a shot leave the job's cooldown, and the flag parser refuses a name it does not know
 * rather than quietly handing out a Warrior's stats.
 *
 * It calls the REAL phys_respawn and the REAL fire path's cooldown expression through the hooks, so a
 * change in physics.h that stops honouring them fails here.
 *
 * Build/run: make test-stats-apply
 */
#include "../common/physics.h"   /* first: stats_apply.h installs into physics' own hooks */
#include "stats_apply.h"

#include <stdio.h>
#include <string.h>

/* physics.h's own bot calls, stubbed the way every other physics-level test in this tree stubs them. */
PlayerState *get_best_bot(void) { return NULL; }
void evolve_bot(PlayerState *loser, PlayerState *winner) { (void)loser; (void)winner; }

static int g_checks = 0;
static int g_failures = 0;

static void ok(int cond, const char *what) {
    g_checks++;
    if (!cond) {
        g_failures++;
        printf("FAIL  %s\n", what);
    } else {
        printf("ok    %s\n", what);
    }
}

int main(void) {
    PlayerState p;
    memset(&p, 0, sizeof p);
    p.id = 3;
    p.active = 1;

    printf("== without a job nothing changes ==\n");
    stats_apply_clear();
    stats_apply_uninstall();
    phys_respawn(&p, 1000);
    ok(p.health == 100, "a slot with no job still respawns at 100 HP");
    ok(stats_apply_rof_for(3, 20) == 20, "and keeps its weapon's own ROF");
    ok(stats_apply_health_hook(&p, 100) == 100, "the hook is a pass-through when a slot has no job");

    printf("== a job reaches the real respawn ==\n");
    stats_apply_install();
    int nin = stats_bridge_job_id("NIN");
    int war = stats_bridge_job_id("WAR");
    ok(nin == 12 && war == 0, "the table still resolves NIN and WAR");
    stats_apply_set(3, nin, 75);
    phys_respawn(&p, 2000);
    ok(p.health == sb_clamp(10, 255, g_stats_bridge.health(nin, 75)),
       "respawn grants the job's health, through the real phys_respawn");
    ok(p.health != 100, "which is not the generic pool");

    stats_apply_set(3, war, 1);
    phys_respawn(&p, 3000);
    ok(p.health == sb_clamp(10, 255, g_stats_bridge.health(war, 1)),
       "and a level-1 Warrior gets its own (smaller) pool");

    printf("== haste shortens the cooldown, it does not rewrite the weapon ==\n");
    stats_apply_set(3, nin, 75);
    int base_rof = 20;
    int fast = stats_apply_rof_for(3, base_rof);
    ok(fast < base_rof, "a fast job shortens the cooldown");
    ok(fast >= 1, "but never to zero ticks");
    stats_apply_set(3, war, 1);
    int slow = stats_apply_rof_for(3, base_rof);
    ok(slow >= fast, "and a slow job is not faster than the fast one");
    ok(stats_apply_rof_for(3, 1) >= 1, "a one-tick weapon stays at least one tick");

    printf("== other slots are untouched ==\n");
    ok(stats_apply_rof_for(4, base_rof) == base_rof, "a slot with no job keeps the stock ROF");
    ok(stats_apply_health_for(4) == 100, "and the stock health");

    printf("== the flag parser says no when it does not know ==\n");
    {
        int job = -1, level = -1;
        ok(stats_apply_parse("NIN:75", &job, &level) && job == nin && level == 75, "NIN:75 parses");
        ok(stats_apply_parse("WAR", &job, &level) && job == war && level == 1, "a bare name is level 1");
        ok(!stats_apply_parse("nin", &job, &level), "lowercase is refused, like the table");
        ok(!stats_apply_parse("QQQ:5", &job, &level), "an unknown job is refused");
        ok(!stats_apply_parse("NIN:", &job, &level), "a missing level is refused");
        ok(!stats_apply_parse("NIN:7x", &job, &level), "trailing junk is refused");
    }

    printf("== the accessors are honest about what is wired ==\n");
    stats_apply_set(3, nin, 75);
    ok(stats_apply_move_permille(3) == g_stats_bridge.move_permille(nin),
       "move reads the bridge (no call site consumes it yet)");
    ok(stats_apply_damage_permille(3) == g_stats_bridge.damage_permille(nin, 75),
       "damage reads the bridge (no call site consumes it yet)");
    ok(stats_apply_move_permille(4) == 1000, "and a jobless slot reads 1000, i.e. unchanged");

    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
