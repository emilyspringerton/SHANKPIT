/* citizen_actor_test.c -- the MODE_ZOMBIES citizen body, end to end, headlessly.
 *
 * What this proves, and what it deliberately cannot: the PARENA table and its built-in C mirror
 * agree field for field; the heuristic + hand-written-policy blend picks the designed action for a
 * spread of situations; a jump actually leaves the ground and obeys its cooldown; a slide crouches
 * the body and holds its own duration; a slide-jump leaves with exactly the extra vertical speed
 * PARENA says; a citizen draws a weapon when a threat is in range (and holsters when it is gone);
 * and each of those dispatches its REFLUX action. It does NOT prove how any of it feels, because
 * there is no display on this box -- that is named in the changelog, not implied here.
 *
 * Build/run: `make test-citizen-actor`. */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../reflux/reflux_runtime.h"
#include "citizen_actor.h"
#include "citizen_actor_rules_host.h"
#include "citizen_sync.h"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void fresh_body(PlayerState *p, int id, int health) {
    memset(p, 0, sizeof(*p));
    p->id = id;
    p->active = 1;
    p->state = STATE_ALIVE;
    p->health = health;
    p->on_ground = 1;
    p->scene_id = SCENE_VOXWORLD;
    p->current_weapon = WPN_KNIFE;
}

