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
 *   9. A fence is a fence: a loose horse slides along it instead of walking through it.
 *  10. The relay drains in batches, in order, and survives the log wrapping past its capacity.
 *  11. The request channel: what a client (or a mod on one) may ask a horse to do -- mount,
 *      dismount, set-goal, feed -- and every refusal, judged by the same function the server runs.
 *  12. The ring's residual bound is MEASURABLE: a cursor overtaken by the log says how many events
 *      it lost, instead of skipping them silently.
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
#include "horse_interact.h"
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

    /* ---- 9. a fence is a fence: a loose horse slides along it instead of walking through it ---- */
    {
        PlayerState *p = fresh_player();
        /* one wall, 200 across, 20 tall, 4 deep, centred on the origin. Index 0 is the deliberate
           dummy every shared map_geo consumer skips (see physics.h). */
        static const Box wall[] = {
            { 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
            { 0.0f, 0.0f, 0.0f, 200.0f, 20.0f, 4.0f }
        };
        map_geo = wall;
        map_count = 2;
        p->x = 500.0f;               /* the player stands well clear: this test is about a fence, not a spook */
        p->z = 500.0f;
        HorseState *h = &local_state.horses[0];
        horse_spawn_defaults(h, 0, p->scene_id, 0.0f, -8.0f, 0.0f);
        h->home_x = 0.0f;
        h->home_z = 8.0f;            /* its paddock is on the FAR side of the fence */
        h->goal = HORSE_GOAL_HOME;
        h->goal_x = h->home_x;
        h->goal_z = h->home_z;
        h->goal_until_ms = test_now_ms + 60000;
        h->goal_act_at_ms = test_now_ms;
        h->stamina_milli = 900;
        tick(400);                   /* ~6.4 s of walking straight at it */
        CHECK(h->z < -1.5f, "the horse walked through the fence (z=%.2f)", h->z);

        /* the same horse with the fence gone still gets where it is going */
        map_count = 1;               /* dummy index 0 only: nothing to hit */
        tick(400);
        CHECK(h->z > -1.5f, "with no fence in the way the horse should have crossed z=0, it is at %.2f", h->z);

        map_geo = map_geo_stadium;
        map_count = 1;
    }

    /* ---- 10. the relay drains in batches, in order, and survives the log wrapping ---- */
    {
        reflux_host_reset();
        for (int i = 0; i < 20; i++)
            reflux_host_dispatch(REFLUX_ACTION_HORSE_GOAL_CHANGED, i, HORSE_GOAL_WANDER, 0);
        NetHorseEvent batch[NET_HORSE_EVENT_MAX];
        int cursor = 0;              /* a cursor at 0 delivers from the oldest retained row */
        int got = 0, order_ok = 1, next_expected = 0;
        for (int guard = 0; guard < 32; guard++) {
            int n = horse_sync_collect_events(batch, NET_HORSE_EVENT_MAX, &cursor);
            if (n <= 0) break;
            for (int i = 0; i < n; i++) {
                if (batch[i].a != next_expected) order_ok = 0;
                next_expected++;
            }
            got += n;
        }
        CHECK(got == 20, "the relay delivered %d of 20 events", got);
        CHECK(order_ok, "the relay reordered events");

        /* past the ring's own capacity: the drain terminates and delivers what is still retained */
        reflux_host_reset();
        for (int i = 0; i < REFLUX_LOG_CAPACITY + 44; i++)
            reflux_host_dispatch(REFLUX_ACTION_HORSE_MOUNTED, i, 0, 0);
        cursor = 0;
        int drained = 0;
        for (int guard = 0; guard < 128; guard++) {
            int n = horse_sync_collect_events(batch, NET_HORSE_EVENT_MAX, &cursor);
            if (n <= 0) break;
            drained += n;
        }
        CHECK(drained == REFLUX_LOG_CAPACITY, "a wrapped log drained %d rows, wanted %d",
              drained, REFLUX_LOG_CAPACITY);

        /* and a fresh cursor is a baseline, never a replay of the whole log */
        HorseRefluxCursor fresh = { -1 };
        HorseEvent poll[4];
        CHECK(horse_api_poll(&fresh, poll, 4) == 0, "a fresh cursor must not replay the log");
    }

    /* ---- 11. the request channel: mount, dismount, set-goal, feed ---- */
    {
        PlayerState *p = fresh_player();
        HorseState *h = &local_state.horses[0];
        horse_spawn_defaults(h, 0, p->scene_id, 0.0f, 0.0f, 0.0f);
        p->x = 1.0f; p->y = 0.0f; p->z = 0.0f;   /* an arm's length away */

        /* a goal that is not HORSE_GOAL_* is a refusal, never a horse told something undefined */
        CHECK(horse_interact_apply(&local_state, p, 0, HORSE_INTERACT_SET_GOAL, 999, test_now_ms)
                  == HORSE_INTERACT_REFUSED_BAD_GOAL, "goal 999 was not refused");
        CHECK(horse_interact_apply(&local_state, p, 0, HORSE_INTERACT_SET_GOAL,
                                   HORSE_GOAL_HOME, test_now_ms) == HORSE_INTERACT_OK,
              "a valid goal within reach was refused");
        CHECK(h->goal == HORSE_GOAL_HOME, "the accepted goal did not take (goal=%d)", h->goal);

        /* 5 m is the arm, the same one the local E key gives */
        p->x = 50.0f;
        CHECK(horse_interact_apply(&local_state, p, 0, HORSE_INTERACT_SET_GOAL,
                                   HORSE_GOAL_GRAZE, test_now_ms) == HORSE_INTERACT_REFUSED_OUT_OF_REACH,
              "a horse 50 m away accepted a request");
        CHECK(horse_interact_apply(&local_state, p, 0, HORSE_INTERACT_FEED, 0, test_now_ms)
                  == HORSE_INTERACT_REFUSED_OUT_OF_REACH, "a carrot from 50 m away was accepted");
        p->x = 1.0f;

        /* someone else is riding it: no mount, and no leading it out from under them */
        h->occupant_player_id = 5;
        CHECK(horse_interact_apply(&local_state, p, 0, HORSE_INTERACT_MOUNT, 0, test_now_ms)
                  == HORSE_INTERACT_REFUSED_BUSY, "a ridden horse accepted a mount");
        CHECK(horse_interact_apply(&local_state, p, 0, HORSE_INTERACT_SET_GOAL,
                                   HORSE_GOAL_FLEE, test_now_ms) == HORSE_INTERACT_REFUSED_BUSY,
              "a bystander re-goaled a ridden horse");
        h->stamina_milli = 100;   /* a full bar refuses a carrot on purpose -- give it something to eat for */
        CHECK(horse_interact_apply(&local_state, p, 0, HORSE_INTERACT_FEED, 0, test_now_ms)
                  == HORSE_INTERACT_OK, "a carrot to a ridden horse should be allowed");
        h->occupant_player_id = -1;

        /* mount, then a stranger cannot dismount it, then the rider can */
        CHECK(horse_interact_apply(&local_state, p, 0, HORSE_INTERACT_MOUNT, 0, test_now_ms)
                  == HORSE_INTERACT_OK, "the mount was refused");
        CHECK(h->occupant_player_id == p->id, "the mount did not seat the caller");
        h->occupant_player_id = 5;
        CHECK(horse_interact_apply(&local_state, p, 0, HORSE_INTERACT_DISMOUNT, 0, test_now_ms)
                  == HORSE_INTERACT_REFUSED_NOT_RIDER, "a non-rider dismounted the horse");
        h->occupant_player_id = p->id;
        CHECK(horse_interact_apply(&local_state, p, 0, HORSE_INTERACT_DISMOUNT, 0, test_now_ms)
                  == HORSE_INTERACT_OK, "the rider could not dismount");
        CHECK(h->occupant_player_id < 0, "the dismount left the rider on the horse");

        /* feed: a carrot travels, an empty saddlebag refuses */
        h->stamina_milli = 100;
        h->carrots = 2;
        int fed = horse_interact_apply(&local_state, p, 0, HORSE_INTERACT_FEED, 0, test_now_ms);
        CHECK(fed == HORSE_INTERACT_OK && h->carrots == 1 && h->stamina_milli > 100,
              "feed refused (%d) or lost the carrot (%d carrots, %d stamina)",
              fed, h->carrots, h->stamina_milli);
        h->carrots = 0;
        h->stamina_milli = 100;
        CHECK(horse_interact_apply(&local_state, p, 0, HORSE_INTERACT_FEED, 0, test_now_ms)
                  == HORSE_INTERACT_REFUSED_NO_CARROT, "an empty saddlebag still fed the horse");

        /* a horse in another scene is not described to you, so it cannot be requested either */
        h->scene_id = SCENE_STADIUM;
        CHECK(horse_interact_apply(&local_state, p, 0, HORSE_INTERACT_MOUNT, 0, test_now_ms)
                  == HORSE_INTERACT_REFUSED_NOTHERE, "a horse in another scene was mountable");
        h->scene_id = p->scene_id;

        /* the request carries the goal, so the server never has to guess what was meant */
        NetHorseInteract pkt;
        horse_interact_build(&pkt, 7, 2, HORSE_INTERACT_SET_GOAL, HORSE_GOAL_FLEE);
        CHECK(pkt.hdr.type == PACKET_HORSE_INTERACT && pkt.hdr.client_id == 7 && pkt.slot == 2 &&
                  pkt.action == HORSE_INTERACT_SET_GOAL && pkt.goal == HORSE_GOAL_FLEE,
              "the request packet lost a field");
        /* out-of-range fields are clipped into the packet's own bytes rather than truncated into a
           different valid-looking value: -3 is not client 3, and -9 is not goal 247 */
        horse_interact_build(&pkt, -3, 999, HORSE_INTERACT_MOUNT, -9);
        CHECK(pkt.hdr.client_id == 0 && pkt.slot == (999 & 0xff) && pkt.goal == 0,
              "out-of-range fields were not clipped (id=%u slot=%u goal=%u)",
              (unsigned)pkt.hdr.client_id, (unsigned)pkt.slot, (unsigned)pkt.goal);
    }

    /* ---- 12. the ring's residual bound is measurable, not silent ---- */
    {
        reflux_host_reset();
        for (int i = 0; i < REFLUX_LOG_CAPACITY + 10; i++)
            reflux_host_dispatch(REFLUX_ACTION_HORSE_JUMPED, i, 0, 0);
        CHECK(horse_sync_missed(0) == 10, "a cursor overtaken by 10 rows reported %d lost",
              horse_sync_missed(0));
        CHECK(horse_sync_missed(reflux_host_total_dispatched()) == 0,
              "an up-to-date cursor must not report a loss");
        CHECK(horse_sync_missed(-1) == 0, "a fresh cursor has no history to lose");
    }

    if (fails) { printf("horse_test: %d FAILURE(S)\n", fails); return 1; }
    printf("horse_test: ok -- parses pads, spawns unicorn actors, tölt burns stamina, jump hops (not flies), "
           "carrot restores, blown slows, a loose horse grazes/bolts on PARENA+MISHRI goals, "
           "slides along a fence instead of through it, and the state + REFLUX events round-trip the "
           "wire in batches past the log's own wrap; the request channel judges mount/dismount/"
           "set-goal/feed by reach, scene and occupancy, and a relay that lost rows says how many\n");
    return 0;
}
