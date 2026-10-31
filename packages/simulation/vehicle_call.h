/* vehicle_call.h -- the three VEHICLE gadgets from the player's KIT (packages/common/kit.h):
 * KIT_ITEM_BUGGY, KIT_ITEM_ROCKET_CAR, KIT_ITEM_HELI.
 *
 * Founder 2026-10-31: "add buggy, rocket car, and hellicopter to the gadgets we already have the
 * buggy and the hellicopter the rocket car implementation is in bedrock racers". So these are not a
 * new vehicle system either: buggy and helicopter are this engine's own BuggyState/HelicopterState,
 * the rocket car is a BuggyState with BuggyState.rocket set (physics.h simulate_rocket_car_state,
 * numbers from PARENA/stdlib/shankpit/rocket_car_rules.prn, ported from Bedrock Racers' br_phys.prn),
 * and this header is just the verb a gadget key needs: put that vehicle in front of the player and
 * seat them in it.
 *
 * Deliberately NOT a teleport: the vehicle is spawned ahead and the player enters it through the same
 * buggy_try_enter / occupant path E already uses, so everything downstream (camera, chase steering,
 * exit offsets, damage, the draw code) is the existing vehicle code and not a second copy of it.
 *
 * Included by hosts AFTER packages/simulation/local_game.h (it is a thin layer over local_state).
 * Local modes only: a networked client cannot spawn vehicles, the server owns that.
 */
#ifndef SHANKPIT_VEHICLE_CALL_H
#define SHANKPIT_VEHICLE_CALL_H

#include "local_game.h"
#include "../common/kit.h"

#define VEHICLE_CALL_SPAWN_AHEAD 7.0f     /* far enough not to spawn inside the player's capsule */
#define VEHICLE_CALL_HELI_HEIGHT 2.6f     /* rotor clearance over the ground it is called onto */
#define VEHICLE_CALL_COOLDOWN 30          /* ticks, the same cooldown E uses after entering */

/* vehicle_call_spawn -- the call verb. item is a KIT_ITEM_* vehicle; anything else is a no-op.
 * Returns VEH_NONE when refused (not a vehicle item, already driving, on cooldown, or every slot in
 * this scene's garage is taken -- refusing beats silently recycling someone else's vehicle), else
 * VEH_BUGGY (including the rocket car, which IS a buggy), VEH_DRONE (either quadcopter) or
 * VEH_HELICOPTER. */
static inline int vehicle_call_spawn(PlayerState *p, int item) {
    if (!p || !p->active) return VEH_NONE;
    if (!kit_item_is_vehicle(item)) return VEH_NONE;
    if (p->in_vehicle) return VEH_NONE;
    if (p->vehicle_cooldown > 0) return VEH_NONE;

    float r = -p->yaw * 0.0174532925f;
    float px = p->x + sinf(r) * VEHICLE_CALL_SPAWN_AHEAD;
    float pz = p->z - cosf(r) * VEHICLE_CALL_SPAWN_AHEAD;

    if (item == KIT_ITEM_BUGGY || item == KIT_ITEM_ROCKET_CAR) {
        int slot = -1;
        for (int i = 0; i < MAX_BUGGIES; i++) {
            if (!local_state.buggies[i].active) { slot = i; break; }
        }
        if (slot < 0) return VEH_NONE;
        BuggyState *b = &local_state.buggies[slot];
        buggy_spawn_defaults(b, slot, p->scene_id, px, pz, p->yaw);
        if (item == KIT_ITEM_ROCKET_CAR) {
            b->rocket = 1;
            b->tank = phys_rocket_tank_max();
            /* the fans hold it off the surface from the first tick, so it arrives hovering rather
             * than sinking into the ground it was called onto */
            b->y += phys_rocket_hover_height();
        }
        if (!buggy_try_enter(p, b)) return VEH_NONE;
        p->vehicle_cooldown = VEHICLE_CALL_COOLDOWN;
        printf("[VEHICLE] called %s id=%d scene=%d at %.1f,%.1f (player=%d)\n",
               b->rocket ? "rocket car" : "buggy", slot, p->scene_id, px, pz, p->id);
        return VEH_BUGGY;
    }

    if (item == KIT_ITEM_DRONE || item == KIT_ITEM_MINI_DRONE) {
        int slot = -1;
        for (int i = 0; i < MAX_DRONES; i++) {
            if (!local_state.drones[i].active) { slot = i; break; }
        }
        if (slot < 0) return VEH_NONE;
        int kind = (item == KIT_ITEM_MINI_DRONE) ? DRONE_KIND_FLY : DRONE_KIND_RECON;
        DroneState *d = &local_state.drones[slot];
        float gy = terrain_sample_height(&g_scene_terrain, px, pz);
        if (gy < 0.0f) gy = 0.0f;
        /* the micro arrives at chest height, the recon high enough to look over a wall */
        drone_spawn_defaults(d, slot, p->scene_id, kind, px,
                             gy + (kind == DRONE_KIND_FLY ? 1.6f : 2.2f), pz, p->yaw);
        if (!drone_try_enter(p, d)) return VEH_NONE;
        p->vehicle_cooldown = VEHICLE_CALL_COOLDOWN;
        printf("[VEHICLE] called %s drone id=%d scene=%d at %.1f,%.1f (player=%d)\n",
               kind == DRONE_KIND_FLY ? "mini fly" : "recon", slot, p->scene_id, px, pz, p->id);
        return VEH_DRONE;
    }

    if (item == KIT_ITEM_PLANE) {
        int slot = -1;
        for (int i = 0; i < MAX_AIRPLANES; i++) {
            if (!local_state.airplanes[i].active) { slot = i; break; }
        }
        if (slot < 0) return VEH_NONE;
        /* further out than the others: an 18-unit wingspan called onto the player's toes is a collision */
        float ax = p->x + sinf(r) * 16.0f;
        float az = p->z - cosf(r) * 16.0f;
        AirplaneState *a = &local_state.airplanes[slot];
        airplane_spawn_defaults(a, slot, p->scene_id, ax, az, p->yaw);
        if (!airplane_try_enter(p, a)) return VEH_NONE;
        p->vehicle_cooldown = VEHICLE_CALL_COOLDOWN;
        printf("[VEHICLE] called C-110 id=%d scene=%d at %.1f,%.1f (player=%d)\n", slot, p->scene_id, ax, az, p->id);
        return VEH_AIRPLANE;
    }

    /* KIT_ITEM_HELI */
    {
        int slot = -1;
        for (int i = 0; i < MAX_HELICOPTERS; i++) {
            if (!local_state.helicopters[i].active) { slot = i; break; }
        }
        if (slot < 0) return VEH_NONE;
        float gy = terrain_sample_height(&g_scene_terrain, px, pz);
        if (gy < 0.0f) gy = 0.0f;
        HelicopterState *h = &local_state.helicopters[slot];
        heli_spawn_defaults(h, slot, p->scene_id, px, gy + VEHICLE_CALL_HELI_HEIGHT, pz);
        h->yaw = p->yaw;
        h->occupant_player_id = p->id;
        p->in_vehicle = 1;
        p->vehicle_type = VEH_HELICOPTER;
        p->x = h->x; p->y = h->y; p->z = h->z;
        p->vx = p->vy = p->vz = 0.0f;
        p->vehicle_cooldown = VEHICLE_CALL_COOLDOWN;
        printf("[VEHICLE] called helicopter id=%d scene=%d at %.1f,%.1f (player=%d)\n",
               slot, p->scene_id, px, pz, p->id);
        return VEH_HELICOPTER;
    }
}

#endif /* SHANKPIT_VEHICLE_CALL_H */
