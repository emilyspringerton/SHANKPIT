/* vehicle_call_test.c -- the three VEHICLE gadgets (packages/simulation/vehicle_call.h) and the
 * rocket car profile behind one of them (physics.h simulate_rocket_car_state).
 *
 * Behavioural, not golden numbers: a called buggy / rocket car / helicopter actually arrives AHEAD of
 * the player and seats them in it, the call respects the vehicle cooldown, a full garage is refused
 * instead of recycling someone else's car, and the rocket car is measurably the Bedrock Racers car --
 * faster than the buggy, hovering off the surface, and paying for its thrust out of the air tank.
 * Build/run: make test-vehicle-call. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "physics.h"
#include "net_sim.h"
#include "kit.h"
#include "local_game.h"
#include "vehicle_call.h"
#include "rocket_car_rules_host.h"
#include "drone_rules_host.h"

/* local_game.h declares these; apps/server and apps/lobby each define their own copy, and so does
 * this test (it links the simulation without either binary's main). */
int g_story_cutscene_done = 0;
int g_story_outro_requested = 0;
int g_shankpit_is_server = 0;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static PlayerState *reset_player(void) {
    memset(&local_state, 0, sizeof local_state);
    map_count = 1;
    PlayerState *p = &local_state.players[0];
    p->active = 1;
    p->id = 0;
    p->scene_id = SCENE_STADIUM;
    p->state = STATE_ALIVE;
    return p;
}

static BuggyState *called_buggy(int want_rocket) {
    for (int i = 0; i < MAX_BUGGIES; i++) {
        BuggyState *b = &local_state.buggies[i];
        if (b->active && b->rocket == want_rocket && b->occupant_player_id == 0) return b;
    }
    return NULL;
}

static BuggyState flight(float y, int rocket) {
    BuggyState b;
    memset(&b, 0, sizeof b);
    b.active = 1; b.id = 90; b.scene_id = SCENE_STADIUM;
    b.x = 0.0f; b.z = 0.0f; b.y = y; b.yaw = 0.0f;
    b.rocket = rocket;
    b.tank = rocket ? phys_rocket_tank_max() : 0;
    b.grounded = 1;
    return b;
}