int main(void) {
    printf("-- PARENA citizen actor rules == built-in mirror --\n");
    {
        const int dists[] = {0, 2000, 2500, 5000, 6000, 13000, 14000, 30000};
        const int healths[] = {1000, 400, 900};
        const int speeds[] = {0, 400, 750, 900, 1000};
        int mismatches = 0;
        for (int action = 0; action < CITACT_COUNT; action++)
            for (unsigned d = 0; d < sizeof(dists) / sizeof(dists[0]); d++)
                for (int threat = 0; threat < 2; threat++)
                    for (unsigned h = 0; h < sizeof(healths) / sizeof(healths[0]); h++)
                        for (unsigned s = 0; s < sizeof(speeds) / sizeof(speeds[0]); s++)
                            for (int ready = 0; ready < 2; ready++) {
                                int want = citizen_heuristic_score(action, dists[d], threat, healths[h],
                                                                   speeds[s], ready);
                                int got = g_citizen_actor_builtin.heuristic_score(action, dists[d], threat,
                                                                                  healths[h], speeds[s], ready);
                                if (want != got) mismatches++;
                                for (int aggression = 0; aggression <= 1000; aggression += 250) {
                                    want = citizen_blended_score(action, dists[d], threat, healths[h],
                                                                 speeds[s], ready, aggression);
                                    got = g_citizen_actor_builtin.blended_score(action, dists[d], threat,
                                                                                healths[h], speeds[s], ready,
                                                                                aggression);
                                    if (want != got) mismatches++;
                                }
                            }
        for (int proximity = 0; proximity <= 1000; proximity += 100)
            for (int hurt = 0; hurt <= 1000; hurt += 250)
                for (int threat = 0; threat < 2; threat++)
                    for (int ready = 0; ready < 2; ready++)
                        for (int speed = 0; speed <= 1000; speed += 250)
                            for (int ground = 0; ground < 2; ground++) {
                                int want = citizen_policy_aggression(proximity, hurt, threat, ready, speed, ground);
                                int got = g_citizen_actor_builtin.policy_aggression(proximity, hurt, threat,
                                                                                    ready, speed, ground);
                                if (want != got) mismatches++;
                            }
        CHECK(mismatches == 0, "%d field(s) differ between citizen_actor_rules.prn and citizen_actor_rules.h", mismatches);
        CHECK(citizen_jump_vy_milli() == 950, "jump vy %d", citizen_jump_vy_milli());
        CHECK(citizen_slide_speed_milli() == 750, "slide speed %d", citizen_slide_speed_milli());
        CHECK(citizen_slide_jump_boost_milli() == 200, "slide-jump boost %d", citizen_slide_jump_boost_milli());
        printf("   PARENA: jump %d milli, cooldown %d ms, slide at %d milli, slide-jump +%d\n",
               citizen_jump_vy_milli(), citizen_jump_cooldown_ms(), citizen_slide_speed_milli(),
               citizen_slide_jump_boost_milli());
    }

    printf("-- the blend picks the designed action --\n");
    {
        const CitizenActorRules *r = citizen_actor_rules();
        /* no threat at all: a citizen's afternoon is a walk */
        CHECK(r->pick(0, 0, 1000, 0, 1, 500) == CITACT_WALK, "idle/quiet pick %d",
              r->pick(0, 0, 1000, 0, 1, 500));
        /* hurt, on the ground, zombie 6 m away: leave the ground */
        CHECK(r->pick(6000, 1, 300, 0, 1, 685) == CITACT_JUMP, "hurt close pick %d",
              r->pick(6000, 1, 300, 0, 1, 685));
        /* already sprinting, zombie 2 m away, jump on cooldown: slide under it */
        CHECK(r->pick(2000, 1, 1000, 900, 0, 548) == CITACT_SLIDE, "fast close pick %d",
              r->pick(2000, 1, 1000, 900, 0, 548));
        /* sprinting with a jump available: slide-jump, the founder's own move */
        CHECK(r->pick(4000, 1, 1000, 900, 1, 642) == CITACT_SLIDE_JUMP, "slide-jump pick %d",
              r->pick(4000, 1, 1000, 900, 1, 642));
        /* standing, zombie 5 m away: draw and fire */
        CHECK(r->pick(5000, 1, 1000, 0, 1, 565) == CITACT_SHOOT, "shoot pick %d",
              r->pick(5000, 1, 1000, 0, 1, 565));
        /* a zombie far off: run, do not shoot across the map */
        CHECK(r->pick(20000, 1, 1000, 0, 1, 360) == CITACT_RUN, "far pick %d",
              r->pick(20000, 1, 1000, 0, 1, 360));
        /* every pick is a real action id */
        for (int dist = 0; dist <= 30000; dist += 1000)
            for (int threat = 0; threat < 2; threat++)
                for (int speed = 0; speed <= 1000; speed += 500) {
                    int a = r->pick(dist, threat, 800, speed, 1, 500);
                    CHECK(a >= 0 && a < CITACT_COUNT, "pick returned %d", a);
                }
        /* the policy moves the way its weights say: closer, hurt, threatened, able, fast, grounded */
        CHECK(r->policy_aggression(1000, 0, 1, 1, 0, 1) > r->policy_aggression(0, 0, 1, 1, 0, 1),
              "proximity did not raise aggression");
        CHECK(r->policy_aggression(500, 1000, 1, 1, 0, 1) > r->policy_aggression(500, 0, 1, 1, 0, 1),
              "hurt did not raise aggression");
        CHECK(r->policy_aggression(500, 0, 1, 0, 0, 1) > r->policy_aggression(500, 0, 0, 0, 0, 1),
              "a threat did not raise aggression");
        CHECK(citizen_actor_action_name(CITACT_SLIDE_JUMP)[0] == 's', "action name missing");
    }

    printf("-- a jump leaves the ground, then obeys its cooldown --\n");
    {
        PlayerState p;
        fresh_body(&p, 2, 30);
        citizen_actor_tick(2, &p, 1, 6000, 0);
        CHECK(citizen_actor_action(2) == CITACT_JUMP, "action %d wanted jump", citizen_actor_action(2));
        citizen_actor_apply(2, &p, 0);
        CHECK(p.y > 0.0f, "jump left y=%f", p.y);
        CHECK(fabsf(p.vy - 0.95f) < 0.001f, "jump vy %f, wanted PARENA's 0.95", p.vy);
        CHECK(citizen_actor_actions_taken() >= 1, "no action was ever counted");

        /* one millisecond later the body may not jump again: whatever it decides instead, the
           cooldown has to hold and nothing may leave the ground */
        p.y = 0.0f; p.vy = 0.0f; p.in_jump = 0;
        citizen_actor_tick(2, &p, 1, 6000, 1);
        CHECK(citizen_actor_action(2) != CITACT_JUMP, "jumped again %d ms after a jump", 1);
        CHECK(citizen_actor_action(2) != CITACT_SLIDE_JUMP, "slide-jumped inside the cooldown");
        citizen_actor_apply(2, &p, 1);
        CHECK(fabsf(p.vy) < 0.0001f, "cooldown still applied an impulse: vy=%f", p.vy);
        CHECK(fabsf(p.y) < 0.0001f, "cooldown still lifted the body: y=%f", p.y);

        /* and exactly at the cooldown it may jump again */
        citizen_actor_tick(2, &p, 1, 6000, (unsigned int)citizen_jump_cooldown_ms());
        CHECK(citizen_actor_action(2) == CITACT_JUMP, "post-cooldown action %d", citizen_actor_action(2));
    }

    printf("-- a slide crouches, holds its duration, and a slide-jump leaves faster --\n");
    {
        PlayerState p;
        fresh_body(&p, 3, 30);
        citizen_actor_tick(3, &p, 1, 6000, 0);   /* jump first, to spend the cooldown */
        p.vx = 0.9f;
        citizen_actor_tick(3, &p, 1, 2000, 10);
        CHECK(citizen_actor_action(3) == CITACT_SLIDE, "action %d wanted slide", citizen_actor_action(3));
        citizen_actor_apply(3, &p, 10);
        CHECK(p.crouching == 1, "slide did not crouch the body");
        int held = 0;
        for (unsigned int t = 11; t < 10u + (unsigned int)citizen_slide_min_ms(); t += 20) {
            citizen_actor_tick(3, &p, 0, 0, t);
            if (citizen_actor_action(3) == CITACT_SLIDE) held++;
        }
        CHECK(held > 0, "the slide ended immediately instead of holding its minimum");

        /* and when it is over the body stands up -- a latched crouch would be worse than no slide */
        citizen_actor_tick(3, &p, 0, 0, 2000);
        CHECK(citizen_actor_action(3) == CITACT_WALK, "post-slide action %d", citizen_actor_action(3));
        citizen_actor_apply(3, &p, 2000);
        CHECK(p.crouching == 0, "the body stayed crouched after the slide ended");

        PlayerState q;
        fresh_body(&q, 4, 100);
        q.vx = 0.9f;
        citizen_actor_tick(4, &q, 1, 4000, 0);
        CHECK(citizen_actor_action(4) == CITACT_SLIDE_JUMP, "action %d wanted slide-jump",
              citizen_actor_action(4));
        citizen_actor_apply(4, &q, 0);
        CHECK(fabsf(q.vy - 1.15f) < 0.001f, "slide-jump vy %f, wanted 0.95 + PARENA's 0.20", q.vy);
        CHECK(q.y > 0.0f, "slide-jump did not leave the ground");
    }

    printf("-- a weapon is drawn for a threat and holstered after it --\n");
    {
        PlayerState p;
        fresh_body(&p, 5, 100);
        citizen_actor_tick(5, &p, 1, 5000, 0);
        CHECK(citizen_actor_is_wielding(5) == 1, "no weapon drawn for a zombie 5 m away");
        CHECK(p.current_weapon == WPN_AR, "weapon %d, wanted the rifle", p.current_weapon);
        CHECK(p.ammo[WPN_AR] > 0, "drawn weapon had no ammo");
        CHECK(citizen_actor_action(5) == CITACT_SHOOT, "action %d wanted shoot", citizen_actor_action(5));
        citizen_actor_apply(5, &p, 0);
        CHECK(p.in_shoot == 1, "shoot action did not pull the trigger");

        citizen_actor_tick(5, &p, 0, 0, 10);
        CHECK(citizen_actor_is_wielding(5) == 0, "weapon stayed drawn with no threat");
        CHECK(p.current_weapon == WPN_KNIFE, "holstered to %d, wanted the knife slot", p.current_weapon);
        citizen_actor_apply(5, &p, 10);
        CHECK(p.in_shoot == 0, "the trigger stayed held after the threat was gone");
    }

    printf("-- every one of those is a REFLUX action --\n");
    {
        reflux_host_reset();
        PlayerState p;
        fresh_body(&p, 6, 30);
        citizen_actor_tick(6, &p, 1, 6000, 0);
        int saw_jump = 0, saw_wield = 0;
        for (int i = 0; i < reflux_host_log_size(); i++) {
            int t = reflux_host_action_type_at(i);
            if (t == REFLUX_ACTION_CITIZEN_JUMPED) saw_jump = 1;
            if (t == REFLUX_ACTION_CITIZEN_WIELDED) saw_wield = 1;
            CHECK(reflux_action_is_citizen(t), "unrelated action %d in the citizen's own block", t);
        }
        CHECK(saw_jump, "no REFLUX_ACTION_CITIZEN_JUMPED was dispatched");
        CHECK(saw_wield, "no REFLUX_ACTION_CITIZEN_WIELDED was dispatched");

        reflux_host_reset();
        PlayerState q;
        fresh_body(&q, 7, 100);
        q.vx = 0.9f;
        citizen_actor_tick(7, &q, 1, 4000, 0);
        int saw_slide_jump = 0;
        for (int i = 0; i < reflux_host_log_size(); i++)
            if (reflux_host_action_type_at(i) == REFLUX_ACTION_CITIZEN_SLIDE_JUMPED) saw_slide_jump = 1;
        CHECK(saw_slide_jump, "no REFLUX_ACTION_CITIZEN_SLIDE_JUMPED was dispatched");
        CHECK(reflux_action_is_citizen(REFLUX_ACTION_CITIZEN_FIRED), "family predicate misses FIRED");
        CHECK(!reflux_action_is_citizen(REFLUX_ACTION_HORSE_POOPED), "family predicate swallowed the horses");
    }

    printf("-- the relay carries the family, in order, and refuses anything else --\n");
    {
        reflux_host_reset();
        PlayerState p;
        fresh_body(&p, 8, 100);
        p.vx = 0.9f;
        citizen_actor_tick(8, &p, 1, 4000, 0);      /* one slide-jump and one wield */
        int total = reflux_host_total_dispatched();
        CHECK(total >= 2, "the relay had only %d row(s) to carry", total);

        /* a fresh cursor baselines rather than replaying host history at a new peer */
        NetCitizenEvent rows[NET_CITIZEN_EVENT_MAX];
        int fresh = -1;
        CHECK(citizen_sync_collect_events(rows, NET_CITIZEN_EVENT_MAX, &fresh) == 0,
              "a fresh cursor replayed history");
        CHECK(citizen_sync_collect_events(rows, NET_CITIZEN_EVENT_MAX, &fresh) == 0,
              "the baseline cursor delivered a row on a second call");
        CHECK(citizen_sync_missed(fresh) == 0, "a fresh cursor claimed to have lost rows");

        /* everything already dispatched, from the very start, is citizen family and nothing else */
        int cursor = 0, n = citizen_sync_collect_events(rows, NET_CITIZEN_EVENT_MAX, &cursor);
        CHECK(n == total, "relay carried %d of %d row(s)", n, total);
        for (int i = 0; i < n; i++)
            CHECK(reflux_action_is_citizen(rows[i].type), "relay carried unrelated type %d", rows[i].type);

        /* `max` is a datagram budget: the cursor stops ON the undelivered row, so a caller that
           loops gets everything in order instead of losing the overflow */
        int capped = 0, got = 0;
        NetCitizenEvent one[1];
        int last_type = -1, ordered = 1;
        while (citizen_sync_collect_events(one, 1, &capped) > 0) {
            if (got == 0 && one[0].type != rows[0].type) ordered = 0;
            last_type = one[0].type;
            got++;
            if (got > NET_CITIZEN_EVENT_MAX) break;
        }
        CHECK(ordered, "the batched relay did not start with the first row");
        CHECK(got == n, "batched relay delivered %d of %d row(s)", got, n);
        CHECK(last_type == rows[n - 1].type, "batched relay ended on type %d, wanted %d",
              last_type, rows[n - 1].type);

        /* applying re-dispatches into THIS peer's log, so a local mod sees what the server saw */
        int before = reflux_host_total_dispatched();
        CHECK(citizen_sync_apply_events(rows, n) == n, "apply refused a citizen row");
        int after = reflux_host_total_dispatched();
        CHECK(after == before + n, "apply appended %d row(s), wanted %d", after - before, n);
        for (int i = 0; i < n; i++) {
            int idx = reflux_host_index_for_abs(before + i);
            CHECK(idx >= 0 && reflux_action_is_citizen(reflux_host_action_type_at(idx)),
                  "relayed row %d landed as something else", i);
        }

        /* untrusted input: a peer must not be able to fake another family through this packet */
        NetCitizenEvent hostile;
        hostile.type = REFLUX_ACTION_HORSE_POOPED;
        hostile.a = 0; hostile.b = 0; hostile.c = 0;
        int b4 = reflux_host_total_dispatched();
        CHECK(citizen_sync_apply_events(&hostile, 1) == 0, "apply forwarded a horse event");
        CHECK(reflux_host_total_dispatched() == b4, "a refused row still reached the log");
    }

    citizen_actor_reset();
    CHECK(citizen_actor_action(2) == CITACT_IDLE, "reset did not forget the bodies");

    if (fails) { printf("citizen_actor: %d FAILURE(S)\n", fails); return 1; }
    printf("citizen_actor: ok -- PARENA's citizen body jumps, crouches, slides, slide-jumps and draws a\n"
           "weapon, the built-in mirror matches the mod field for field, each decision is a REFLUX action,\n"
           "and the wire relay carries that family to a client while refusing every other one\n");
    return 0;
}
