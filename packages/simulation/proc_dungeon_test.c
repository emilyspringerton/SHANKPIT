/* proc_dungeon_test.c -- a procedural dungeon that is the same dungeon for everyone, a shared zone that
 * survives being emptied (for fifteen minutes), and a gate that is a real widget circuit.
 *
 * The point of the file is the two properties the founder actually asked for:
 *   "the first person to enter it it generates and then everyone else enters the same one"
 *   "a grace period of 15 minutes ... after the zone dies the first person to enter it back it generates
 *    a new one"
 * so determinism is checked by saving two levels from one seed and comparing the bytes, and the lifecycle
 * is checked against a fake clock, not a sleep.
 */
#include <stdio.h>
#include <string.h>

#include "proc_dungeon.h"
#include "zone_registry.h"
#include "zone_rules_host.h"
#include "widget_runtime.h"

static int g_fail = 0, g_checks = 0;
static void ok(int cond, const char *what) {
    g_checks++;
    if (!cond) { g_fail++; printf("FAIL  %s\n", what); }
    else printf("ok    %s\n", what);
}

/* Reads into the CALLER's buffer. The first draft returned one static buffer, so comparing two "files"
 * compared one pointer to itself -- the determinism checks passed while proving nothing. */
static int slurp(const char *path, char *out, size_t cap) {
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f || !out || cap == 0) return 0;
    n = fread(out, 1, cap - 1, f);
    fclose(f);
    out[n] = 0;
    return 1;
}

static char g_expired_id[ZONE_ID_MAX];
static int g_expired_n;
static void on_expire(void *user, const char *id) {
    (void)user;
    snprintf(g_expired_id, sizeof g_expired_id, "%s", id ? id : "");
    g_expired_n++;
}

/* fake sinks for the gate circuit */
static int g_box_open_gate, g_box_events;
static void box_sink(void *user, int box_index, int open) {
    (void)user;
    if (box_index == *(int *)user + 1000) return;
    g_box_open_gate = open;
    g_box_events++;
}

