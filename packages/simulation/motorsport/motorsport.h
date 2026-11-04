/* motorsport.h -- the motor-sport tech tree: building mini bikes and go karts.
 *
 * The parts stack for a tuned machine. A bike is a group of slots; each slot is a part from
 * the catalog, and each part comes in classes. A class is a full level of the tech tree:
 *   - motor: 0 = stock / smokestack, 1 = crossplane, 2 = parallel-twin, 3 = triple, 4 = V4.
 *   - frame: 0 = trail, 1 = enduro, 2 = dirt, 3 = scrambler, 4 = cross.
 *   - suspension: 0 = coil, 1 = air, 2 = preload, 3 = double-fork, 4 = fully suspended.
 *   - tires: 0 = knobbly, 1 = trail, 2 = slick, 3 = drag, 4 = rally-spec.
 *   - electronics: 0 = mechanical, 1 = CDI, 2 = fuel injection, 3 = ECU, 4 = traction control.
 *
 * A built bike is a group of 5 parts, one per slot. The build queue is the set of parts you
 * have, and each slot may only accept a part of its own class. The build cost is the stack of
 * tier upgrades: a motor at class 0 is free stock, at class 1 costs 5 engines, and so on.
 *
 * The racecourse is the payoff: a sprint is a race on a track with a specific surface, and the
 * winner is decided by the engine's torque, the frame's weight, the suspension's damping, and
 * the tires' grip. The build cost is the whole stack of parts, and the racecourse is the reward.
 */
#ifndef SHANKPIT_MOTORSPORT_H
#define SHANKPIT_MOTORSPORT_H

#include <stddef.h>

/* A part class is a full level of the tech tree. A motor at class 0 is stock, at class 1 is
 * crossplane, up to class 4 is a V4. */enum MotorClass {
    MOTO_CLASS_STOCK = 0,
    MOTO_CLASS_CROSSPARALLEL,
    MOTO_CLASS_PARALLEL_TWIN,
    MOTO_CLASS_TRIPLE,
    MOTO_CLASS_V4
};

/* A frame class. Class 0 is a trail frame, class 1 is an enduro frame, up to class 4 is a cross
 * frame. */enum FrameClass {
    FRAME_CLASS_TRAIL = 0,
    FRAME_CLASS_ENDURO,
    FRAME_CLASS_DIRT,
    FRAME_CLASS_SCRAMBLER,
    FRAME_CLASS_CROSS
};

/* A suspension class. Class 0 is a coil, class 1 is air, up to class 4 is fully suspended. */enum SuspensionClass {
    SUSPENSION_CLASS_COIL = 0,
    SUSPENSION_CLASS_AIR,
    SUSPENSION_CLASS_PRELOAD,
    SUSPENSION_CLASS_DOUBLE_FORK,
    SUSPENSION_CLASS_FULLY_SUSPENDED
};

/* A tire class. Class 0 is a knobbly, class 1 is a trail, up to class 4 is a rally-spec. */enum TireClass {
    TIRE_CLASS_KNOBBLY = 0,
    TIRE_CLASS_TRAIL,
    TIRE_CLASS_SLEEK,
    TIRE_CLASS_DRAG,
    TIRE_CLASS_RALLY
};

/* An electronics class. Class 0 is mechanical, class 1 is CDI, up to class 4 is traction control. */enum ElectronicsClass {
    ELECTRONIC_CLASS_MECHANICAL = 0,
    ELECTRONIC_CLASS_CDI,
    ELECTRONIC_CLASS_FUEL_INJECTION,
    ELECTRONIC_CLASS_ECU,
    ELECTRONIC_CLASS_TRACTION_CONTROL
};

/* The complete part catalog as a table of slots and classes. */enum PartSlotId {
    PART_SLOT_ID_ENGINE = 0,
    PART_SLOT_ID_FRAME,
    PART_SLOT_ID_SUSPENSION,
    PART_SLOT_ID_TIRES,
    PART_SLOT_ID_ELECTRONICS,
    PART_SLOT_ID_COUNT
};

/* A motor-sport bike. It is a 5-part machine, one part in each slot. */typedef struct {
    int engine;      /* MOTO_CLASS_* */int frame;        /* FRAME_CLASS_* */
    int suspension;  /* SUSPENSION_CLASS_* */
    int tires;       /* TIRE_CLASS_* */
    int electronics; /* ELECTRONIC_CLASS_* */
} Bike;

/* A build is a group of parts: the stack of parts you have gathered, in order. A build may be
 * consumed to build a bike, but each slot may only take a part of its own class. */typedef struct {
    int engine;
    int frame;
    int suspension;
    int tires;
    int electronics;
} Build;

/* Install the built-in part catalog. Call once at startup from the host that owns the bike. */void motorsport_install(void);

/* The number of classes per slot (5: stock, 1, 2, 3, 4). */int motorsport_class_count(void);

/* The price of a part in the build queue, given its class. A motor at class 0 is free stock, at
 * class 1 costs 5 engines, at class 2 costs 12, at class 3 costs 20, at class 4 costs 35. */int motorsport_part_cost(int slot, int class_id);

/* The weight of a part in the build queue, once it is built, given its class. A heavier bike
 * loses the race; a lighter bike wins. */int motorsport_part_weight(int slot, int class_id);

/* The power of a part in the build queue, given its class. A motor's power is the engine's
 * class: a motor at class 4 has 4x the power of a stock motor. */int motorsport_part_power(int slot, int class_id);

/* The grip of a part in the build queue, given its class. A tire's grip is the tire class:
 * a tire at class 4 has 4x the grip of a stock tire. */int motorsport_part_grip(int slot, int class_id);

/* The build budget: the total cost of a build, for the player to spend. */int motorsport_build_budget(int *cost);

/* The build cost: the total cost of a build, for the player to spend. */
int motorsport_build_cost(int *cost);

/* Build a bike from a build queue. A slot may only take a part of its own class, so a frame may
 * not be a motor. Returns 0 on success, -1 on error. */int motorsport_build_bike(Bike *bike, const Build *build);

/* A racecourse is the payoff. A sprint is a race on a track with a specific surface, and the
 * winner is decided by the engine's torque, the frame's weight, the suspension's damping, and the
 * tires' grip. Returns 0 on success, -1 on error. */int motorsport_sprint_race(const Bike *left, const Bike *right);

#endif /* SHANKPIT_MOTORSPORT_H */
