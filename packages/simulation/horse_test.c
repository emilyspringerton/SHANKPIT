/* horse_test.c -- the tölt mount end to end, headless.
 *
 * What this proves, in the order the game does it:
 *   1. PARENA's horse_rules.prn numbers are installed and are the ones the tick reads.
 *   2. level_boxes.h parses "horse_spawns" (and saves it back), so a farm pad is authored data.
 *   3. scene_load turns those pads into real HorseState actors, and clears stale horses first.
 *   4. horse_spawn_defaults gives a full stamina bar, two carrots and the unicorn skin.
 *   5. Riding burns stamina; the jump is an actual arc (up, then back down to the ground) and costs
 *      180; a carrot gives 400 back and is consumed; a blown horse tops out at 62% speed.
 *   6. PARENA owns what a LOOSE horse decides (graze/wander/idle/home/flee, and its pace/range).
 *   7. A loose horse has an afternoon: it picks a goal, grazes its stamina back, stays in its paddock,
 *      is spooked by a player in its face, bolts, and says so on REFLUX (MISHRI's mood is the input).
 *   8. The wire: PACKET_HORSE_STATE round-trips a horse (and removes one the packet stopped
 *      mentioning), PACKET_HORSE_EVENTS relays the horse REFLUX family, and anything outside that
 *      family is refused rather than re-dispatched.
 *
 * Build/run: make test-horse. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "physics.h"
#include "local_game.h"
#include "horse_rules.h"
#include "horse_rules_host.h"
#include "horse_goals.h"
#include "horse_goals_host.h"
#include "horse_sync.h"
#include "horse_api.h"
#include "../reflux/reflux_runtime.h"
#include "level_boxes.h"

/* local_game.h declares these; apps/server and apps/lobby each define their own copy, and so does
 * this test (it links the simulation without either binary's main). */
int g_story_cutscene_done = 0;
int g_story_outro_requested = 0;
int g_shankpit_is_server = 0;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static PlayerState *fresh_player(void) {
    memset(&local_state, 0, sizeof local_state);
    map_count = 1;
    PlayerState *p = &local_state.players[0];
    p->active = 1;
    p->id = 0;
    p->scene_id = SCENE_CUSTOM_LEVEL;
    p->state = STATE_ALIVE;
    return p;
}

/* The tick takes a clock now (MISHRI's mood timers and a horse's own goal deadlines are real time),
 * so the test owns one and advances it by a fixed 16 ms per tick -- the client's own cadence. */
static unsigned int test_now_ms = 0;
static void tick(int n) {
    for (int i = 0; i < n; i++) { test_now_ms += 16; horse_tick_all(test_now_ms); }
}

/* A capturing REFLUX hook: local_game.h deliberately never names reflux_host_dispatch (it would drag
 * the runtime into every test and training binary), so the host installs one. The test installs this
 * and asserts on what the horse actually dispatched. */
static int cap_type[128], cap_a[128], cap_b[128], cap_c[128], cap_n = 0;
static void capture_hook(int type, int a, int b, int c) {
    if (cap_n >= 128) return;
    cap_type[cap_n] = type; cap_a[cap_n] = a; cap_b[cap_n] = b; cap_c[cap_n] = c;
    cap_n++;
}
static int captured(int type) {
    for (int i = 0; i < cap_n; i++) if (cap_type[i] == type) return 1;
    return 0;
}