int main(void) {
    static CustomLevelData a, b, c;
    static ZoneRegistry reg;
    unsigned seed1 = 0, seed2 = 0, seed3 = 0;
    int n1, n2, n3;

    /* ---- the rules agree, PARENA vs compiled-in defaults --------------------------------- */
    {
        ZoneRules saved = g_zone_rules;
        int mismatches = 0, samples = 0;
        zone_rules_install();
        for (int seed = 0; seed < 64; seed++) {
            samples++;
            if (saved.room_count(seed) != zone_room_count(seed)) mismatches++;
            if (saved.wall_height(seed) != zone_wall_height(seed)) mismatches++;
            if (saved.spawn_count(seed) != zone_spawn_count(seed)) mismatches++;
            if (saved.difficulty(seed) != zone_difficulty(seed)) mismatches++;
            for (int r = 0; r < 8; r++) {
                samples++;
                if (saved.room_width(seed, r) != zone_room_width(seed, r)) mismatches++;
                if (saved.room_depth(seed, r) != zone_room_depth(seed, r)) mismatches++;
                if (saved.material_for(seed, r) != zone_material_for(seed, r)) mismatches++;
            }
        }
        for (int players = 0; players <= 2; players++)
            for (int idle = 0; idle <= 1000000; idle += 50000) {
                samples++;
                if (saved.should_expire(players, idle) != zone_should_expire(players, idle)) mismatches++;
            }
        ok(samples > 100 && mismatches == 0, "PARENA's zone rules equal the compiled-in defaults");
        ok(zone_grace_seconds() == 900, "the grace period is the founder's fifteen minutes");
    }

    /* ---- one seed is one dungeon -------------------------------------------------------- */
    n1 = proc_dungeon_generate(&a, "dungeon_a", 0x1234u);
    n2 = proc_dungeon_generate(&b, "dungeon_a", 0x1234u);
    ok(n1 > 0 && n1 == n2, "the same seed builds the same number of boxes");
    ok(a.count == n1 && b.count == n2, "and the level reports the count the generator returned");
    ok(a.count > 10 && a.count <= LEVEL_BOXES_MAX, "a real layout, inside the level's own box cap");
    ok(a.edu_widget_count == 3, "the gate is authored as a lever, a relay and a machine");
    ok(strcmp(a.edu_widgets[0].entity_id, "proc_gate_lever") == 0 &&
       a.edu_widgets[1].input_pin == 0 && a.edu_widgets[2].input_pin == 1,
       "and the pins point backwards, which is the circuit's own rule");
    ok(a.edu_widgets[2].box_index >= 0 && a.edu_widgets[2].box_index < a.count,
       "the machine drives a gate box that exists");
    ok(a.spawner_count >= 3, "the layout seeds real spawners");

    ok(proc_dungeon_save("/tmp/proc_a1.json", &a) && proc_dungeon_save("/tmp/proc_a2.json", &b),
       "both layouts save as level JSON");
    {
        static char ja[262144], jb[262144];
        ok(slurp("/tmp/proc_a1.json", ja, sizeof ja) && slurp("/tmp/proc_a2.json", jb, sizeof jb),
           "both level files read back");
        ok(ja[0] && strcmp(ja, jb) == 0,
           "two clients generating from one seed write byte-identical levels");
    }

    /* a different seed is a different dungeon (and the hub is always at the origin) */
    n3 = proc_dungeon_generate(&c, "dungeon_a", 0x9999u);
    ok(n3 > 0 && proc_dungeon_save("/tmp/proc_c.json", &c), "a different seed builds and saves");
    {
        static char ja[262144], jc[262144];
        ok(slurp("/tmp/proc_a1.json", ja, sizeof ja) && slurp("/tmp/proc_c.json", jc, sizeof jc),
           "both seeds read back");
        ok(strcmp(ja, jc) != 0, "and they are not the same layout");
    }

    /* the saved level round-trips through the real parser (so a co-op client can load the file) */
    {
        static CustomLevelData back;
        ok(level_boxes_load_from_file("/tmp/proc_a1.json", &back), "the saved level loads back");
        ok(back.count == a.count && back.edu_widget_count == 3 && back.spawner_count == a.spawner_count,
           "with its boxes, gate circuit and spawners intact");
        ok(back.edu_widgets[2].box_index == a.edu_widgets[2].box_index,
           "including the gate's own box index");
    }

    /* ---- the gate really opens, through the real circuit --------------------------------- */
    {
        static WidgetRuntime rt;
        WidgetActor actor;
        int ticks = 0;
        ok(widget_runtime_load(&rt, &a) == 3, "the generated level's circuit loads");
        actor.x = 0.0f; actor.y = 0.0f; actor.z = 6.0f;   /* standing at the lever */
        ok(widget_runtime_focus(&rt, actor.x, actor.y, actor.z, 0, 16) == 0, "the lever is in reach");
        ok(widget_runtime_use(&rt) == 1, "and F flips it");
        while (ticks < 120 && g_box_open_gate == 0) {
            widget_runtime_tick(&rt, &actor, 1, NULL, box_sink, &a.edu_widgets[2].box_index);
            ticks++;
        }
        ok(g_box_open_gate == 1 && ticks >= 45 && ticks <= 60,
           "the gate opens on the relay's own delay, not on the press");
    }

    /* ---- the shared-zone lifecycle ------------------------------------------------------ */
    zone_registry_init(&reg, 777u);
    ok(zone_registry_enter(&reg, "dungeon_a", 0, &seed1) == 1, "the first entrant creates the zone");
    ok(seed1 != 0 && zone_registry_state(&reg, "dungeon_a") == ZONE_STATE_ACTIVE, "which is active");
    ok(zone_registry_enter(&reg, "dungeon_a", 0, &seed2) == 0 && seed2 == seed1,
       "a second entrant joins the SAME zone with the same seed (co-op is not instanced)");
    ok(zone_registry_players(&reg, "dungeon_a") == 2, "two players are inside");

    ok(zone_registry_leave(&reg, "dungeon_a", 1000) == 0, "one leaves: not idle yet");
    ok(zone_registry_leave(&reg, "dungeon_a", 2000) == 1, "the last one leaves: the countdown starts");
    ok(zone_registry_state(&reg, "dungeon_a") == ZONE_STATE_IDLE, "the zone is idle, not gone");
    ok(zone_registry_tick(&reg, 2000 + 899999, on_expire, NULL) == 0 && g_expired_n == 0,
       "and it does not expire one millisecond before fifteen minutes");

    ok(zone_registry_enter(&reg, "dungeon_a", 500000, &seed3) == 0 && seed3 == seed1,
       "re-entering inside the window keeps the SAME layout");
    ok(zone_registry_state(&reg, "dungeon_a") == ZONE_STATE_ACTIVE, "and cancels the countdown");
    ok(zone_registry_tick(&reg, 9000000, on_expire, NULL) == 0,
       "an occupied zone never expires, however long it has existed");
    zone_registry_leave(&reg, "dungeon_a", 9000000);
    ok(zone_registry_tick(&reg, 9000000 + 900000, on_expire, NULL) == 1 && g_expired_n == 1,
       "fifteen minutes after the last player leaves, it dies");
    ok(strcmp(g_expired_id, "dungeon_a") == 0, "and the host is told which zone to delete");
    ok(zone_registry_state(&reg, "dungeon_a") == ZONE_STATE_UNINIT, "the slot is cleared");

    ok(zone_registry_enter(&reg, "dungeon_a", 10000000, &seed3) == 1, "the next entrant regenerates");
    ok(seed3 != seed1, "from a NEW seed, so a farmed-out dungeon does not come back the same");
    {
        static CustomLevelData fresh;
        ok(proc_dungeon_generate(&fresh, "dungeon_a", seed3) > 0 && proc_dungeon_save("/tmp/proc_d.json", &fresh),
           "and that seed builds a saveable level");
        {
            static char ja[262144], jd[262144];
            ok(slurp("/tmp/proc_a1.json", ja, sizeof ja) && slurp("/tmp/proc_d.json", jd, sizeof jd),
               "the regenerated dungeon reads back");
            ok(strcmp(ja, jd) != 0, "which is a different dungeon");
        }
    }

    /* a full table refuses rather than sharing a stranger's world */
    {
        static ZoneRegistry full;
        char id[16];
        int i, refused = 0;
        zone_registry_init(&full, 5u);
        for (i = 0; i < ZONE_MAX_ZONES_DEFAULT; i++) {
            snprintf(id, sizeof id, "z%d", i);
            if (zone_registry_enter(&full, id, 0, NULL) != 1) refused = -1;
        }
        ok(refused == 0, "the table holds its own maximum of zones");
        ok(zone_registry_enter(&full, "overflow", 0, NULL) == -1, "and refuses one more entry");
    }

    /* two zones do not interfere */
    {
        static ZoneRegistry two;
        unsigned s1 = 0, s2 = 0;
        zone_registry_init(&two, 9u);
        ok(zone_registry_enter(&two, "alpha", 0, &s1) == 1 && zone_registry_enter(&two, "beta", 0, &s2) == 1,
           "two different zones can both be live");
        ok(s1 != s2, "with different seeds");
        zone_registry_leave(&two, "alpha", 100);
        ok(zone_registry_tick(&two, 100 + 900000, on_expire, NULL) >= 1 &&
           zone_registry_state(&two, "beta") == ZONE_STATE_ACTIVE,
           "and expiring one leaves the other alone");
    }

    printf("\n%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