int main(void) {
    rocket_car_rules_install();
    drone_rules_install();

    /* ---- the buggy: arrives ahead of the player, and they are already in it ---- */
    {
        PlayerState *p = reset_player();
        int v = vehicle_call_spawn(p, KIT_ITEM_BUGGY);
        CHECK(v == VEH_BUGGY, "buggy call returned %d", v);
        CHECK(p->in_vehicle && p->vehicle_type == VEH_BUGGY, "player was not seated in the buggy");
        BuggyState *b = called_buggy(0);
        CHECK(b != NULL, "no plain buggy in the world after the call");
        if (b) {
            /* yaw 0 faces -Z, and the call places it VEHICLE_CALL_SPAWN_AHEAD in front */
            CHECK(b->z < -5.0f && fabsf(b->x) < 0.5f, "buggy spawned at %.1f,%.1f -- not ahead", b->x, b->z);
            CHECK(b->occupant_player_id == 0, "buggy hatched with the wrong driver: %d", b->occupant_player_id);
        }
        CHECK(vehicle_call_spawn(p, KIT_ITEM_BUGGY) == VEH_NONE, "a second call ignored the vehicle cooldown");
        printf("PASS: buggy called ahead of the player, seated, on a cooldown\n");
    }

    /* ---- the rocket car: a buggy with the Bedrock Racers profile, and a full tank ---- */
    {
        PlayerState *p = reset_player();
        int v = vehicle_call_spawn(p, KIT_ITEM_ROCKET_CAR);
        CHECK(v == VEH_BUGGY, "rocket car call returned %d", v);
        CHECK(p->in_vehicle && p->vehicle_type == VEH_BUGGY, "player was not seated in the rocket car");
        BuggyState *b = called_buggy(1);
        CHECK(b != NULL, "no rocket-flagged buggy after the call");
        if (b) {
            CHECK(b->tank == phys_rocket_tank_max(), "rocket car arrived with %d air", b->tank);
            CHECK(b->y > phys_rocket_hover_height(), "rocket car arrived %f above the ground, not hovering", b->y);
        }
        printf("PASS: rocket car called with a full tank, hovering, and flagged as the rocket profile\n");
    }

    /* ---- the helicopter: the engine's own HelicopterState, occupied ---- */
    {
        PlayerState *p = reset_player();
        int v = vehicle_call_spawn(p, KIT_ITEM_HELI);
        CHECK(v == VEH_HELICOPTER, "helicopter call returned %d", v);
        CHECK(p->in_vehicle && p->vehicle_type == VEH_HELICOPTER, "player was not seated in the helicopter");
        HelicopterState *h = NULL;
        for (int i = 0; i < MAX_HELICOPTERS; i++)
            if (local_state.helicopters[i].active && local_state.helicopters[i].occupant_player_id == 0)
                h = &local_state.helicopters[i];
        CHECK(h != NULL, "no occupied helicopter in the world after the call");
        if (h) CHECK(h->y > 2.0f && h->z < -5.0f, "helicopter at %.1f,%.1f -- not ahead and clear", h->y, h->z);
        printf("PASS: helicopter called ahead, clear of the ground, and occupied\n");
    }

    /* ---- the drones: two airframes, both immediate-thrust quadcopters ---- */
    {
        PlayerState *p = reset_player();
        int v = vehicle_call_spawn(p, KIT_ITEM_DRONE);
        CHECK(v == VEH_DRONE, "recon drone call returned %d", v);
        CHECK(p->in_vehicle && p->vehicle_type == VEH_DRONE, "player was not seated in the recon drone");
        DroneState *d = NULL;
        for (int i = 0; i < MAX_DRONES; i++)
            if (local_state.drones[i].active && local_state.drones[i].occupant_player_id == 0)
                d = &local_state.drones[i];
        CHECK(d != NULL, "no occupied drone after the recon call");
        if (d) CHECK(d->kind == DRONE_KIND_RECON && d->y > 1.0f, "recon drone kind=%d y=%.2f", d->kind, d->y);

        p = reset_player();
        v = vehicle_call_spawn(p, KIT_ITEM_MINI_DRONE);
        CHECK(v == VEH_DRONE, "mini drone call returned %d", v);
        d = NULL;
        for (int i = 0; i < MAX_DRONES; i++)
            if (local_state.drones[i].active && local_state.drones[i].occupant_player_id == 0)
                d = &local_state.drones[i];
        CHECK(d != NULL, "no occupied drone after the mini call");
        if (d) CHECK(d->kind == DRONE_KIND_FLY, "mini drone came out as kind %d", d->kind);
        printf("PASS: both drones call, arrive occupied, and keep their kind\n");
    }

    /* ---- the airframes: thrust is immediate, and the micro is the twitchy one ---- */
    {
        DroneState recon, fly;
        drone_spawn_defaults(&recon, 40, SCENE_STADIUM, DRONE_KIND_RECON, 0.0f, 20.0f, 0.0f, 0.0f);
        drone_spawn_defaults(&fly, 41, SCENE_STADIUM, DRONE_KIND_FLY, 0.0f, 20.0f, 0.0f, 0.0f);
        drone_simulate_step(&recon, 1.0f, 0.0f, 0.0f, 0.0f, 0, SHANKPIT_NET_FIXED_DT);
        drone_simulate_step(&fly, 1.0f, 0.0f, 0.0f, 0.0f, 0, SHANKPIT_NET_FIXED_DT);
        CHECK(fabsf(recon.vz) > 0.05f, "the recon did not move on its very first tick: vz=%.4f", recon.vz);
        CHECK(fly.vz < -0.10f, "the micro did not move on its very first tick: vz=%.4f", fly.vz);
        for (int i = 0; i < 180; i++) {
            drone_simulate_step(&recon, 1.0f, 0.0f, 0.0f, 0.0f, 0, SHANKPIT_NET_FIXED_DT);
            drone_simulate_step(&fly, 1.0f, 0.0f, 0.0f, 0.0f, 0, SHANKPIT_NET_FIXED_DT);
        }
        float rs = sqrtf(recon.vx * recon.vx + recon.vz * recon.vz);
        float fs = sqrtf(fly.vx * fly.vx + fly.vz * fly.vz);
        CHECK(fs > rs + 0.5f, "the micro (%.2f) is not meaningfully faster than the recon (%.2f)", fs, rs);
        printf("PASS: both thrust on tick one, and the micro outruns the recon (%.2f vs %.2f)\n", fs, rs);
    }

    /* ---- the same storm, two different airframes: PARENA's drift rule, measured ---- */
    {
        DroneState recon, fly;
        drone_spawn_defaults(&recon, 42, SCENE_STADIUM, DRONE_KIND_RECON, 0.0f, 20.0f, 0.0f, 0.0f);
        drone_spawn_defaults(&fly, 43, SCENE_STADIUM, DRONE_KIND_FLY, 0.0f, 20.0f, 0.0f, 0.0f);
        float rmax = 0.0f, fmax = 0.0f;
        for (int i = 0; i < 300; i++) {
            drone_simulate_step(&recon, 0.0f, 0.0f, 0.0f, 0.0f, 900, SHANKPIT_NET_FIXED_DT);
            drone_simulate_step(&fly, 0.0f, 0.0f, 0.0f, 0.0f, 900, SHANKPIT_NET_FIXED_DT);
            float r = fabsf(recon.vx) + fabsf(recon.vz);
            float f = fabsf(fly.vx) + fabsf(fly.vz);
            if (r > rmax) rmax = r;
            if (f > fmax) fmax = f;
        }
        CHECK(rmax > 0.0f, "even the recon felt nothing of a 900 permille storm");
        CHECK(fmax > rmax * 1.5f, "the micro was not buffeted more than the recon: %.4f vs %.4f", fmax, rmax);
        printf("PASS: one storm, micro deflection %.3f vs recon %.3f\n", fmax, rmax);
    }

    /* ---- a wall: the micro bounces, the recon stops, neither goes through ---- */
    {
        reset_player();
        /* map_geo is the physics layer's live pointer (that is how buggy_wall_test builds walls too),
           so a wall is a fixture swapped in for the run and swapped back after. */
        Box wall[2];
        memset(wall, 0, sizeof wall);
        wall[1].x = 6.0f; wall[1].y = 0.0f; wall[1].z = 0.0f;
        wall[1].w = 4.0f; wall[1].h = 6.0f; wall[1].d = 4.0f;
        const Box *saved_geo = map_geo;
        int saved_count = map_count;
        map_geo = wall;
        map_count = 2;
        DroneState recon, fly;
        drone_spawn_defaults(&recon, 50, SCENE_STADIUM, DRONE_KIND_RECON, 2.0f, 1.0f, 0.0f, -90.0f);
        drone_spawn_defaults(&fly, 51, SCENE_STADIUM, DRONE_KIND_FLY, 2.0f, 1.0f, 0.0f, -90.0f);
        int micro_bounced = 0;
        for (int i = 0; i < 120; i++) {
            drone_simulate_step(&recon, 1.0f, 0.0f, 0.0f, 0.0f, 0, SHANKPIT_NET_FIXED_DT);
            drone_simulate_step(&fly, 1.0f, 0.0f, 0.0f, 0.0f, 0, SHANKPIT_NET_FIXED_DT);
            if (fly.vx < -0.05f) micro_bounced = 1;
        }
        CHECK(recon.x < 4.5f, "the recon went through the wall: x=%.2f", recon.x);
        CHECK(fly.x < 4.5f, "the micro went through the wall: x=%.2f", fly.x);
        CHECK(micro_bounced, "the micro never bounced off the wall (vx=%.3f)", fly.vx);
        map_geo = saved_geo;
        map_count = saved_count;
        printf("PASS: a wall -- the micro bounces (%.2f), the recon stops (%.2f), nobody passes\n",
               fly.x, recon.x);
    }

    /* ---- the C-110: called onto the ground ahead, lever closed, nose along the player's heading ---- */
    {
        PlayerState *p = reset_player();
        int v = vehicle_call_spawn(p, KIT_ITEM_PLANE);
        CHECK(v == VEH_AIRPLANE, "plane call returned %d", v);
        CHECK(p->in_vehicle && p->vehicle_type == VEH_AIRPLANE, "player was not seated in the C-110");
        AirplaneState *a = NULL;
        for (int i = 0; i < MAX_AIRPLANES; i++)
            if (local_state.airplanes[i].active && local_state.airplanes[i].occupant_player_id == 0)
                a = &local_state.airplanes[i];
        CHECK(a != NULL, "no occupied plane after the call");
        if (a) {
            CHECK(a->z < -10.0f && fabsf(a->x) < 0.5f, "plane spawned at %.1f,%.1f -- not ahead", a->x, a->z);
            CHECK(a->grounded, "the plane arrived airborne");
            CHECK(a->throttle == 0.0f, "the plane arrived with the throttle open: %.0f", a->throttle);
            CHECK(a->health == (float)af_hull(), "the plane arrived damaged: %.0f", a->health);
        }
        printf("PASS: the C-110 calls onto the ground ahead with the lever closed\n");
    }

    /* ---- a shard: any debris kills the micro, only a real hit breaks the recon ---- */
    {
        DroneState fly, recon;
        drone_spawn_defaults(&fly, 60, SCENE_STADIUM, DRONE_KIND_FLY, 0.0f, 20.0f, 0.0f, 0.0f);
        CHECK(!drone_shard_contact(&fly, 30.0f, 20.0f, 0.0f, 0, 0, 0, 15000),
              "a shard 30 units away killed the micro");
        CHECK(drone_shard_contact(&fly, 0.05f, 20.0f, 0.0f, 0, 0, 0, 15000),
              "a 15 kg shard touching the micro did not kill it");
        CHECK(fly.destroyed && !fly.active, "the micro was not retired after a fatal shard");
        CHECK(!drone_shard_contact(&fly, 0.0f, 20.0f, 0.0f, 0, 0, 0, 15000), "a dead drone was hit twice");

        drone_spawn_defaults(&recon, 61, SCENE_STADIUM, DRONE_KIND_RECON, 0.0f, 20.0f, 0.0f, 0.0f);
        CHECK(!drone_shard_contact(&recon, 0.0f, 20.0f, 0.0f, 0.0f, 0.0f, 0.0f, 15),
              "a pebble killed the recon");
        CHECK(!recon.destroyed, "the recon died to a 15 g shard sitting still");
        CHECK(drone_shard_contact(&recon, 0.0f, 20.0f, 0.0f, 0.0f, 0.0f, -3.0f, 15000),
              "a 15 kg shard at 3 units/tick did not break the recon");
        CHECK(recon.destroyed && recon.health == 0.0f, "the recon was not retired by a fatal shard");

        /* the pilot is thrown clear where it died, not left riding a wreck */
        PlayerState *p = reset_player();
        DroneState d;
        drone_spawn_defaults(&d, 62, SCENE_STADIUM, DRONE_KIND_FLY, 4.0f, 12.0f, -3.0f, 0.0f);
        d.occupant_player_id = p->id;
        p->in_vehicle = 1;
        p->vehicle_type = VEH_DRONE;
        drone_eject_pilot(p, &d);
        CHECK(!p->in_vehicle && p->vehicle_type == VEH_NONE, "the pilot stayed in the vehicle");
        CHECK(fabsf(p->y - 12.0f) < 0.01f && fabsf(p->x - 4.0f) < 0.01f, "the pilot was not ejected in place");
        printf("PASS: shards -- the micro dies to any debris, the recon only to a real hit, pilot ejected\n");
    }

    /* ---- the gimbal: a clamped head, not free look ---- */
    {
        DroneState recon, fly;
        drone_spawn_defaults(&recon, 70, SCENE_STADIUM, DRONE_KIND_RECON, 0, 20, 0, 0);
        drone_gimbal_track(&recon, 170.0f, 80.0f);
        float yl = (float)df_gimbal_yaw_limit(DRONE_KIND_RECON) * 0.001f;
        float pl = (float)df_gimbal_pitch_limit(DRONE_KIND_RECON) * 0.001f;
        CHECK(yl > 60.0f && pl > 30.0f, "the recon's gimbal limits are too small to be one: %.1f/%.1f", yl, pl);
        CHECK(fabsf(recon.gimbal_yaw) <= yl + 0.01f, "the head swung past its limit: %.2f", recon.gimbal_yaw);
        CHECK(fabsf(recon.gimbal_pitch) <= pl + 0.01f, "the head pitched past its limit: %.2f", recon.gimbal_pitch);
        drone_gimbal_track(&recon, 10.0f, -5.0f);
        CHECK(fabsf(recon.gimbal_yaw - 10.0f) < 0.5f && fabsf(recon.gimbal_pitch + 5.0f) < 0.5f,
              "inside its limits the head should follow exactly: yaw %.2f pitch %.2f",
              recon.gimbal_yaw, recon.gimbal_pitch);
        CHECK(fabsf(drone_gimbal_view_yaw(&recon) - 10.0f) < 0.5f, "the view yaw is not airframe+gimbal");

        drone_spawn_defaults(&fly, 71, SCENE_STADIUM, DRONE_KIND_FLY, 0, 20, 0, 0);
        drone_gimbal_track(&fly, 90.0f, 0.0f);
        CHECK(fabsf(fly.gimbal_yaw) < yl, "the micro's head moves like the recon's");
        printf("PASS: gimbal -- recon %.0f deg of head, micro %.0f, both clamped\n",
               yl, (float)df_gimbal_yaw_limit(DRONE_KIND_FLY) * 0.001f);
    }

    /* ---- refusals: not a vehicle item, and a garage with nothing free ---- */
    {
        PlayerState *p = reset_player();
        CHECK(vehicle_call_spawn(p, KIT_ITEM_PHEROMONE) == VEH_NONE, "a non-vehicle item spawned a vehicle");
        CHECK(vehicle_call_spawn(p, KIT_ITEM_NONE) == VEH_NONE, "the empty slot spawned a vehicle");
        for (int i = 0; i < MAX_BUGGIES; i++) local_state.buggies[i].active = 1;
        CHECK(vehicle_call_spawn(p, KIT_ITEM_BUGGY) == VEH_NONE, "spawned a buggy into a full garage");
        for (int i = 0; i < MAX_HELICOPTERS; i++) local_state.helicopters[i].active = 1;
        p->vehicle_cooldown = 0;
        CHECK(vehicle_call_spawn(p, KIT_ITEM_HELI) == VEH_NONE, "spawned a helicopter into a full pad");
        printf("PASS: refusals -- not a call item / no free slot\n");
    }

    /* ---- the profile itself: faster than the buggy, hovering, and it spends air ---- */
    {
        BuggyState car = flight(20.0f, 1);
        BuggyState plain = flight(20.0f, 0);
        for (int i = 0; i < 240; i++) {
            simulate_buggy_state(&car, 1.0f, 0.0f, SHANKPIT_NET_FIXED_DT, 1);
            simulate_buggy_state(&plain, 1.0f, 0.0f, SHANKPIT_NET_FIXED_DT, 1);
        }
        float carspd = sqrtf(car.vx * car.vx + car.vz * car.vz);
        float plainsm = sqrtf(plain.vx * plain.vx + plain.vz * plain.vz);
        CHECK(carspd > plainsm + 1.0f, "rocket car %.2f is not meaningfully faster than the buggy %.2f", carspd, plainsm);
        CHECK(carspd > BUGGY_TOP_SPEED, "rocket car never exceeded the buggy's top speed: %.2f", carspd);
        CHECK(car.tank < phys_rocket_tank_max(), "holding the throttle spent no air: tank=%d", car.tank);

        int drained = car.tank;
        for (int i = 0; i < 240; i++) simulate_buggy_state(&car, 0.0f, 0.0f, SHANKPIT_NET_FIXED_DT, 1);
        CHECK(car.tank > drained, "idle air did not refill: %d -> %d", drained, car.tank);
        printf("PASS: rocket car %.2f vs buggy %.2f units/tick, tank %d -> %d while idle\n",
               carspd, plainsm, drained, car.tank);
    }

    /* ---- FLYABILITY: the four nozzles climb while the lift key is held (br_phys fly-sub!'s lift),
       the fans catch the car again when it is let go, and the climb is paid for out of the tank ---- */
    {
        BuggyState car = flight(20.0f, 1);
        for (int i = 0; i < 180; i++) simulate_buggy_state(&car, 0.0f, 0.0f, SHANKPIT_NET_FIXED_DT, 1);
        const float hovered = car.y;
        const int tank_before = car.tank;
        for (int i = 0; i < 45; i++)
            simulate_buggy_state_ex(&car, 0.0f, 0.0f, SHANKPIT_NET_FIXED_DT, 1, 1.0f, 0);
        const float climbed = car.y;
        CHECK(climbed > hovered + 2.0f, "holding lift did not climb: %.2f -> %.2f", hovered, climbed);
        CHECK(car.tank < tank_before, "the lift nozzles burned no air: %d -> %d", tank_before, car.tank);
        for (int i = 0; i < 300; i++)
            simulate_buggy_state_ex(&car, 0.0f, 0.0f, SHANKPIT_NET_FIXED_DT, 1, 0.0f, 0);
        CHECK(car.y < climbed - 2.0f, "the car did not come down after the lift was released: %.2f", car.y);
        CHECK(fabsf(car.y - hovered) < 0.6f,
              "the fans did not settle it back at the hover height: %.2f vs %.2f", car.y, hovered);
        printf("PASS: lift climbs %.2f -> %.2f, the fans settle it back to %.2f\n", hovered, climbed, car.y);
    }

    /* ---- BOOST: the rear nozzle's own key -- faster than the throttle alone, and it spends air;
       an empty tank has nothing to shove with (br_phys's "never helpless, but not a rocket") ---- */
    {
        BuggyState plain = flight(20.0f, 1);
        BuggyState boosted = flight(20.0f, 1);
        for (int i = 0; i < 240; i++) {
            simulate_buggy_state(&plain, 1.0f, 0.0f, SHANKPIT_NET_FIXED_DT, 1);
            simulate_buggy_state_ex(&boosted, 1.0f, 0.0f, SHANKPIT_NET_FIXED_DT, 1, 0.0f, 1);
        }
        const float pspd = sqrtf(plain.vx * plain.vx + plain.vz * plain.vz);
        const float bspd = sqrtf(boosted.vx * boosted.vx + boosted.vz * boosted.vz);
        CHECK(bspd > pspd + 1.0f, "boost %.2f is not faster than the throttle alone %.2f", bspd, pspd);
        /* the tank is shared with the throttle, so the spend is measured over a burn short enough
           that neither car runs it dry (240 ticks flattens both to the same puddle) */
        BuggyState short_plain = flight(20.0f, 1);
        BuggyState short_boost = flight(20.0f, 1);
        for (int i = 0; i < 12; i++) {
            simulate_buggy_state(&short_plain, 1.0f, 0.0f, SHANKPIT_NET_FIXED_DT, 1);
            simulate_buggy_state_ex(&short_boost, 1.0f, 0.0f, SHANKPIT_NET_FIXED_DT, 1, 0.0f, 1);
        }
        CHECK(short_boost.tank < short_plain.tank, "boost spent no extra air: %d vs %d",
              short_boost.tank, short_plain.tank);

        /* empty, it cannot START a burn (br_phys air-tick!) -- judged on the tick that found the tank
           empty, not after the idle refill has put something back in it */
        BuggyState dry = flight(20.0f, 1);
        BuggyState dry_boosted = flight(20.0f, 1);
        dry.tank = 0;
        dry_boosted.tank = 0;
        simulate_buggy_state(&dry, 1.0f, 0.0f, SHANKPIT_NET_FIXED_DT, 1);
        simulate_buggy_state_ex(&dry_boosted, 1.0f, 0.0f, SHANKPIT_NET_FIXED_DT, 1, 0.0f, 1);
        CHECK(fabsf(dry.vx - dry_boosted.vx) < 0.0001f && fabsf(dry.vz - dry_boosted.vz) < 0.0001f,
              "an empty tank boosted on the tick that found it empty");
        printf("PASS: boost %.2f vs %.2f, it burns its own air, and an empty tank cannot start one\n", bspd, pspd);
    }

    /* ---- an empty tank drops it onto its belly instead of leaving a rocket that never runs out ---- */
    {
        BuggyState dry = flight(20.0f, 1);
        dry.tank = 0;
        for (int i = 0; i < 400; i++) simulate_buggy_state(&dry, 0.0f, 0.0f, SHANKPIT_NET_FIXED_DT, 1);
        CHECK(dry.y < phys_rocket_hover_height() + 0.5f, "an empty tank still held it high: y=%.2f", dry.y);
        CHECK(dry.y >= BUGGY_CHASSIS_CLEARANCE - 0.01f, "an empty tank sank it through the floor: y=%.2f", dry.y);
        printf("PASS: an empty tank sits it down at %.2f and it does not sink through\n", dry.y);
    }

    printf(fails ? "FAILURES: %d\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