int main(void) {
    horse_rules_install();
    horse_goals_install();
    horse_reflux_install(capture_hook);

    /* ---- 1. the PARENA numbers are the live ones ---- */
    {
        CHECK(horse_rules()->jump_cost_milli() == 180, "jump cost %d", horse_rules()->jump_cost_milli());
        CHECK(horse_rules()->jump_speed_milli() == 5600, "jump speed %d", horse_rules()->jump_speed_milli());
        CHECK(horse_rules()->max_speed_milli() == 8000, "top speed %d", horse_rules()->max_speed_milli());
        CHECK(horse_rules()->carrot_restore_milli() == 400, "carrot %d", horse_rules()->carrot_restore_milli());
        CHECK(horse_can_jump(200, 1) == 1, "200 stamina on the ground must be allowed to jump");
        CHECK(horse_can_jump(199, 1) == 0, "199 stamina must not");
        CHECK(horse_can_jump(1000, 0) == 0, "airborne must not");
        CHECK(horse_fatigue_permille(249) == 620 && horse_fatigue_permille(250) == 1000,
              "fatigue breakpoint wrong: %d/%d", horse_fatigue_permille(249), horse_fatigue_permille(250));
        CHECK(horse_tolt_drain_milli(8000) == 90 && horse_tolt_drain_milli(0) == 0,
              "tölt drain curve wrong");
    }

    /* ---- 2. the level key parses and saves back ---- */
    {
        const char *doc =
            "{\"version\":1,\"name\":\"farm\",\"width\":40,\"height\":20,\"depth\":40,"
            "\"ground_plane_enabled\":true,\"walls\":[{\"x\":0,\"y\":0,\"z\":0,\"sx\":10,\"sy\":1,\"sz\":10}],"
            "\"horse_spawns\":[{\"x\":11.5,\"y\":0.1,\"z\":-22.25,\"yaw\":90},"
            "{\"x\":-4,\"y\":0.1,\"z\":8,\"yaw\":270}]}";
        CustomLevelData a, b;
        memset(&a, 0, sizeof a);
        CHECK(level_boxes_parse_json(doc, &a) == 1, "level with horse_spawns did not parse");
        CHECK(a.horse_spawn_count == 2, "parsed %d horse spawns", a.horse_spawn_count);
        CHECK(fabsf(a.horse_spawns[0].x - 11.5f) < 0.001f && fabsf(a.horse_spawns[0].z + 22.25f) < 0.001f,
              "first pad at (%.2f,%.2f)", a.horse_spawns[0].x, a.horse_spawns[0].z);
        CHECK(fabsf(a.horse_spawns[0].yaw - 90.0f) < 0.001f, "yaw %.2f", a.horse_spawns[0].yaw);
        CHECK(level_boxes_save_to_file("/tmp/horse_level.json", &a) == 1, "save failed");
        memset(&b, 0, sizeof b);
        CHECK(level_boxes_load_from_file("/tmp/horse_level.json", &b) == 1, "reload failed");
        CHECK(b.horse_spawn_count == a.horse_spawn_count, "round-trip dropped horse spawns (%d -> %d)",
              a.horse_spawn_count, b.horse_spawn_count);
        CHECK(fabsf(b.horse_spawns[1].yaw - 270.0f) < 0.001f, "round-trip yaw %.2f", b.horse_spawns[1].yaw);
    }

    /* ---- 3. scene_load places the authored horses, and does not leave stale ones behind ---- */
    {
        fresh_player();
        float x[2] = { 26.0f, -14.0f }, y[2] = { 0.1f, 0.1f }, z[2] = { 31.0f, -8.0f }, yaw[2] = { 45.0f, 200.0f };
        phys_set_custom_level_horse_spawns(x, y, z, yaw, 2);
        scene_load(SCENE_CUSTOM_LEVEL);
        int active = 0;
        for (int i = 0; i < MAX_HORSES; i++) if (local_state.horses[i].active) active++;
        CHECK(active == 2, "scene_load placed %d horses, wanted 2", active);
        CHECK(fabsf(local_state.horses[0].x - 26.0f) < 0.01f, "pad 0 x %.2f", local_state.horses[0].x);
        CHECK(fabsf(local_state.horses[1].yaw - 200.0f) < 0.01f, "pad 1 yaw %.2f", local_state.horses[1].yaw);
        /* A second load with no authored horses must clear them, not keep them standing. */
        phys_set_custom_level_horse_spawns(x, y, z, yaw, 0);
        scene_load(SCENE_CUSTOM_LEVEL);
        active = 0;
        for (int i = 0; i < MAX_HORSES; i++) if (local_state.horses[i].active) active++;
        CHECK(active == 0, "scene change left %d stale horses", active);
    }

    /* ---- 4. a fresh horse: full bar, two carrots, unicorn skin ---- */
    {
        PlayerState *p = fresh_player();
        HorseState *h = &local_state.horses[0];
        horse_spawn_defaults(h, 0, p->scene_id, 0.0f, 0.0f, 0.0f);
        CHECK(h->stamina_milli == HORSE_STAMINA_MAX_MILLI, "stamina %d", h->stamina_milli);
        CHECK(h->carrots == HORSE_START_CARROTS, "carrots %d", h->carrots);
        CHECK(h->skin == HORSE_SKIN_UNICORN, "skin %d", h->skin);
        CHECK(h->grounded == 1, "a spawned horse must be on the ground");

        /* ---- 5a. tölt burns stamina ---- */
        CHECK(horse_try_enter(p, h) == 1, "could not mount");
        p->in_fwd = 1.0f;
        tick(240); /* four seconds of tölt */
        CHECK(h->speed > 1.0f, "horse barely moved (%.2f m/s)", h->speed);
        CHECK(h->stamina_milli < HORSE_STAMINA_MAX_MILLI, "tölt did not cost stamina (%d)", h->stamina_milli);

        /* ---- 5b. the jump: leaves the ground, comes back, costs 180 ---- */
        int before = h->stamina_milli;
        h->jump_cooldown = 0;
        p->in_jump = 1;
        tick(1);
        p->in_jump = 0;
        CHECK(h->jump_vy > 0.0f, "jump produced no upward velocity (%.2f)", h->jump_vy);
        CHECK(h->grounded == 0, "jump did not leave the ground");
        CHECK(before - h->stamina_milli >= 170, "jump cost %d", before - h->stamina_milli);
        float peak = h->y;
        int air = 0;
        for (int i = 0; i < 200 && !h->grounded; i++) {
            tick(1);
            if (h->y > peak) peak = h->y;
            air++;
        }
        CHECK(h->grounded == 1, "the horse never landed");
        CHECK(air > 10, "the hop lasted %d ticks -- not a jump", air);
        CHECK(peak > 1.3f, "peak height %.2f -- the hop never rose", peak);

        /* ---- 5c. a carrot refills part of the bar and is eaten ---- */
        int carrots_before = h->carrots;
        int low = h->stamina_milli = 200; /* a tired horse */
        h->feed_cooldown = 0;
        p->in_ability2 = 1;
        tick(1);
        p->in_ability2 = 0;
        CHECK(h->carrots == carrots_before - 1, "carrot not consumed (%d)", h->carrots);
        CHECK(h->stamina_milli >= low + 300, "carrot only restored %d", h->stamina_milli - low);

        /* ---- 5d. blown: 62% of top speed, still moving ---- */
        h->stamina_milli = 50;
        h->speed = 0.0f;
        p->in_fwd = 1.0f;
        tick(600);
        CHECK(h->speed <= 8.0f * 0.62f + 0.01f, "a blown horse reached %.2f m/s", h->speed);
        CHECK(h->speed > 3.0f, "a blown horse should still jog, got %.2f", h->speed);
    }

    /* ---- 6. PARENA owns what a loose horse decides ---- */
    {
        CHECK(horse_goals()->goal_pick(100, 200, 0) == HORSE_GOAL_GRAZE, "a hungry horse must eat first");
        CHECK(horse_goals()->goal_pick(100, 1000, 6) == HORSE_GOAL_HOME, "a startled horse must go home");
        CHECK(horse_goals()->goal_pick(100, 1000, 4) == HORSE_GOAL_IDLE, "a social horse must stand");
        CHECK(horse_goals()->goal_pick(100, 1000, 0) == HORSE_GOAL_GRAZE, "roll 100 should graze");
        CHECK(horse_goals()->goal_pick(500, 1000, 0) == HORSE_GOAL_WANDER, "roll 500 should wander");
        CHECK(horse_goals()->goal_pick(900, 1000, 0) == HORSE_GOAL_IDLE, "roll 900 should stand");
        CHECK(horse_goals()->goal_duration_ms(HORSE_GOAL_GRAZE) == 14000, "graze duration %d",
              horse_goals()->goal_duration_ms(HORSE_GOAL_GRAZE));
        CHECK(horse_goals()->goal_speed_milli(HORSE_GOAL_FLEE) > horse_goals()->goal_speed_milli(HORSE_GOAL_WANDER),
              "a bolting horse must be faster than an ambling one");
        CHECK(horse_goals()->goal_speed_milli(HORSE_GOAL_IDLE) == 0, "idle must not walk");
        CHECK(horse_goals()->goal_radius_milli(HORSE_GOAL_GRAZE) < horse_goals()->goal_radius_milli(HORSE_GOAL_WANDER),
              "grazing must stay closer to the pad than wandering");
        CHECK(horse_goals()->graze_restore_milli(0) > horse_goals()->graze_restore_milli(6),
              "a nervous horse must eat less");
    }

    /* ---- 7. a loose horse has an afternoon, and MISHRI's mood colours it ---- */
    {
        PlayerState *p = fresh_player();
        HorseState *h = &local_state.horses[0];
        horse_spawn_defaults(h, 0, p->scene_id, 100.0f, 100.0f, 0.0f);
        CHECK(captured(REFLUX_ACTION_HORSE_SPAWNED), "a spawn must say so on REFLUX");
        h->stamina_milli = 200;   /* hungry: it should choose to graze */
        cap_n = 0;
        tick(40);                 /* past the MISHRI reaction window */
        CHECK(h->goal == HORSE_GOAL_GRAZE, "a hungry loose horse picked goal %d", h->goal);
        CHECK(h->goal_until_ms > 0, "a goal must have a deadline");
        CHECK(captured(REFLUX_ACTION_HORSE_GOAL_CHANGED), "a goal change must say so on REFLUX");
        int before = h->stamina_milli;
        tick(120);
        CHECK(h->stamina_milli > before, "grazing did not refill the bar (%d -> %d)", before, h->stamina_milli);
        float drift = sqrtf((h->x - 100.0f) * (h->x - 100.0f) + (h->z - 100.0f) * (h->z - 100.0f));
        CHECK(drift < 20.0f, "a grazing horse drifted %.1f m from its pad", drift);

        /* a player walks up to it: it bolts, and says so */
        p->x = h->x + 2.0f;
        p->z = h->z;
        cap_n = 0;
        tick(2);
        CHECK(h->frightened == 1, "a horse must be spooked by a player in its face");
        CHECK(captured(REFLUX_ACTION_HORSE_STARTLED), "no REFLUX_ACTION_HORSE_STARTLED was dispatched");
        CHECK(h->goal == HORSE_GOAL_FLEE, "a spooked horse must flee, goal=%d", h->goal);
    }

    /* ---- 8. the wire: state round-trips, events relay, junk is refused ---- */
    {
        ServerState sender, receiver;
        memset(&sender, 0, sizeof sender);
        memset(&receiver, 0, sizeof receiver);
        horse_spawn_defaults(&sender.horses[2], 2, SCENE_CUSTOM_LEVEL, 12.5f, -3.0f, 90.0f);
        sender.horses[2].goal = HORSE_GOAL_WANDER;
        sender.horses[2].stamina_milli = 640;
        sender.horses[2].carrots = 1;
        sender.horses[2].occupant_player_id = -1;

        NetHorseEntry rows[NET_HORSE_MAX_ENTRIES];
        int n = horse_sync_fill(&sender, SCENE_CUSTOM_LEVEL, rows, NET_HORSE_MAX_ENTRIES);
        CHECK(n == 1, "fill wrote %d rows", n);
        CHECK(rows[0].slot == 2 && rows[0].goal == HORSE_GOAL_WANDER, "entry lost its identity");
        CHECK(rows[0].occupant == -1, "a loose horse must travel as -1, got %d", (int)rows[0].occupant);

        NetHorseState pkt;
        memset(&pkt, 0, sizeof pkt);
        pkt.hdr.type = PACKET_HORSE_STATE;
        pkt.hdr.scene_id = SCENE_CUSTOM_LEVEL;
        pkt.count = (unsigned char)n;
        memcpy(pkt.e, rows, sizeof(NetHorseEntry) * (size_t)n);
        int len = (int)(sizeof(NetHeader) + 4 + sizeof(NetHorseEntry) * (size_t)n);
        CHECK(horse_sync_apply(&receiver, &pkt, len) == 1, "apply did not land the horse");
        CHECK(fabsf(receiver.horses[2].x - 12.5f) < 0.001f && receiver.horses[2].stamina_milli == 640,
              "round-trip lost position or stamina");
        CHECK(receiver.horses[2].skin == HORSE_SKIN_UNICORN, "skin lost in transit");
        /* a horse the packet no longer mentions is gone (whole-scene semantics) */
        pkt.count = 0;
        horse_sync_apply(&receiver, &pkt, (int)(sizeof(NetHeader) + 4));
        CHECK(receiver.horses[2].active == 0, "a horse dropped from the packet must be removed");

        /* events: the family is relayed, anything else is refused */
        reflux_host_reset();
        NetHorseEvent ev[2];
        ev[0].type = REFLUX_ACTION_HORSE_MOUNTED; ev[0].a = 2; ev[0].b = 7; ev[0].c = 0;
        ev[1].type = REFLUX_ACTION_BUTTON_PRESSED; ev[1].a = 99; ev[1].b = 99; ev[1].c = 99;
        CHECK(horse_sync_apply_events(ev, 2) == 1, "only the horse event should be accepted");
        CHECK(reflux_host_log_size() == 1, "log has %d entries, wanted 1", reflux_host_log_size());
        CHECK(reflux_host_action_type_at(0) == REFLUX_ACTION_HORSE_MOUNTED, "wrong event relayed");

        /* and the relayed event reaches a mod's own cursor */
        HorseRefluxCursor cur = { 0 };
        reflux_host_reset();
        horse_sync_apply_events(ev, 1);
        HorseEvent got[4];
        int polled = horse_api_poll(&cur, got, 4);
        CHECK(polled == 1 && got[0].type == REFLUX_ACTION_HORSE_MOUNTED,
              "a mod's own poll saw %d horse events", polled);
    }

    if (fails) { printf("horse_test: %d FAILURE(S)\n", fails); return 1; }
    printf("horse_test: ok -- parses pads, spawns unicorn actors, tölt burns stamina, jump hops (not flies), "
           "carrot restores, blown slows, a loose horse grazes/bolts on PARENA+MISHRI goals, "
           "and the state + REFLUX events round-trip the wire\n");
    return 0;
}
