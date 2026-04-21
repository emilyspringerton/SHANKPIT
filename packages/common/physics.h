#ifndef PHYSICS_H
#define PHYSICS_H
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include "protocol.h"
#include "../world/terrain.h"

// --- TUNING ---
#define GRAVITY_FLOAT 0.025f 
#define GRAVITY_DROP 0.075f  
#define JUMP_FORCE 0.95f     
#define MAX_SPEED 0.95f      
#define FRICTION 0.15f      
#define ACCEL 0.6f          
#define STOP_SPEED 0.1f     
#define SLIDE_FRICTION 0.01f 
#define CROUCH_SPEED 0.35f  

// --- BUGGY TUNING ---
// Tuning target (60 Hz fixed step): ~3.7-4.2s 0->BUGGY_TOP_SPEED on flat ground at full throttle.
#define BUGGY_TOP_SPEED 6.24f
#define BUGGY_REVERSE_TOP_SPEED 1.8f
#define BUGGY_BASE_DRIVE_FORCE 0.0364f
#define BUGGY_COAST_FRICTION 0.0048f
#define BUGGY_BRAKE_FRICTION 0.022f
#define BUGGY_TURN_RATE_LOW 3.1f
#define BUGGY_TURN_RATE_HIGH 0.9f
#define BUGGY_LATERAL_GRIP 0.12f
#define BUGGY_TRANSMISSION_BAND1_END 0.22f
#define BUGGY_TRANSMISSION_BAND2_END 0.52f
#define BUGGY_TRANSMISSION_BAND3_END 0.78f
#define BUGGY_GRAVITY 0.15f
#define BUGGY_WHEELBASE 4.8f
#define BUGGY_TRACK_WIDTH 3.8f
#define BUGGY_WHEEL_RADIUS 1.05f
#define BUGGY_CHASSIS_CLEARANCE 0.95f

#define EYE_HEIGHT 2.59f    
#define PLAYER_WIDTH 0.97f  
#define PLAYER_HEIGHT 6.47f 
#define HEAD_SIZE 1.94f     
#define HEAD_OFFSET 2.42f   
#define MELEE_RANGE_SQ 250.0f 

#define KATANA_SLASH_RANGE 2.75f
#define KATANA_SLASH_ARC_DEG 88.0f
#define KATANA_SLASH_DAMAGE 40
#define KATANA_SLASH_COOLDOWN 28
#define KATANA_SLASH_ACTIVE_TICKS 6
#define KATANA_DASH_SPEED 5.8f
#define KATANA_DASH_TIME 8
#define KATANA_DASH_DAMAGE 50
#define KATANA_DASH_COOLDOWN 420
#define KATANA_DASH_UPWARD_LIMIT 0.35f
#define KATANA_DASH_HIT_RADIUS 3.3f
#define KATANA_DASH_HIT_RADIUS_SQ (KATANA_DASH_HIT_RADIUS * KATANA_DASH_HIT_RADIUS)
#define KATANA_DASH_HIT_MAX 8

void evolve_bot(PlayerState *loser, PlayerState *winner);
PlayerState* get_best_bot();
void phys_respawn(PlayerState *p, unsigned int now);

typedef struct { float x, y, z, w, h, d; } Box;
typedef struct { float x, y; } Vec2;

// --- SCENES ---
static const Box map_geo_stadium[] = {
    /* Inner arena floor and under-slab: keep the center readable for combat + rally crossings. */
    {0.00, -6.00, 0.00, 1880.00, 8.00, 1880.00},
    {0.00, -1.00, 0.00, 900.00, 6.00, 900.00},

    /* Track runoff apron: broad low ring between the dirt loop and the hard stadium structure. */
    {0.00, 2.00, 760.00, 1260.00, 8.00, 120.00},
    {0.00, 2.00, -760.00, 1260.00, 8.00, 120.00},
    {760.00, 2.00, 0.00, 120.00, 8.00, 1260.00},
    {-760.00, 2.00, 0.00, 120.00, 8.00, 1260.00},
    {620.00, 2.00, 620.00, 260.00, 8.00, 260.00},
    {-620.00, 2.00, 620.00, 260.00, 8.00, 260.00},
    {620.00, 2.00, -620.00, 260.00, 8.00, 260.00},
    {-620.00, 2.00, -620.00, 260.00, 8.00, 260.00},

    /* Lower retaining wall: first hard collision ring, set well outside rally loop extremes. */
    {0.00, 20.00, 860.00, 1380.00, 30.00, 40.00},
    {0.00, 20.00, -860.00, 1380.00, 30.00, 40.00},
    {860.00, 20.00, 0.00, 40.00, 30.00, 1320.00},
    {-860.00, 20.00, 0.00, 40.00, 30.00, 1320.00},
    {760.00, 20.00, 760.00, 220.00, 30.00, 220.00},
    {-760.00, 20.00, 760.00, 220.00, 30.00, 220.00},
    {760.00, 20.00, -760.00, 220.00, 30.00, 220.00},
    {-760.00, 20.00, -760.00, 220.00, 30.00, 220.00},

    /* Recessed tunnel facades (dark vomitory look) without opening the collision boundary. */
    {-320.00, 16.00, 842.00, 90.00, 18.00, 8.00},
    {0.00, 16.00, 842.00, 100.00, 18.00, 8.00},
    {320.00, 16.00, 842.00, 90.00, 18.00, 8.00},
    {-280.00, 16.00, -842.00, 86.00, 18.00, 8.00},
    {280.00, 16.00, -842.00, 86.00, 18.00, 8.00},

    /* Mid concourse berm: second tier pushes outward/upward so the bowl reads as architecture. */
    {0.00, 44.00, 900.00, 1500.00, 24.00, 80.00},
    {0.00, 44.00, -900.00, 1500.00, 24.00, 80.00},
    {900.00, 44.00, 0.00, 80.00, 24.00, 1440.00},
    {-900.00, 44.00, 0.00, 80.00, 24.00, 1440.00},
    {820.00, 44.00, 820.00, 260.00, 24.00, 260.00},
    {-820.00, 44.00, 820.00, 260.00, 24.00, 260.00},
    {820.00, 44.00, -820.00, 260.00, 24.00, 260.00},
    {-820.00, 44.00, -820.00, 260.00, 24.00, 260.00},

    /* Upper seating silhouette: chunky stands mass enclosing the arena at gameplay scale. */
    {0.00, 76.00, 940.00, 1620.00, 30.00, 110.00},
    {0.00, 70.00, -940.00, 1540.00, 24.00, 96.00},
    {940.00, 72.00, 0.00, 108.00, 26.00, 1500.00},
    {-940.00, 72.00, 0.00, 108.00, 26.00, 1500.00},
    {860.00, 72.00, 860.00, 300.00, 30.00, 300.00},
    {-860.00, 72.00, 860.00, 300.00, 30.00, 300.00},
    {860.00, 72.00, -860.00, 300.00, 26.00, 300.00},
    {-860.00, 72.00, -860.00, 300.00, 26.00, 300.00},

    /* Main grandstand (north side) to give one side heavier visual weight. */
    {0.00, 106.00, 990.00, 940.00, 34.00, 120.00},
    {-420.00, 120.00, 1010.00, 260.00, 16.00, 80.00},
    {420.00, 120.00, 1010.00, 260.00, 16.00, 80.00},

    /* Corner towers / light support silhouettes for long-range breakup. */
    {980.00, 112.00, 920.00, 56.00, 80.00, 56.00},
    {-980.00, 112.00, 920.00, 56.00, 80.00, 56.00},
    {980.00, 108.00, -920.00, 52.00, 72.00, 52.00},
    {-980.00, 108.00, -920.00, 52.00, 72.00, 52.00}
};

static const Box map_geo_garage[] = {
    {0.00, -2.00, 0.00, 160.00, 4.00, 160.00},
    {0.00, -8.00, 0.00, 170.00, 2.00, 170.00},
    {0.00, 18.00, 0.00, 160.00, 4.00, 160.00},
    {60.00, 9.00, 0.00, 4.00, 18.00, 160.00},
    {-60.00, 9.00, 0.00, 4.00, 18.00, 160.00},
    {0.00, 9.00, 60.00, 160.00, 18.00, 4.00},
    {0.00, 9.00, -60.00, 160.00, 18.00, 4.00},
    {64.00, 9.00, 0.00, 4.00, 22.00, 170.00},
    {-64.00, 9.00, 0.00, 4.00, 22.00, 170.00},
    {0.00, 9.00, 64.00, 170.00, 22.00, 4.00},
    {0.00, 9.00, -64.00, 170.00, 22.00, 4.00},
    {0.00, 21.00, 64.50, 174.00, 2.00, 6.00},
    {0.00, 21.00, -64.50, 174.00, 2.00, 6.00},
    {64.50, 21.00, 0.00, 6.00, 2.00, 174.00},
    {-64.50, 21.00, 0.00, 6.00, 2.00, 174.00},
    {0.00, 9.00, 52.00, 14.00, 12.00, 2.00},
    {-10.00, 5.00, -20.00, 12.00, 4.00, 12.00},
    {10.00, 5.00, -20.00, 12.00, 4.00, 12.00}
};

static const Box map_geo_story_cave[] = {
    {0.0f, -8.0f, 0.0f, 140.0f, 8.0f, 1520.0f},
    {0.0f, 18.0f, 0.0f, 138.0f, 8.0f, 1520.0f},
    {-70.0f, 5.0f, 0.0f, 8.0f, 30.0f, 1520.0f},
    {70.0f, 5.0f, 0.0f, 8.0f, 30.0f, 1520.0f},
    {-12.0f, 2.0f, -480.0f, 42.0f, 14.0f, 80.0f},
    {24.0f, 2.0f, -160.0f, 52.0f, 16.0f, 90.0f},
    {-20.0f, 2.0f, 170.0f, 48.0f, 15.0f, 92.0f},
    {12.0f, 2.0f, 500.0f, 60.0f, 17.0f, 100.0f},
    {-18.0f, 2.0f, 780.0f, 58.0f, 15.0f, 94.0f},
    {0.0f, 2.0f, 1020.0f, 62.0f, 18.0f, 110.0f},
    {34.0f, 9.0f, -620.0f, 12.0f, 18.0f, 140.0f},
    {-28.0f, 9.0f, -280.0f, 10.0f, 16.0f, 120.0f},
    {30.0f, 9.0f, 80.0f, 12.0f, 16.0f, 130.0f},
    {-34.0f, 9.0f, 420.0f, 10.0f, 17.0f, 128.0f},
    {26.0f, 9.0f, 700.0f, 12.0f, 16.0f, 116.0f}
};


#define CITY_MAX_BOXES 2048
static Box map_geo_voxworld[CITY_MAX_BOXES];
static int map_geo_voxworld_count = 0;
static int map_geo_voxworld_init = 0;
static Box map_geo_dust[CITY_MAX_BOXES];
static int map_geo_dust_count = 0;
static int map_geo_dust_init = 0;
static Box map_geo_tanker[CITY_MAX_BOXES];
static int map_geo_tanker_count = 0;
static int map_geo_tanker_init = 0;
static Box map_geo_poo_poo_island[CITY_MAX_BOXES];
static int map_geo_poo_poo_island_count = 0;
static int map_geo_poo_poo_island_init = 0;

static const Box *map_geo = map_geo_stadium;
static int map_count = 0;

#define GARAGE_KILL_Y -30.0f
#define GARAGE_BOUNDS_X 70.0f
#define GARAGE_BOUNDS_Z 70.0f

#define STADIUM_KILL_Y -140.0f
#define STADIUM_BOUNDS_X 1120.0f
#define STADIUM_BOUNDS_Z 1120.0f
#define STADIUM_TERRAIN_W 140
#define STADIUM_TERRAIN_H 140
#define STADIUM_CELL 14.0f
#define STADIUM_ORIGIN_X (-(STADIUM_TERRAIN_W * STADIUM_CELL * 0.5f))
#define STADIUM_ORIGIN_Z (-(STADIUM_TERRAIN_H * STADIUM_CELL * 0.5f))

#define VOXWORLD_KILL_Y -180.0f
#define VOXWORLD_TERRAIN_W 160
#define VOXWORLD_TERRAIN_H 96
#define VOXWORLD_CELL 18.0f
#define VOXWORLD_ORIGIN_X (-(VOXWORLD_TERRAIN_W * VOXWORLD_CELL * 0.5f))
#define VOXWORLD_ORIGIN_Z (-(VOXWORLD_TERRAIN_H * VOXWORLD_CELL * 0.5f))
#define VOXWORLD_LENGTH 2600.0f
#define VOXWORLD_HALF_LENGTH (VOXWORLD_LENGTH * 0.5f)
#define VOXWORLD_WIDTH 1400.0f
#define VOXWORLD_HALF_WIDTH (VOXWORLD_WIDTH * 0.5f)
#define VOXWORLD_BOUNDS_X (VOXWORLD_HALF_LENGTH + 250.0f)
#define VOXWORLD_BOUNDS_Z (VOXWORLD_HALF_WIDTH + 220.0f)
#define VOXWORLD_BASE_RED_X -1040.0f
#define VOXWORLD_BASE_BLUE_X 1040.0f
#define VOXWORLD_BASE_Z 0.0f
#define VOXWORLD_HELI_RED_X (VOXWORLD_BASE_RED_X - 24.0f)
#define VOXWORLD_HELI_RED_Z 34.0f
#define VOXWORLD_HELI_BLUE_X (-VOXWORLD_HELI_RED_X)
#define VOXWORLD_HELI_BLUE_Z (-VOXWORLD_HELI_RED_Z)
#define VOXWORLD_HELI_GROUNDED_OFFSET 1.3f
#define DUST_KILL_Y -90.0f
#define DUST_TERRAIN_W 92
#define DUST_TERRAIN_H 92
#define DUST_CELL 12.0f
#define DUST_ORIGIN_X (-(DUST_TERRAIN_W * DUST_CELL * 0.5f))
#define DUST_ORIGIN_Z (-(DUST_TERRAIN_H * DUST_CELL * 0.5f))
#define DUST_BOUNDS_X 560.0f
#define DUST_BOUNDS_Z 540.0f
#define DUST_ATTACK_SPAWN_X -430.0f
#define DUST_ATTACK_SPAWN_Z -210.0f
#define DUST_DEFEND_SPAWN_X 420.0f
#define DUST_DEFEND_SPAWN_Z 200.0f
#define DUST_MID_X 0.0f
#define DUST_MID_Z 0.0f
#define DUST_UNDERPASS_X 10.0f
#define DUST_UNDERPASS_Z -170.0f
#define DUST_A_SITE_X 250.0f
#define DUST_A_SITE_Z -140.0f
#define DUST_B_SITE_X 270.0f
#define DUST_B_SITE_Z 170.0f
#define DUST_BRIDGE_X 20.0f
#define DUST_BRIDGE_Z -20.0f

#define TANKER_KILL_Y -70.0f
#define TANKER_BOUNDS_X 360.0f
#define TANKER_BOUNDS_Z 240.0f
#define POO_POO_ISLAND_KILL_Y -110.0f
#define POO_POO_ISLAND_TERRAIN_W 172
#define POO_POO_ISLAND_TERRAIN_H 172
#define POO_POO_ISLAND_CELL 14.0f
#define POO_POO_ISLAND_ORIGIN_X (-(POO_POO_ISLAND_TERRAIN_W * POO_POO_ISLAND_CELL * 0.5f))
#define POO_POO_ISLAND_ORIGIN_Z (-(POO_POO_ISLAND_TERRAIN_H * POO_POO_ISLAND_CELL * 0.5f))
#define POO_POO_ISLAND_BOUNDS_X 1240.0f
#define POO_POO_ISLAND_BOUNDS_Z 1240.0f
#define STORY_CAVE_KILL_Y -80.0f
#define STORY_CAVE_BOUNDS_X 110.0f
#define STORY_CAVE_BOUNDS_Z 820.0f

#define GARAGE_PORTAL_X 0.0f
#define GARAGE_PORTAL_Y 6.0f
#define GARAGE_PORTAL_Z 56.0f
#define GARAGE_PORTAL_RADIUS 6.0f
#define GARAGE_VOX_PORTAL_X 48.0f
#define GARAGE_VOX_PORTAL_Y 6.0f
#define GARAGE_VOX_PORTAL_Z 0.0f
#define GARAGE_VOX_PORTAL_RADIUS 6.5f
#define GARAGE_DUST_PORTAL_X -48.0f
#define GARAGE_DUST_PORTAL_Y 6.0f
#define GARAGE_DUST_PORTAL_Z -12.0f
#define GARAGE_DUST_PORTAL_RADIUS 6.0f
#define GARAGE_TANKER_PORTAL_X -48.0f
#define GARAGE_TANKER_PORTAL_Y 6.0f
#define GARAGE_TANKER_PORTAL_Z 0.0f
#define GARAGE_TANKER_PORTAL_RADIUS 6.0f
#define GARAGE_POO_POO_PORTAL_X 52.0f
#define GARAGE_POO_POO_PORTAL_Y 6.0f
#define GARAGE_POO_POO_PORTAL_Z -24.0f
#define GARAGE_POO_POO_PORTAL_RADIUS 6.5f
#define STADIUM_PORTAL_X 0.0f
#define STADIUM_PORTAL_Y 2.0f
#define STADIUM_PORTAL_Z 0.0f
#define STADIUM_PORTAL_RADIUS 16.0f
#define STADIUM_EDGE_PORTAL_X 406.0f
#define STADIUM_EDGE_PORTAL_Y 2.0f
#define STADIUM_EDGE_PORTAL_Z 0.0f
#define STADIUM_EDGE_PORTAL_RADIUS 14.0f
#define STADIUM_EDGE_TELEPORT_X -360.0f
#define STADIUM_EDGE_TELEPORT_Y 2.0f
#define STADIUM_EDGE_TELEPORT_Z 0.0f
#define VOXWORLD_PORTAL_X -360.0f
#define VOXWORLD_PORTAL_Y 2.0f
#define VOXWORLD_PORTAL_Z 0.0f
#define VOXWORLD_PORTAL_RADIUS 16.0f
#define DUST_PORTAL_X -470.0f
#define DUST_PORTAL_Y 6.0f
#define DUST_PORTAL_Z -210.0f
#define DUST_PORTAL_RADIUS 14.0f
#define TANKER_PORTAL_X 292.0f
#define TANKER_PORTAL_Y 6.0f
#define TANKER_PORTAL_Z 0.0f
#define TANKER_PORTAL_RADIUS 13.0f
#define POO_POO_ISLAND_PORTAL_X -880.0f
#define POO_POO_ISLAND_PORTAL_Y 7.0f
#define POO_POO_ISLAND_PORTAL_Z -160.0f
#define POO_POO_ISLAND_PORTAL_RADIUS 14.0f
#define PORTAL_ID_GARAGE_EXIT 0
#define PORTAL_ID_STADIUM_TO_VOXWORLD 1
#define PORTAL_ID_VOXWORLD_TO_STADIUM 2
#define PORTAL_ID_GARAGE_TO_VOXWORLD 3
#define PORTAL_ID_GARAGE_TO_DUST 4
#define PORTAL_ID_DUST_TO_GARAGE 5
#define PORTAL_ID_GARAGE_TO_TANKER 6
#define PORTAL_ID_TANKER_TO_GARAGE 7
#define PORTAL_ID_GARAGE_TO_POO_POO_ISLAND 8
#define PORTAL_ID_POO_POO_ISLAND_TO_GARAGE 9

#define POO_POO_HUB_X -280.0f
#define POO_POO_HUB_Z -90.0f
#define POO_POO_MARINA_X -610.0f
#define POO_POO_MARINA_Z -320.0f
#define POO_POO_LIGHTHOUSE_X 760.0f
#define POO_POO_LIGHTHOUSE_Z -520.0f
#define POO_POO_VOLCANO_X 640.0f
#define POO_POO_VOLCANO_Z 560.0f
#define POO_POO_BEACH_X -760.0f
#define POO_POO_BEACH_Z 310.0f

typedef struct {
    float x;
    float y;
    float z;
    const char *label;
} VehiclePad;

typedef struct {
    float x;
    float z;
    const char *label;
} VoxRouteAnchor;

typedef struct {
    float x, y, z;
    float scale;
    float yaw;
    float tint;
} BushProp;

static const VehiclePad garage_vehicle_pads[] = {
    {-30.0f, 0.0f, -30.0f, "FOXBODY '93"},
    {0.0f, 0.0f, -30.0f, "LANDSHIP"},
    {30.0f, 0.0f, -30.0f, "RESERVED"}
};

static const Vec2 voxworld_spawn_points_red[] = {
    {-1120.0f, -90.0f}, {-1090.0f, 90.0f}, {-980.0f, 0.0f},
    {-940.0f, -220.0f}, {-940.0f, 220.0f}, {-1180.0f, 0.0f}
};
static const Vec2 voxworld_spawn_points_blue[] = {
    {1120.0f, -90.0f}, {1090.0f, 90.0f}, {980.0f, 0.0f},
    {940.0f, -220.0f}, {940.0f, 220.0f}, {1180.0f, 0.0f}
};
static const Vec2 voxworld_spawn_points_ffa[] = {
    {-700.0f, -180.0f}, {-600.0f, 150.0f}, {-320.0f, 260.0f}, {-260.0f, -250.0f},
    {-40.0f, -200.0f}, {20.0f, 220.0f}, {260.0f, -260.0f}, {320.0f, 230.0f},
    {580.0f, -190.0f}, {690.0f, 170.0f}, {-170.0f, 470.0f}, {190.0f, -470.0f}
};
static const VehiclePad voxworld_vehicle_pads[] = {
    {-1000.0f, 0.0f, -85.0f, "RED WARTHOG"},
    {-1015.0f, 0.0f, 105.0f, "RED GHOST"},
    {1000.0f, 0.0f, 85.0f, "BLUE WARTHOG"},
    {1015.0f, 0.0f, -105.0f, "BLUE GHOST"}
};
static const Vec2 voxworld_flag_home_red = {-1080.0f, 0.0f};
static const Vec2 voxworld_flag_home_blue = {1080.0f, 0.0f};
static const Vec2 voxworld_teleporters[] = {
    {-1015.0f, 180.0f}, {1015.0f, -180.0f}
};
static const Vec2 voxworld_teleport_destinations[] = {
    {-120.0f, 470.0f}, {120.0f, -470.0f}
};
static const VoxRouteAnchor voxworld_route_anchors[] = {
    {0.0f, 0.0f, "CENTER LANE"},
    {0.0f, -500.0f, "LEFT CAVE CUT"},
    {0.0f, 500.0f, "RIGHT CLIFF SHELF"},
    {VOXWORLD_BASE_RED_X, 0.0f, "RED BASE"},
    {VOXWORLD_BASE_BLUE_X, 0.0f, "BLUE BASE"}
};

static const Vec2 dust_spawn_points_attack[] = {
    {-455.0f, -250.0f}, {-445.0f, -190.0f}, {-390.0f, -225.0f},
    {-360.0f, -165.0f}, {-330.0f, -260.0f}, {-300.0f, -200.0f}
};
static const Vec2 dust_spawn_points_defend[] = {
    {470.0f, 250.0f}, {450.0f, 180.0f}, {390.0f, 210.0f},
    {365.0f, 140.0f}, {330.0f, 260.0f}, {300.0f, 180.0f}
};
static const Vec2 dust_spawn_points_dm[] = {
    {-430.0f, -210.0f}, {-290.0f, -150.0f}, {-180.0f, -190.0f}, {-90.0f, -70.0f},
    {40.0f, -210.0f}, {120.0f, -20.0f}, {210.0f, -140.0f}, {290.0f, -10.0f},
    {-210.0f, 180.0f}, {-60.0f, 140.0f}, {110.0f, 200.0f}, {260.0f, 170.0f}
};
static const Vec2 tanker_spawn_points_dm[] = {
    {-260.0f, 0.0f}, {-210.0f, -90.0f}, {-205.0f, 92.0f}, {-120.0f, -140.0f},
    {-110.0f, 140.0f}, {-30.0f, -72.0f}, {-20.0f, 82.0f}, {80.0f, -155.0f},
    {90.0f, 155.0f}, {150.0f, -30.0f}, {155.0f, 35.0f}, {220.0f, 0.0f}
};
static const Vec2 tanker_spawn_points_red[] = {
    {-265.0f, -92.0f}, {-258.0f, 88.0f}, {-225.0f, 0.0f},
    {-190.0f, -130.0f}, {-185.0f, 128.0f}, {-150.0f, 0.0f}
};
static const Vec2 tanker_spawn_points_blue[] = {
    {265.0f, 92.0f}, {258.0f, -88.0f}, {225.0f, 0.0f},
    {190.0f, 130.0f}, {185.0f, -128.0f}, {150.0f, 0.0f}
};
static const Vec2 stadium_spawn_points_dm[] = {
    {-280.0f, -170.0f}, {-280.0f, 170.0f}, {-210.0f, -250.0f}, {-210.0f, 250.0f},
    {-120.0f, -180.0f}, {-120.0f, 180.0f}, {0.0f, -250.0f}, {0.0f, 250.0f},
    {120.0f, -180.0f}, {120.0f, 180.0f}, {210.0f, -250.0f}, {210.0f, 250.0f}
};
static const Vec2 stadium_spawn_points_red[] = {
    {-325.0f, -165.0f}, {-315.0f, 165.0f}, {-280.0f, 0.0f},
    {-240.0f, -245.0f}, {-240.0f, 245.0f}, {-190.0f, 0.0f}
};
static const Vec2 stadium_spawn_points_blue[] = {
    {325.0f, 165.0f}, {315.0f, -165.0f}, {280.0f, 0.0f},
    {240.0f, 245.0f}, {240.0f, -245.0f}, {190.0f, 0.0f}
};
static const VoxRouteAnchor dust_route_anchors[] = {
    {DUST_MID_X, DUST_MID_Z, "MID"},
    {DUST_UNDERPASS_X, DUST_UNDERPASS_Z, "UNDERPASS"},
    {DUST_BRIDGE_X, DUST_BRIDGE_Z, "BRIDGE"},
    {DUST_A_SITE_X, DUST_A_SITE_Z, "A SITE"},
    {DUST_B_SITE_X, DUST_B_SITE_Z, "B SITE"}
};
static const VoxRouteAnchor dust_objective_anchors[] = {
    {DUST_A_SITE_X, DUST_A_SITE_Z, "ALPHA"},
    {DUST_B_SITE_X, DUST_B_SITE_Z, "BRAVO"}
};
static const Vec2 poo_poo_island_spawn_points_red[] = {
    {-430.0f, -130.0f}, {-360.0f, -40.0f}, {-290.0f, -170.0f},
    {-520.0f, -260.0f}, {-660.0f, -320.0f}, {-760.0f, -220.0f}
};
static const Vec2 poo_poo_island_spawn_points_blue[] = {
    {500.0f, 420.0f}, {620.0f, 520.0f}, {760.0f, 470.0f},
    {760.0f, 340.0f}, {880.0f, 260.0f}, {580.0f, 300.0f}
};
static const Vec2 poo_poo_island_spawn_points_dm[] = {
    {-520.0f, -140.0f}, {-360.0f, -80.0f}, {-250.0f, -250.0f}, {-150.0f, 130.0f},
    {-720.0f, 230.0f}, {-810.0f, -340.0f}, {-610.0f, -330.0f}, {-100.0f, -20.0f},
    {140.0f, 180.0f}, {360.0f, 340.0f}, {620.0f, 560.0f}, {820.0f, -420.0f},
    {760.0f, 180.0f}, {540.0f, -120.0f}
};
static const VoxRouteAnchor poo_poo_island_route_anchors[] = {
    {-560.0f, -210.0f, "HUB LOOP WEST"},
    {-120.0f, -60.0f, "HUB LOOP EAST"},
    {240.0f, 160.0f, "GREEN BELT"},
    {510.0f, 340.0f, "VOLCANO APPROACH"},
    {770.0f, -210.0f, "COASTAL ROAD"},
    {-390.0f, 250.0f, "BEACH LOOP"}
};
static const VoxRouteAnchor poo_poo_island_landmark_anchors[] = {
    {POO_POO_HUB_X, POO_POO_HUB_Z, "TOWN PLAZA"},
    {POO_POO_MARINA_X, POO_POO_MARINA_Z, "MARINA"},
    {POO_POO_BEACH_X, POO_POO_BEACH_Z, "BEACH"},
    {POO_POO_LIGHTHOUSE_X, POO_POO_LIGHTHOUSE_Z, "LIGHTHOUSE"},
    {POO_POO_VOLCANO_X, POO_POO_VOLCANO_Z, "VOLCANO"}
};
static const VoxRouteAnchor poo_poo_island_hub_anchors[] = {
    {-350.0f, -120.0f, "TOWN NORTH"},
    {-300.0f, -180.0f, "FOUNTAIN PLAZA"},
    {-440.0f, -150.0f, "MARKET STREET"}
};
static const VoxRouteAnchor poo_poo_island_scenic_anchors[] = {
    {-800.0f, 320.0f, "RESORT BEACH"},
    {760.0f, -520.0f, "LIGHTHOUSE POINT"},
    {620.0f, 560.0f, "VOLCANO RIDGE"}
};
static const VoxRouteAnchor poo_poo_island_activity_anchors[] = {
    {-620.0f, -310.0f, "MARINA PAD"},
    {-160.0f, 30.0f, "LAKE ROUTE"},
    {300.0f, 220.0f, "HILL TRAIL"},
    {470.0f, 480.0f, "VOLCANO TRAIL"}
};

#define MAX_VOXWORLD_BUSHES 256
#define VOXWORLD_BUSH_DENSITY 1.0f
static BushProp g_voxworld_bushes[MAX_VOXWORLD_BUSHES];
static int g_voxworld_bush_count = 0;
static int g_voxworld_bushes_ready = 0;

float phys_rand_f() { return ((float)(rand()%1000)/500.0f) - 1.0f; }

static int g_phys_game_mode = MODE_DEATHMATCH;
static inline int phys_team_mode_enabled(void) {
    return g_phys_game_mode == MODE_TDM || g_phys_game_mode == MODE_CTF || g_phys_game_mode == MODE_TDMB || g_phys_game_mode == MODE_TDMO || g_phys_game_mode == MODE_CTFB || g_phys_game_mode == MODE_CTFO;
}

static inline int phys_is_friendly(const PlayerState *a, const PlayerState *b) {
    if (!a || !b) return 0;
    if (!phys_team_mode_enabled()) return 0;
    return a->team_id >= 0 && a->team_id == b->team_id;
}

static TerrainHeightfield g_scene_terrain;

static const Vec2 stadium_rally_loop[] = {
    {680.0f, -180.0f},
    {620.0f, 250.0f},
    {360.0f, 610.0f},
    {-90.0f, 700.0f},
    {-430.0f, 560.0f},
    {-690.0f, 250.0f},
    {-720.0f, -210.0f},
    {-500.0f, -600.0f},
    {-120.0f, -730.0f},
    {300.0f, -640.0f},
    {620.0f, -420.0f}
};

static inline float vox_hash_noise(float x, float z) {
    return sinf(x * 0.00431f + z * 0.00711f) * cosf(z * 0.00377f - x * 0.00623f);
}

static inline float stadium_height_at(float x, float z) {
    if (g_scene_terrain.active && g_scene_terrain.heights) {
        return terrain_sample_height(&g_scene_terrain, x, z);
    }
    return 0.0f;
}

static inline float stadium_point_to_segment_dist(float px, float pz, float ax, float az, float bx, float bz) {
    float vx = bx - ax;
    float vz = bz - az;
    float wx = px - ax;
    float wz = pz - az;
    float vv = vx * vx + vz * vz;
    float t = (vv > 0.0001f) ? ((wx * vx + wz * vz) / vv) : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float cx = ax + vx * t;
    float cz = az + vz * t;
    float dx = px - cx;
    float dz = pz - cz;
    return sqrtf(dx * dx + dz * dz);
}

static inline float stadium_road_distance(float x, float z) {
    float best = 1e9f;
    int count = (int)(sizeof(stadium_rally_loop) / sizeof(stadium_rally_loop[0]));
    for (int i = 0; i < count; i++) {
        int next = (i + 1) % count;
        float d = stadium_point_to_segment_dist(
            x, z,
            stadium_rally_loop[i].x, stadium_rally_loop[i].y,
            stadium_rally_loop[next].x, stadium_rally_loop[next].y
        );
        if (d < best) best = d;
    }
    return best;
}

static inline float stadium_track_weight_at(float x, float z) {
    const float road_half_width = 64.0f;
    const float shoulder_width = 86.0f;
    float d = stadium_road_distance(x, z);
    if (d >= shoulder_width) return 0.0f;
    float t = 1.0f - (d - road_half_width) / (shoulder_width - road_half_width);
    if (d <= road_half_width) t = 1.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static inline void vox_terrain_stamp(TerrainHeightfield *t, float cx, float cz, float radius, float target_h, float blend) {
    if (!t || !t->heights || radius <= 0.0f) return;
    float minx = cx - radius;
    float maxx = cx + radius;
    float minz = cz - radius;
    float maxz = cz + radius;
    int gx0 = (int)((minx - t->origin_x) / t->cell_size); if (gx0 < 0) gx0 = 0;
    int gz0 = (int)((minz - t->origin_z) / t->cell_size); if (gz0 < 0) gz0 = 0;
    int gx1 = (int)((maxx - t->origin_x) / t->cell_size); if (gx1 > t->width - 1) gx1 = t->width - 1;
    int gz1 = (int)((maxz - t->origin_z) / t->cell_size); if (gz1 > t->height - 1) gz1 = t->height - 1;
    for (int gz = gz0; gz <= gz1; gz++) {
        for (int gx = gx0; gx <= gx1; gx++) {
            float wx = t->origin_x + gx * t->cell_size;
            float wz = t->origin_z + gz * t->cell_size;
            float dx = wx - cx;
            float dz = wz - cz;
            float dist = sqrtf(dx * dx + dz * dz);
            if (dist > radius) continue;
            float falloff = 1.0f - (dist / radius);
            falloff = falloff * falloff * (3.0f - 2.0f * falloff);
            float cur = terrain_get_height(t, gx, gz);
            float target = cur + (target_h - cur) * blend * falloff;
            terrain_set_height(t, gx, gz, target);
        }
    }
}

static inline void vox_terrain_smooth(TerrainHeightfield *t, int passes, float alpha) {
    if (!t || !t->heights || passes <= 0) return;
    int n = t->width * t->height;
    float *scratch = (float*)malloc(sizeof(float) * (size_t)n);
    if (!scratch) return;
    for (int pass = 0; pass < passes; pass++) {
        for (int gz = 0; gz < t->height; gz++) {
            for (int gx = 0; gx < t->width; gx++) {
                float sum = 0.0f;
                int cnt = 0;
                for (int dz = -1; dz <= 1; dz++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        int sx = gx + dx; if (sx < 0) sx = 0; if (sx > t->width - 1) sx = t->width - 1;
                        int sz = gz + dz; if (sz < 0) sz = 0; if (sz > t->height - 1) sz = t->height - 1;
                        sum += terrain_get_height(t, sx, sz);
                        cnt++;
                    }
                }
                float cur = terrain_get_height(t, gx, gz);
                float avg = (cnt > 0) ? (sum / (float)cnt) : cur;
                scratch[gz * t->width + gx] = cur + (avg - cur) * alpha;
            }
        }
        for (int gz = 0; gz < t->height; gz++) {
            for (int gx = 0; gx < t->width; gx++) {
                terrain_set_height(t, gx, gz, scratch[gz * t->width + gx]);
            }
        }
    }
    free(scratch);
}

static inline void voxworld_add_box(float x, float y, float z, float w, float h, float d) {
    if (map_geo_voxworld_count >= CITY_MAX_BOXES) return;
    map_geo_voxworld[map_geo_voxworld_count++] = (Box){x, y, z, w, h, d};
}

static inline float voxworld_height_at(float x, float z) {
    if (g_scene_terrain.active && g_scene_terrain.heights) {
        return terrain_sample_height(&g_scene_terrain, x, z);
    }
    return 0.0f;
}

static inline float voxworld_base_roof_top_y(float base_x) {
    return voxworld_height_at(base_x, 0.0f) + 62.0f;
}

static inline float voxworld_heli_spawn_y(float base_x) {
    return voxworld_base_roof_top_y(base_x) + VOXWORLD_HELI_GROUNDED_OFFSET;
}

static inline void voxworld_add_stair_ramp(float x0, float z0, float x1, float z1,
                                           float width, float base_y, float rise, int steps) {
    if (steps < 2) steps = 2;
    float dx = x1 - x0;
    float dz = z1 - z0;
    float len = sqrtf(dx * dx + dz * dz);
    if (len < 0.1f) return;
    float dir_x = dx / len;
    float dir_z = dz / len;
    float step_len = len / (float)steps;
    float step_h = rise / (float)steps;
    for (int i = 0; i < steps; i++) {
        float t = ((float)i + 0.5f) / (float)steps;
        float cx = x0 + dir_x * (len * t);
        float cz = z0 + dir_z * (len * t);
        float cy = base_y + step_h * ((float)i + 0.5f);
        float box_w = (fabsf(dir_x) > fabsf(dir_z)) ? step_len : width;
        float box_d = (fabsf(dir_x) > fabsf(dir_z)) ? width : step_len;
        voxworld_add_box(cx, cy, cz, box_w + 1.0f, step_h + 0.5f, box_d + 1.0f);
    }
}

static inline void voxworld_build_base_geo(float base_x, int team_sign) {
    float front_x = base_x + (float)team_sign * 110.0f;
    float shell_h = voxworld_height_at(base_x, 0.0f);
    float front_h = voxworld_height_at(front_x, 0.0f);
    float roof_y = shell_h + 58.0f;
    float bay_y = shell_h + 18.0f;
    float room_y = shell_h + 24.0f;

    voxworld_add_box(base_x, shell_h + 8.0f, 0.0f, 170.0f, 16.0f, 150.0f);
    voxworld_add_box(base_x - (float)team_sign * 18.0f, room_y, 0.0f, 126.0f, 26.0f, 92.0f);
    voxworld_add_box(base_x, roof_y, 0.0f, 148.0f, 8.0f, 124.0f);
    voxworld_add_box(base_x - (float)team_sign * 62.0f, shell_h + 30.0f, -65.0f, 18.0f, 36.0f, 24.0f);
    voxworld_add_box(base_x - (float)team_sign * 62.0f, shell_h + 30.0f, 65.0f, 18.0f, 36.0f, 24.0f);

    voxworld_add_box(base_x + (float)team_sign * 34.0f, bay_y + 9.0f, -64.0f, 10.0f, 20.0f, 22.0f);
    voxworld_add_box(base_x + (float)team_sign * 34.0f, bay_y + 9.0f, 64.0f, 10.0f, 20.0f, 22.0f);
    voxworld_add_box(base_x + (float)team_sign * 68.0f, front_h + 10.0f, 0.0f, 38.0f, 14.0f, 64.0f);
    voxworld_add_box(base_x + (float)team_sign * 94.0f, front_h + 5.0f, -95.0f, 58.0f, 6.0f, 36.0f);
    voxworld_add_box(base_x + (float)team_sign * 94.0f, front_h + 5.0f, 95.0f, 58.0f, 6.0f, 36.0f);

    voxworld_add_box(base_x - (float)team_sign * 24.0f, shell_h + 44.0f, -10.0f, 14.0f, 26.0f, 44.0f);
    voxworld_add_box(base_x - (float)team_sign * 18.0f, shell_h + 53.0f, 0.0f, 42.0f, 8.0f, 34.0f);
    voxworld_add_box(base_x - (float)team_sign * 122.0f, shell_h + 22.0f, (float)team_sign * 165.0f, 34.0f, 20.0f, 52.0f);

    float back_x = base_x - (float)team_sign * 170.0f;
    float side_x = base_x - (float)team_sign * 114.0f;
    float rear_ground_h = voxworld_height_at(back_x, (float)team_sign * 128.0f);
    float side_mid_h = shell_h + 20.0f;
    float roof_edge_h = shell_h + 56.0f;

    voxworld_add_stair_ramp(back_x, (float)team_sign * 128.0f,
                            side_x, (float)team_sign * 128.0f,
                            28.0f,
                            rear_ground_h + 2.0f,
                            side_mid_h - (rear_ground_h + 2.0f),
                            5);
    voxworld_add_stair_ramp(side_x, (float)team_sign * 128.0f,
                            base_x - (float)team_sign * 62.0f, (float)team_sign * 78.0f,
                            24.0f,
                            side_mid_h,
                            (roof_edge_h - side_mid_h),
                            5);
    voxworld_add_stair_ramp(base_x - (float)team_sign * 62.0f, (float)team_sign * 78.0f,
                            base_x - (float)team_sign * 28.0f, (float)team_sign * 42.0f,
                            20.0f,
                            roof_edge_h,
                            (voxworld_base_roof_top_y(base_x) - roof_edge_h),
                            3);
}

static inline void init_voxworld_bloodgulch_geo(void) {
    if (map_geo_voxworld_init) return;
    map_geo_voxworld_init = 1;
    map_geo_voxworld_count = 0;

    voxworld_add_box(0.0f, -8.0f, 0.0f, 3400.0f, 10.0f, 2200.0f);
    voxworld_add_box(0.0f, 110.0f, VOXWORLD_BOUNDS_Z, 3400.0f, 260.0f, 16.0f);
    voxworld_add_box(0.0f, 110.0f, -VOXWORLD_BOUNDS_Z, 3400.0f, 260.0f, 16.0f);
    voxworld_add_box(VOXWORLD_BOUNDS_X, 110.0f, 0.0f, 16.0f, 260.0f, 2200.0f);
    voxworld_add_box(-VOXWORLD_BOUNDS_X, 110.0f, 0.0f, 16.0f, 260.0f, 2200.0f);

    voxworld_build_base_geo(VOXWORLD_BASE_RED_X, +1);
    voxworld_build_base_geo(VOXWORLD_BASE_BLUE_X, -1);

    for (int i = -2; i <= 2; i++) {
        float cx = (float)i * 280.0f;
        float cz = (i % 2 == 0) ? -120.0f : 120.0f;
        float cy = voxworld_height_at(cx, cz) + 8.0f;
        voxworld_add_box(cx, cy, cz, 42.0f, 16.0f, 36.0f);
    }
    for (int i = 0; i < 4; i++) {
        float side = (i < 2) ? -1.0f : 1.0f;
        float x = (i % 2 == 0) ? -380.0f : 380.0f;
        float z = side * 500.0f;
        float y = voxworld_height_at(x, z) + 10.0f;
        voxworld_add_box(x, y, z, 52.0f, 20.0f, 26.0f);
    }
    printf("[VOXWORLD] authored geo boxes=%d\n", map_geo_voxworld_count);
}

static inline float dust_height_at(float x, float z) {
    if (g_scene_terrain.active && g_scene_terrain.heights) {
        return terrain_sample_height(&g_scene_terrain, x, z);
    }
    return 0.0f;
}

static inline void dust_add_box(float x, float y, float z, float w, float h, float d) {
    if (map_geo_dust_count >= CITY_MAX_BOXES) return;
    map_geo_dust[map_geo_dust_count++] = (Box){x, y, z, w, h, d};
}

static inline void add_geo_stair_ramp(float x0, float z0, float x1, float z1,
                                      float width, float base_y, float rise, int steps) {
    if (steps < 2) steps = 2;
    float dx = x1 - x0;
    float dz = z1 - z0;
    float len = sqrtf(dx * dx + dz * dz);
    if (len < 0.1f) return;
    float dir_x = dx / len;
    float dir_z = dz / len;
    float step_len = len / (float)steps;
    float step_h = rise / (float)steps;
    for (int i = 0; i < steps; i++) {
        float t = ((float)i + 0.5f) / (float)steps;
        float cx = x0 + dir_x * (len * t);
        float cz = z0 + dir_z * (len * t);
        float cy = base_y + step_h * ((float)i + 0.5f);
        float box_w = (fabsf(dir_x) > fabsf(dir_z)) ? step_len : width;
        float box_d = (fabsf(dir_x) > fabsf(dir_z)) ? width : step_len;
        dust_add_box(cx, cy, cz, box_w + 1.0f, step_h + 0.4f, box_d + 1.0f);
    }
}

static inline void init_dust_compound_geo(void) {
    if (map_geo_dust_init) return;
    map_geo_dust_init = 1;
    map_geo_dust_count = 0;

    dust_add_box(0.0f, -10.0f, 0.0f, 1400.0f, 12.0f, 1400.0f);
    dust_add_box(0.0f, 100.0f, DUST_BOUNDS_Z, 1400.0f, 220.0f, 14.0f);
    dust_add_box(0.0f, 100.0f, -DUST_BOUNDS_Z, 1400.0f, 220.0f, 14.0f);
    dust_add_box(DUST_BOUNDS_X, 100.0f, 0.0f, 14.0f, 220.0f, 1200.0f);
    dust_add_box(-DUST_BOUNDS_X, 100.0f, 0.0f, 14.0f, 220.0f, 1200.0f);

    float mid_h = dust_height_at(DUST_MID_X, DUST_MID_Z);
    float under_h = dust_height_at(DUST_UNDERPASS_X, DUST_UNDERPASS_Z);
    float a_h = dust_height_at(DUST_A_SITE_X, DUST_A_SITE_Z);
    float b_h = dust_height_at(DUST_B_SITE_X, DUST_B_SITE_Z);
    float bridge_h = dust_height_at(DUST_BRIDGE_X, DUST_BRIDGE_Z) + 24.0f;

    dust_add_box(-120.0f, mid_h + 7.0f, 0.0f, 26.0f, 14.0f, 250.0f);
    dust_add_box(120.0f, mid_h + 7.0f, -15.0f, 26.0f, 14.0f, 250.0f);
    dust_add_box(0.0f, mid_h + 6.0f, 115.0f, 220.0f, 12.0f, 20.0f);
    dust_add_box(-10.0f, mid_h + 8.0f, -98.0f, 210.0f, 16.0f, 20.0f);
    dust_add_box(-30.0f, mid_h + 8.0f, -8.0f, 40.0f, 16.0f, 40.0f);

    dust_add_box(20.0f, bridge_h, -20.0f, 240.0f, 10.0f, 46.0f);
    dust_add_box(-95.0f, bridge_h + 10.0f, -20.0f, 26.0f, 20.0f, 46.0f);
    dust_add_box(135.0f, bridge_h + 10.0f, -20.0f, 26.0f, 20.0f, 46.0f);
    add_geo_stair_ramp(-230.0f, -20.0f, -95.0f, -20.0f, 46.0f, mid_h + 1.0f, bridge_h - (mid_h + 1.0f), 5);
    add_geo_stair_ramp(260.0f, -20.0f, 135.0f, -20.0f, 46.0f, a_h + 2.0f, bridge_h - (a_h + 2.0f), 5);

    dust_add_box(-10.0f, under_h + 3.0f, -170.0f, 120.0f, 7.0f, 150.0f);
    dust_add_box(-85.0f, under_h + 13.0f, -170.0f, 14.0f, 28.0f, 130.0f);
    dust_add_box(75.0f, under_h + 13.0f, -170.0f, 14.0f, 28.0f, 130.0f);
    add_geo_stair_ramp(-35.0f, -240.0f, -35.0f, -98.0f, 34.0f, under_h + 1.0f, 17.0f, 5);
    add_geo_stair_ramp(35.0f, -240.0f, 35.0f, -98.0f, 34.0f, under_h + 1.0f, 17.0f, 5);

    dust_add_box(250.0f, a_h + 5.0f, -140.0f, 170.0f, 10.0f, 140.0f);
    dust_add_box(320.0f, a_h + 14.0f, -115.0f, 32.0f, 22.0f, 32.0f);
    dust_add_box(225.0f, a_h + 10.0f, -210.0f, 80.0f, 20.0f, 16.0f);
    add_geo_stair_ramp(130.0f, -135.0f, 190.0f, -140.0f, 44.0f, mid_h + 1.0f, (a_h - mid_h) + 8.0f, 4);

    dust_add_box(270.0f, b_h + 5.0f, 170.0f, 160.0f, 10.0f, 150.0f);
    dust_add_box(340.0f, b_h + 17.0f, 210.0f, 22.0f, 32.0f, 70.0f);
    dust_add_box(210.0f, b_h + 17.0f, 225.0f, 22.0f, 32.0f, 70.0f);
    dust_add_box(285.0f, b_h + 13.0f, 88.0f, 90.0f, 26.0f, 14.0f);
    add_geo_stair_ramp(135.0f, 105.0f, 190.0f, 145.0f, 36.0f, mid_h + 2.0f, (b_h - mid_h) + 7.0f, 4);

    dust_add_box(-410.0f, dust_height_at(-410.0f, -210.0f) + 8.0f, -210.0f, 120.0f, 14.0f, 90.0f);
    dust_add_box(410.0f, dust_height_at(410.0f, 190.0f) + 8.0f, 200.0f, 120.0f, 14.0f, 90.0f);
    dust_add_box(-230.0f, dust_height_at(-230.0f, -55.0f) + 9.0f, -55.0f, 18.0f, 18.0f, 90.0f);
    dust_add_box(240.0f, dust_height_at(240.0f, 35.0f) + 9.0f, 35.0f, 18.0f, 18.0f, 90.0f);

    printf("[DUST] authored geo boxes=%d ramp_steps=%d\n", map_geo_dust_count, 23);
}


static inline void tanker_add_box(float x, float y, float z, float w, float h, float d) {
    if (map_geo_tanker_count >= CITY_MAX_BOXES) return;
    map_geo_tanker[map_geo_tanker_count++] = (Box){x, y, z, w, h, d};
}

static inline int get_ctf_pedestal_anchor(int scene_id, int team_id,
                                          float *center_x, float *center_y, float *center_z,
                                          float *size_x, float *size_y, float *size_z,
                                          float *top_y,
                                          float *flag_x, float *flag_y, float *flag_z);

static inline void init_oil_tanker_geo(void) {
    if (map_geo_tanker_init) return;
    map_geo_tanker_init = 1;
    map_geo_tanker_count = 0;

    tanker_add_box(0.0f, -3.0f, 0.0f, 620.0f, 6.0f, 420.0f);
    tanker_add_box(0.0f, 2.0f, 0.0f, 560.0f, 4.0f, 220.0f);
    tanker_add_box(0.0f, 2.0f, -145.0f, 520.0f, 4.0f, 44.0f);
    tanker_add_box(0.0f, 2.0f, 145.0f, 520.0f, 4.0f, 44.0f);

    tanker_add_box(-190.0f, 8.0f, 0.0f, 170.0f, 4.0f, 72.0f);
    tanker_add_box(-100.0f, 8.0f, 0.0f, 24.0f, 4.0f, 72.0f);
    tanker_add_box(-145.0f, 11.0f, 0.0f, 18.0f, 2.0f, 72.0f);

    tanker_add_box(190.0f, 11.0f, 0.0f, 120.0f, 6.0f, 96.0f);
    tanker_add_box(210.0f, 17.0f, 0.0f, 70.0f, 6.0f, 54.0f);
    tanker_add_box(225.0f, 22.0f, 0.0f, 34.0f, 4.0f, 30.0f);

    tanker_add_box(-260.0f, 2.0f, -110.0f, 58.0f, 4.0f, 58.0f);
    tanker_add_box(-230.0f, 2.0f, 120.0f, 58.0f, 4.0f, 58.0f);
    tanker_add_box(-90.0f, 2.0f, -100.0f, 66.0f, 4.0f, 40.0f);
    tanker_add_box(-40.0f, 2.0f, 105.0f, 66.0f, 4.0f, 40.0f);
    tanker_add_box(70.0f, 2.0f, -112.0f, 58.0f, 4.0f, 58.0f);
    tanker_add_box(95.0f, 2.0f, 98.0f, 58.0f, 4.0f, 58.0f);

    tanker_add_box(0.0f, 4.0f, -195.0f, 620.0f, 8.0f, 6.0f);
    tanker_add_box(0.0f, 4.0f, 195.0f, 620.0f, 8.0f, 6.0f);
    tanker_add_box(-305.0f, 4.0f, 0.0f, 6.0f, 8.0f, 420.0f);
    tanker_add_box(305.0f, 4.0f, 0.0f, 6.0f, 8.0f, 420.0f);

    tanker_add_box(-260.0f, 7.0f, -65.0f, 16.0f, 8.0f, 50.0f);
    tanker_add_box(-260.0f, 8.0f, -45.0f, 16.0f, 10.0f, 30.0f);
    tanker_add_box(-230.0f, 7.0f, 65.0f, 16.0f, 8.0f, 50.0f);
    tanker_add_box(-230.0f, 8.0f, 45.0f, 16.0f, 10.0f, 30.0f);

    for (int team = 0; team <= 1; team++) {
        float cx = 0.0f, cy = 0.0f, cz = 0.0f;
        float sx = 0.0f, sy = 0.0f, sz = 0.0f;
        if (!get_ctf_pedestal_anchor(SCENE_OIL_TANKER, team,
                                     &cx, &cy, &cz,
                                     &sx, &sy, &sz,
                                     NULL,
                                     NULL, NULL, NULL)) {
            continue;
        }
        tanker_add_box(cx, cy, cz, sx, sy, sz);
    }

    printf("[OIL_TANKER] authored geo boxes=%d\n", map_geo_tanker_count);
}

static inline float poo_poo_island_height_at(float x, float z) {
    if (g_scene_terrain.active && g_scene_terrain.heights) {
        return terrain_sample_height(&g_scene_terrain, x, z);
    }
    return 0.0f;
}

static inline void poo_poo_island_add_box(float x, float y, float z, float w, float h, float d) {
    if (map_geo_poo_poo_island_count >= CITY_MAX_BOXES) return;
    map_geo_poo_poo_island[map_geo_poo_poo_island_count++] = (Box){x, y, z, w, h, d};
}

static inline void init_poo_poo_island_geo(void) {
    if (map_geo_poo_poo_island_init) return;
    map_geo_poo_poo_island_init = 1;
    map_geo_poo_poo_island_count = 0;

    poo_poo_island_add_box(0.0f, -20.0f, 0.0f, 3000.0f, 20.0f, 3000.0f);
    poo_poo_island_add_box(0.0f, 140.0f, POO_POO_ISLAND_BOUNDS_Z, 2800.0f, 320.0f, 16.0f);
    poo_poo_island_add_box(0.0f, 140.0f, -POO_POO_ISLAND_BOUNDS_Z, 2800.0f, 320.0f, 16.0f);
    poo_poo_island_add_box(POO_POO_ISLAND_BOUNDS_X, 140.0f, 0.0f, 16.0f, 320.0f, 2800.0f);
    poo_poo_island_add_box(-POO_POO_ISLAND_BOUNDS_X, 140.0f, 0.0f, 16.0f, 320.0f, 2800.0f);

    float town_h = poo_poo_island_height_at(POO_POO_HUB_X, POO_POO_HUB_Z);
    float marina_h = poo_poo_island_height_at(POO_POO_MARINA_X, POO_POO_MARINA_Z);
    float beach_h = poo_poo_island_height_at(POO_POO_BEACH_X, POO_POO_BEACH_Z);
    float lighthouse_h = poo_poo_island_height_at(POO_POO_LIGHTHOUSE_X, POO_POO_LIGHTHOUSE_Z);
    float volcano_h = poo_poo_island_height_at(POO_POO_VOLCANO_X, POO_POO_VOLCANO_Z);

    poo_poo_island_add_box(-320.0f, town_h + 4.0f, -140.0f, 190.0f, 8.0f, 130.0f);
    poo_poo_island_add_box(-250.0f, town_h + 8.0f, -190.0f, 70.0f, 18.0f, 52.0f);
    poo_poo_island_add_box(-390.0f, town_h + 8.0f, -170.0f, 56.0f, 18.0f, 48.0f);
    poo_poo_island_add_box(-245.0f, town_h + 7.0f, -95.0f, 54.0f, 16.0f, 44.0f);
    poo_poo_island_add_box(-345.0f, town_h + 7.0f, -78.0f, 52.0f, 16.0f, 42.0f);
    poo_poo_island_add_box(-300.0f, town_h + 3.6f, -135.0f, 26.0f, 7.2f, 26.0f);

    poo_poo_island_add_box(-610.0f, marina_h + 3.0f, -320.0f, 190.0f, 6.0f, 130.0f);
    poo_poo_island_add_box(-690.0f, marina_h + 2.5f, -370.0f, 24.0f, 5.0f, 110.0f);
    poo_poo_island_add_box(-640.0f, marina_h + 2.5f, -392.0f, 24.0f, 5.0f, 90.0f);
    poo_poo_island_add_box(-545.0f, marina_h + 2.5f, -370.0f, 22.0f, 5.0f, 100.0f);
    poo_poo_island_add_box(-560.0f, marina_h + 7.0f, -250.0f, 60.0f, 14.0f, 48.0f);

    poo_poo_island_add_box(-760.0f, beach_h + 0.9f, 310.0f, 210.0f, 1.8f, 130.0f);
    poo_poo_island_add_box(-850.0f, beach_h + 2.4f, 260.0f, 42.0f, 4.8f, 42.0f);
    poo_poo_island_add_box(-640.0f, beach_h + 2.8f, 350.0f, 44.0f, 5.6f, 44.0f);
    poo_poo_island_add_box(-530.0f, beach_h + 2.0f, 285.0f, 72.0f, 4.0f, 18.0f);

    poo_poo_island_add_box(760.0f, lighthouse_h + 22.0f, -520.0f, 30.0f, 44.0f, 30.0f);
    poo_poo_island_add_box(760.0f, lighthouse_h + 52.0f, -520.0f, 20.0f, 16.0f, 20.0f);
    poo_poo_island_add_box(760.0f, lighthouse_h + 60.0f, -520.0f, 28.0f, 4.0f, 28.0f);
    poo_poo_island_add_box(700.0f, lighthouse_h + 8.0f, -450.0f, 84.0f, 16.0f, 24.0f);

    poo_poo_island_add_box(640.0f, volcano_h + 35.0f, 560.0f, 170.0f, 70.0f, 170.0f);
    poo_poo_island_add_box(640.0f, volcano_h + 76.0f, 560.0f, 80.0f, 12.0f, 80.0f);
    poo_poo_island_add_box(640.0f, volcano_h + 82.0f, 560.0f, 42.0f, 4.0f, 42.0f);

    poo_poo_island_add_box(-80.0f, poo_poo_island_height_at(-80.0f, 80.0f) + 6.0f, 80.0f, 130.0f, 6.0f, 24.0f);
    poo_poo_island_add_box(55.0f, poo_poo_island_height_at(55.0f, 140.0f) + 7.0f, 140.0f, 26.0f, 14.0f, 130.0f);
    poo_poo_island_add_box(560.0f, poo_poo_island_height_at(560.0f, 120.0f) + 8.0f, 120.0f, 180.0f, 16.0f, 24.0f);

    printf("[POO_POO_ISLAND] authored geo boxes=%d landmarks=%d routes=%d scenic=%d\n",
           map_geo_poo_poo_island_count,
           (int)(sizeof(poo_poo_island_landmark_anchors) / sizeof(VoxRouteAnchor)),
           (int)(sizeof(poo_poo_island_route_anchors) / sizeof(VoxRouteAnchor)),
           (int)(sizeof(poo_poo_island_scenic_anchors) / sizeof(VoxRouteAnchor)));
}

static int phys_scene_id = SCENE_STADIUM;
static TerrainHeightfield g_scene_terrain = {0};
static int g_last_ground_source_terrain = 0;
static int g_scene_terrain_scene_id = -1;
static inline void init_voxworld_bloodgulch_terrain(void);
static inline void init_dust_compound_terrain(void);
static inline void init_dust_compound_geo(void);
static inline void init_oil_tanker_geo(void);
static inline void voxworld_build_bushes(void);

static inline float voxworld_bush_hash01(int x, int z, int salt) {
    uint32_t h = (uint32_t)x * 374761393u ^ (uint32_t)z * 668265263u ^ (uint32_t)salt * 362437u;
    h ^= h >> 13;
    h *= 1274126177u;
    h ^= h >> 16;
    return (float)(h & 0x00FFFFFFu) / 16777215.0f;
}

static inline float voxworld_bush_dist2(float x0, float z0, float x1, float z1) {
    float dx = x0 - x1;
    float dz = z0 - z1;
    return dx * dx + dz * dz;
}

static inline int voxworld_point_is_near_reserved_area(float x, float z) {
    const float base_clear_r = 310.0f;
    if (voxworld_bush_dist2(x, z, VOXWORLD_BASE_RED_X, VOXWORLD_BASE_Z) < base_clear_r * base_clear_r) return 1;
    if (voxworld_bush_dist2(x, z, VOXWORLD_BASE_BLUE_X, VOXWORLD_BASE_Z) < base_clear_r * base_clear_r) return 1;
    if (voxworld_bush_dist2(x, z, VOXWORLD_HELI_RED_X, VOXWORLD_HELI_RED_Z) < 140.0f * 140.0f) return 1;
    if (voxworld_bush_dist2(x, z, VOXWORLD_HELI_BLUE_X, VOXWORLD_HELI_BLUE_Z) < 140.0f * 140.0f) return 1;

    if (voxworld_bush_dist2(x, z, voxworld_flag_home_red.x, voxworld_flag_home_red.y) < 170.0f * 170.0f) return 1;
    if (voxworld_bush_dist2(x, z, voxworld_flag_home_blue.x, voxworld_flag_home_blue.y) < 170.0f * 170.0f) return 1;

    for (int i = 0; i < (int)(sizeof(voxworld_vehicle_pads) / sizeof(voxworld_vehicle_pads[0])); i++) {
        if (voxworld_bush_dist2(x, z, voxworld_vehicle_pads[i].x, voxworld_vehicle_pads[i].z) < 95.0f * 95.0f) return 1;
    }
    for (int i = 0; i < (int)(sizeof(voxworld_spawn_points_red) / sizeof(voxworld_spawn_points_red[0])); i++) {
        if (voxworld_bush_dist2(x, z, voxworld_spawn_points_red[i].x, voxworld_spawn_points_red[i].y) < 80.0f * 80.0f) return 1;
    }
    for (int i = 0; i < (int)(sizeof(voxworld_spawn_points_blue) / sizeof(voxworld_spawn_points_blue[0])); i++) {
        if (voxworld_bush_dist2(x, z, voxworld_spawn_points_blue[i].x, voxworld_spawn_points_blue[i].y) < 80.0f * 80.0f) return 1;
    }
    for (int i = 0; i < (int)(sizeof(voxworld_spawn_points_ffa) / sizeof(voxworld_spawn_points_ffa[0])); i++) {
        if (voxworld_bush_dist2(x, z, voxworld_spawn_points_ffa[i].x, voxworld_spawn_points_ffa[i].y) < 62.0f * 62.0f) return 1;
    }
    for (int i = 0; i < (int)(sizeof(voxworld_teleporters) / sizeof(voxworld_teleporters[0])); i++) {
        if (voxworld_bush_dist2(x, z, voxworld_teleporters[i].x, voxworld_teleporters[i].y) < 120.0f * 120.0f) return 1;
    }
    for (int i = 0; i < (int)(sizeof(voxworld_teleport_destinations) / sizeof(voxworld_teleport_destinations[0])); i++) {
        if (voxworld_bush_dist2(x, z, voxworld_teleport_destinations[i].x, voxworld_teleport_destinations[i].y) < 130.0f * 130.0f) return 1;
    }
    if (fabsf(z) < 96.0f && fabsf(x) < 900.0f) return 1;
    return 0;
}

static inline int voxworld_point_is_good_for_bush(TerrainHeightfield *t, float x, float z) {
    if (!t || !t->heights || !terrain_contains_world(t, x, z)) return 0;
    if (voxworld_point_is_near_reserved_area(x, z)) return 0;
    float nx = 0.0f, ny = 1.0f, nz = 0.0f;
    terrain_sample_normal(t, x, z, &nx, &ny, &nz);
    if (ny < 0.66f) return 0;
    if (ny > 0.998f && fabsf(z) < 130.0f) return 0;

    float y = terrain_sample_height(t, x, z);
    for (int i = 0; i < map_geo_voxworld_count; i++) {
        const Box *b = &map_geo_voxworld[i];
        float pad_w = b->w * 0.54f + 5.0f;
        float pad_d = b->d * 0.54f + 5.0f;
        if (fabsf(x - b->x) <= pad_w && fabsf(z - b->z) <= pad_d) {
            float top = b->y + b->h * 0.5f;
            if (y <= top + 8.0f) return 0;
        }
    }
    return 1;
}

static inline void voxworld_clear_bushes(void) {
    g_voxworld_bush_count = 0;
}

static inline void voxworld_build_bushes(void) {
    TerrainHeightfield *t = &g_scene_terrain;
    voxworld_clear_bushes();
    if (!t || !t->heights) return;

    const float min_x = -VOXWORLD_HALF_LENGTH + 110.0f;
    const float max_x = VOXWORLD_HALF_LENGTH - 110.0f;
    const float min_z = -VOXWORLD_HALF_WIDTH + 85.0f;
    const float max_z = VOXWORLD_HALF_WIDTH - 85.0f;
    const float spacing = 98.0f;
    const float target = 170.0f * VOXWORLD_BUSH_DENSITY;

    int gxmax = (int)((max_x - min_x) / spacing);
    int gzmax = (int)((max_z - min_z) / spacing);
    for (int gz = 0; gz <= gzmax; gz++) {
        for (int gx = 0; gx <= gxmax; gx++) {
            if (g_voxworld_bush_count >= MAX_VOXWORLD_BUSHES) break;

            int key_x = gx - 241;
            int key_z = gz + 619;
            float jx = (voxworld_bush_hash01(key_x, key_z, 11) - 0.5f) * spacing * 0.72f;
            float jz = (voxworld_bush_hash01(key_x, key_z, 29) - 0.5f) * spacing * 0.72f;
            float x = min_x + (float)gx * spacing + jx;
            float z = min_z + (float)gz * spacing + jz;

            if (!voxworld_point_is_good_for_bush(t, x, z)) continue;
            float nx = 0.0f, ny = 1.0f, nz = 0.0f;
            terrain_sample_normal(t, x, z, &nx, &ny, &nz);
            float side_pref = fminf(1.0f, fmaxf(0.0f, (fabsf(z) - 180.0f) / 420.0f));
            float slope_pref = fminf(1.0f, fmaxf(0.0f, (0.95f - ny) / 0.3f));
            float flank_pref = fminf(1.0f, fmaxf(0.0f, (fabsf(x) - 360.0f) / 900.0f));
            float route_avoid = fminf(1.0f, fmaxf(0.0f, (fabsf(z) - 140.0f) / 360.0f));
            float placement_score = 0.20f + side_pref * 0.34f + slope_pref * 0.24f + flank_pref * 0.20f + route_avoid * 0.16f;
            placement_score += fmaxf(0.0f, -vox_hash_noise(x * 0.9f, z * 0.9f)) * 0.12f;
            float roll = voxworld_bush_hash01(key_x, key_z, 47);
            float target_factor = target / 170.0f;
            if (roll > placement_score * target_factor) continue;

            BushProp *b = &g_voxworld_bushes[g_voxworld_bush_count++];
            b->x = x;
            b->z = z;
            b->y = terrain_sample_height(t, x, z) + 0.15f;
            b->scale = (0.80f + voxworld_bush_hash01(key_x, key_z, 59) * 0.62f) * (0.88f + side_pref * 0.22f);
            b->yaw = voxworld_bush_hash01(key_x, key_z, 83) * 360.0f;
            b->tint = voxworld_bush_hash01(key_x, key_z, 101);
        }
    }
    g_voxworld_bushes_ready = 1;
    printf("[VOXWORLD] bushes built count=%d density=%.2f\n", g_voxworld_bush_count, VOXWORLD_BUSH_DENSITY);
}

static inline void init_poo_poo_island_terrain(void);
static inline void init_stadium_terrain(void);

static inline void phys_set_scene(int scene_id) {
    phys_scene_id = scene_id;
    if (scene_id == SCENE_GARAGE_OSAKA) {
        map_geo = map_geo_garage;
        map_count = (int)(sizeof(map_geo_garage) / sizeof(Box));
        g_scene_terrain.active = 0;
    } else if (scene_id == SCENE_DUST_COMPOUND) {
        init_dust_compound_terrain();
        init_dust_compound_geo();
        map_geo = map_geo_dust;
        map_count = map_geo_dust_count;
        g_scene_terrain.active = (g_scene_terrain.heights != NULL);
    } else if (scene_id == SCENE_OIL_TANKER) {
        init_oil_tanker_geo();
        map_geo = map_geo_tanker;
        map_count = map_geo_tanker_count;
        g_scene_terrain.active = 0;
    } else if (scene_id == SCENE_POO_POO_ISLAND) {
        init_poo_poo_island_terrain();
        init_poo_poo_island_geo();
        map_geo = map_geo_poo_poo_island;
        map_count = map_geo_poo_poo_island_count;
        g_scene_terrain.active = (g_scene_terrain.heights != NULL);
    } else if (scene_id == SCENE_VOXWORLD) {
        init_voxworld_bloodgulch_terrain();
        init_voxworld_bloodgulch_geo();
        if (!g_voxworld_bushes_ready) voxworld_build_bushes();
        map_geo = map_geo_voxworld;
        map_count = map_geo_voxworld_count;
        g_scene_terrain.active = (g_scene_terrain.heights != NULL);
    } else if (scene_id == SCENE_STADIUM) {
        init_stadium_terrain();
        map_geo = map_geo_stadium;
        map_count = (int)(sizeof(map_geo_stadium) / sizeof(Box));
        g_scene_terrain.active = (g_scene_terrain.heights != NULL);
    } else if (scene_id == SCENE_STORY_CAVE) {
        map_geo = map_geo_story_cave;
        map_count = (int)(sizeof(map_geo_story_cave) / sizeof(Box));
        g_scene_terrain.active = 0;
    } else {
        map_geo = map_geo_stadium;
        map_count = (int)(sizeof(map_geo_stadium) / sizeof(Box));
        g_scene_terrain.active = 0;
    }
}

static inline TerrainHeightfield* scene_active_terrain(void) {
    return g_scene_terrain.active ? &g_scene_terrain : NULL;
}

static inline const BushProp *voxworld_get_bushes(int *out_count) {
    if (out_count) *out_count = g_voxworld_bush_count;
    return g_voxworld_bushes;
}

static inline int phys_last_grounded_on_terrain(void) {
    return g_last_ground_source_terrain;
}

static inline void scene_set_game_mode(int mode) {
    g_phys_game_mode = mode;
}

static inline void init_voxworld_bloodgulch_terrain(void) {
    if (g_scene_terrain_scene_id == SCENE_VOXWORLD && g_scene_terrain.heights) {
        g_scene_terrain.active = 1;
        return;
    }
    if (g_scene_terrain.heights) terrain_free(&g_scene_terrain);
    if (!terrain_init(&g_scene_terrain, VOXWORLD_TERRAIN_W, VOXWORLD_TERRAIN_H, VOXWORLD_CELL, VOXWORLD_ORIGIN_X, VOXWORLD_ORIGIN_Z)) return;
    terrain_clear(&g_scene_terrain, 0.0f);
    g_voxworld_bushes_ready = 0;
    g_voxworld_bush_count = 0;

    for (int gz = 0; gz < g_scene_terrain.height; gz++) {
        for (int gx = 0; gx < g_scene_terrain.width; gx++) {
            float wx = g_scene_terrain.origin_x + gx * g_scene_terrain.cell_size;
            float wz = g_scene_terrain.origin_z + gz * g_scene_terrain.cell_size;

            float cross = fabsf(wz) / VOXWORLD_HALF_WIDTH;
            if (cross > 1.0f) cross = 1.0f;
            float side = 8.0f + 108.0f * cross * cross;
            float lane = -10.0f + 4.0f * sinf(wx * 0.0031f);

            float end_t = fabsf(wx) / VOXWORLD_HALF_LENGTH;
            if (end_t > 1.0f) end_t = 1.0f;
            float end_raise = 7.0f * end_t * end_t;

            float h = side + lane + end_raise;

            float center_lumps = 0.0f;
            center_lumps += 7.5f * expf(-((wx + 260.0f) * (wx + 260.0f)) / (2.0f * 180.0f * 180.0f)) * expf(-(wz * wz) / (2.0f * 140.0f * 140.0f));
            center_lumps -= 6.0f * expf(-((wx - 120.0f) * (wx - 120.0f)) / (2.0f * 150.0f * 150.0f)) * expf(-((wz + 80.0f) * (wz + 80.0f)) / (2.0f * 110.0f * 110.0f));
            center_lumps += 6.5f * expf(-((wx - 320.0f) * (wx - 320.0f)) / (2.0f * 170.0f * 170.0f)) * expf(-((wz - 120.0f) * (wz - 120.0f)) / (2.0f * 120.0f * 120.0f));
            h += center_lumps;

            if (wz < -340.0f && fabsf(wx) < 1180.0f) {
                float cave_t = (fabsf(wz + 490.0f) / 170.0f);
                if (cave_t < 1.0f) {
                    float cave_cut = (1.0f - cave_t);
                    h -= 26.0f * cave_cut * cave_cut;
                }
            }
            if (wz > 310.0f && fabsf(wx) < 1220.0f) {
                float shelf_t = fabsf(wz - 480.0f) / 190.0f;
                if (shelf_t < 1.0f) {
                    float shelf = 1.0f - shelf_t;
                    h += 20.0f * shelf * shelf;
                }
            }
            if (fabsf(wx - VOXWORLD_BASE_RED_X) < 260.0f || fabsf(wx - VOXWORLD_BASE_BLUE_X) < 260.0f) {
                float base = 12.0f * expf(-(wz * wz) / (2.0f * 240.0f * 240.0f));
                h += base;
            }

            h += vox_hash_noise(wx, wz) * 1.6f;
            terrain_set_height(&g_scene_terrain, gx, gz, h);
        }
    }

    vox_terrain_stamp(&g_scene_terrain, VOXWORLD_BASE_RED_X, 0.0f, 220.0f, 20.0f, 1.0f);
    vox_terrain_stamp(&g_scene_terrain, VOXWORLD_BASE_BLUE_X, 0.0f, 220.0f, 20.0f, 1.0f);
    vox_terrain_stamp(&g_scene_terrain, VOXWORLD_BASE_RED_X + 130.0f, 0.0f, 140.0f, 14.0f, 1.0f);
    vox_terrain_stamp(&g_scene_terrain, VOXWORLD_BASE_BLUE_X - 130.0f, 0.0f, 140.0f, 14.0f, 1.0f);
    vox_terrain_stamp(&g_scene_terrain, -260.0f, -500.0f, 130.0f, -6.0f, 0.75f);
    vox_terrain_stamp(&g_scene_terrain, 260.0f, -500.0f, 130.0f, -6.0f, 0.75f);
    vox_terrain_stamp(&g_scene_terrain, -260.0f, 480.0f, 140.0f, 28.0f, 0.75f);
    vox_terrain_stamp(&g_scene_terrain, 260.0f, 480.0f, 140.0f, 28.0f, 0.75f);
    vox_terrain_smooth(&g_scene_terrain, 2, 0.42f);

    printf("[VOXWORLD] terrain initialized %dx%d cell=%.1f origin=(%.1f, %.1f)\n",
           g_scene_terrain.width, g_scene_terrain.height, g_scene_terrain.cell_size,
           g_scene_terrain.origin_x, g_scene_terrain.origin_z);
    g_scene_terrain_scene_id = SCENE_VOXWORLD;
}

static inline void init_dust_compound_terrain(void) {
    if (g_scene_terrain_scene_id == SCENE_DUST_COMPOUND && g_scene_terrain.heights) {
        g_scene_terrain.active = 1;
        return;
    }
    if (g_scene_terrain.heights) terrain_free(&g_scene_terrain);
    if (!terrain_init(&g_scene_terrain, DUST_TERRAIN_W, DUST_TERRAIN_H, DUST_CELL, DUST_ORIGIN_X, DUST_ORIGIN_Z)) return;
    terrain_clear(&g_scene_terrain, 2.0f);
    for (int gz = 0; gz < g_scene_terrain.height; gz++) {
        for (int gx = 0; gx < g_scene_terrain.width; gx++) {
            float wx = g_scene_terrain.origin_x + gx * g_scene_terrain.cell_size;
            float wz = g_scene_terrain.origin_z + gz * g_scene_terrain.cell_size;
            float h = 5.0f;
            h += 2.2f * sinf(wx * 0.0065f) + 1.8f * cosf(wz * 0.0072f);
            h += 0.7f * sinf((wx + wz) * 0.015f);
            h += 0.018f * wx;
            float mid_shape = expf(-(wz * wz) / (2.0f * 190.0f * 190.0f));
            h += 4.5f * mid_shape;
            float underpass = expf(-((wx - 5.0f) * (wx - 5.0f)) / (2.0f * 120.0f * 120.0f)) * expf(-((wz + 170.0f) * (wz + 170.0f)) / (2.0f * 120.0f * 120.0f));
            h -= 14.0f * underpass;
            float a_terrace = expf(-((wx - DUST_A_SITE_X) * (wx - DUST_A_SITE_X)) / (2.0f * 180.0f * 180.0f)) * expf(-((wz - DUST_A_SITE_Z) * (wz - DUST_A_SITE_Z)) / (2.0f * 180.0f * 180.0f));
            float b_terrace = expf(-((wx - DUST_B_SITE_X) * (wx - DUST_B_SITE_X)) / (2.0f * 170.0f * 170.0f)) * expf(-((wz - DUST_B_SITE_Z) * (wz - DUST_B_SITE_Z)) / (2.0f * 170.0f * 170.0f));
            h += 7.0f * a_terrace;
            h += 6.0f * b_terrace;
            terrain_set_height(&g_scene_terrain, gx, gz, h);
        }
    }
    vox_terrain_stamp(&g_scene_terrain, DUST_ATTACK_SPAWN_X, DUST_ATTACK_SPAWN_Z, 120.0f, 4.0f, 1.0f);
    vox_terrain_stamp(&g_scene_terrain, DUST_DEFEND_SPAWN_X, DUST_DEFEND_SPAWN_Z, 120.0f, 14.0f, 1.0f);
    vox_terrain_stamp(&g_scene_terrain, DUST_MID_X, DUST_MID_Z, 220.0f, 8.0f, 0.7f);
    vox_terrain_stamp(&g_scene_terrain, DUST_UNDERPASS_X, DUST_UNDERPASS_Z, 130.0f, -9.0f, 1.0f);
    vox_terrain_smooth(&g_scene_terrain, 2, 0.5f);
    printf("[DUST] terrain initialized %dx%d cell=%.1f origin=(%.1f, %.1f)\n",
           g_scene_terrain.width, g_scene_terrain.height, g_scene_terrain.cell_size,
           g_scene_terrain.origin_x, g_scene_terrain.origin_z);
    g_scene_terrain_scene_id = SCENE_DUST_COMPOUND;
}

static inline void init_stadium_terrain(void) {
    if (g_scene_terrain_scene_id == SCENE_STADIUM && g_scene_terrain.heights) {
        g_scene_terrain.active = 1;
        return;
    }
    if (g_scene_terrain.heights) terrain_free(&g_scene_terrain);
    if (!terrain_init(&g_scene_terrain, STADIUM_TERRAIN_W, STADIUM_TERRAIN_H, STADIUM_CELL, STADIUM_ORIGIN_X, STADIUM_ORIGIN_Z)) return;
    terrain_clear(&g_scene_terrain, 10.0f);

    for (int gz = 0; gz < g_scene_terrain.height; gz++) {
        for (int gx = 0; gx < g_scene_terrain.width; gx++) {
            float wx = g_scene_terrain.origin_x + gx * g_scene_terrain.cell_size;
            float wz = g_scene_terrain.origin_z + gz * g_scene_terrain.cell_size;

            float r = sqrtf(wx * wx + wz * wz);
            float ring = fminf(1.0f, fmaxf(0.0f, (r - 320.0f) / 600.0f));
            float h = 7.5f + ring * 20.0f;

            h += sinf(wx * 0.0052f) * 2.3f;
            h += cosf(wz * 0.0059f) * 2.1f;
            h += sinf((wx - wz) * 0.0083f) * 1.4f;

            float north_ridge = expf(-((wx + 140.0f) * (wx + 140.0f)) / (2.0f * 360.0f * 360.0f))
                              * expf(-((wz - 580.0f) * (wz - 580.0f)) / (2.0f * 210.0f * 210.0f));
            float west_bowl = expf(-((wx + 650.0f) * (wx + 650.0f)) / (2.0f * 260.0f * 260.0f))
                            * expf(-((wz + 40.0f) * (wz + 40.0f)) / (2.0f * 340.0f * 340.0f));
            float southeast_berm = expf(-((wx - 550.0f) * (wx - 550.0f)) / (2.0f * 300.0f * 300.0f))
                                 * expf(-((wz + 520.0f) * (wz + 520.0f)) / (2.0f * 250.0f * 250.0f));
            h += north_ridge * 22.0f;
            h -= west_bowl * 12.0f;
            h += southeast_berm * 14.0f;

            float core_flat = expf(-(wx * wx) / (2.0f * 300.0f * 300.0f)) * expf(-(wz * wz) / (2.0f * 300.0f * 300.0f));
            h = h * (1.0f - core_flat) + 3.0f * core_flat;

            float road = stadium_track_weight_at(wx, wz);
            if (road > 0.0f) {
                float road_profile = 5.5f + 1.4f * sinf(wx * 0.004f + wz * 0.003f);
                h = h * (1.0f - road * 0.88f) + road_profile * road * 0.88f;
                float d = stadium_road_distance(wx, wz);
                float shoulder = 1.0f - fminf(1.0f, fabsf(d - 64.0f) / 28.0f);
                if (shoulder > 0.0f) h += shoulder * 3.4f;
            }

            h += vox_hash_noise(wx * 0.7f, wz * 0.7f) * 1.0f;
            terrain_set_height(&g_scene_terrain, gx, gz, h);
        }
    }

    /* Keep the recognizable stadium center flat while raising selected outer sections for rally flow. */
    vox_terrain_stamp(&g_scene_terrain, 0.0f, 0.0f, 325.0f, 3.0f, 1.0f);
    vox_terrain_stamp(&g_scene_terrain, -120.0f, 620.0f, 210.0f, 36.0f, 0.8f);
    vox_terrain_stamp(&g_scene_terrain, 580.0f, -520.0f, 240.0f, 26.0f, 0.7f);
    vox_terrain_stamp(&g_scene_terrain, -680.0f, 220.0f, 220.0f, 18.0f, 0.7f);
    vox_terrain_smooth(&g_scene_terrain, 3, 0.44f);

    printf("[STADIUM] terrain initialized %dx%d cell=%.1f origin=(%.1f, %.1f)\n",
           g_scene_terrain.width, g_scene_terrain.height, g_scene_terrain.cell_size,
           g_scene_terrain.origin_x, g_scene_terrain.origin_z);
    g_scene_terrain_scene_id = SCENE_STADIUM;
}

static inline void init_poo_poo_island_terrain(void) {
    if (g_scene_terrain_scene_id == SCENE_POO_POO_ISLAND && g_scene_terrain.heights) {
        g_scene_terrain.active = 1;
        return;
    }
    if (g_scene_terrain.heights) terrain_free(&g_scene_terrain);
    if (!terrain_init(&g_scene_terrain, POO_POO_ISLAND_TERRAIN_W, POO_POO_ISLAND_TERRAIN_H,
                      POO_POO_ISLAND_CELL, POO_POO_ISLAND_ORIGIN_X, POO_POO_ISLAND_ORIGIN_Z)) return;
    terrain_clear(&g_scene_terrain, -8.0f);
    for (int gz = 0; gz < g_scene_terrain.height; gz++) {
        for (int gx = 0; gx < g_scene_terrain.width; gx++) {
            float wx = g_scene_terrain.origin_x + gx * g_scene_terrain.cell_size;
            float wz = g_scene_terrain.origin_z + gz * g_scene_terrain.cell_size;
            float radius_n = sqrtf(wx * wx + wz * wz) / 1180.0f;
            float island_mask = 1.0f - fminf(1.0f, radius_n);
            island_mask = island_mask * island_mask * (3.0f - 2.0f * island_mask);
            float h = -22.0f + island_mask * 38.0f;
            h += sinf(wx * 0.0055f + wz * 0.0023f) * 2.0f;
            h += cosf(wz * 0.0047f - wx * 0.0017f) * 1.7f;
            h += sinf((wx + wz) * 0.008f) * 1.1f;
            float town = expf(-((wx - POO_POO_HUB_X) * (wx - POO_POO_HUB_X)) / (2.0f * 240.0f * 240.0f))
                       * expf(-((wz - POO_POO_HUB_Z) * (wz - POO_POO_HUB_Z)) / (2.0f * 220.0f * 220.0f));
            float marina = expf(-((wx - POO_POO_MARINA_X) * (wx - POO_POO_MARINA_X)) / (2.0f * 180.0f * 180.0f))
                         * expf(-((wz - POO_POO_MARINA_Z) * (wz - POO_POO_MARINA_Z)) / (2.0f * 160.0f * 160.0f));
            float beach = expf(-((wx - POO_POO_BEACH_X) * (wx - POO_POO_BEACH_X)) / (2.0f * 250.0f * 250.0f))
                        * expf(-((wz - POO_POO_BEACH_Z) * (wz - POO_POO_BEACH_Z)) / (2.0f * 190.0f * 190.0f));
            float volcano = expf(-((wx - POO_POO_VOLCANO_X) * (wx - POO_POO_VOLCANO_X)) / (2.0f * 260.0f * 260.0f))
                          * expf(-((wz - POO_POO_VOLCANO_Z) * (wz - POO_POO_VOLCANO_Z)) / (2.0f * 250.0f * 250.0f));
            float lighthouse = expf(-((wx - POO_POO_LIGHTHOUSE_X) * (wx - POO_POO_LIGHTHOUSE_X)) / (2.0f * 180.0f * 180.0f))
                             * expf(-((wz - POO_POO_LIGHTHOUSE_Z) * (wz - POO_POO_LIGHTHOUSE_Z)) / (2.0f * 180.0f * 180.0f));
            h += town * 15.0f + marina * 6.0f;
            h -= beach * 9.0f;
            h += lighthouse * 11.0f;
            h += volcano * 66.0f;
            h += island_mask * vox_hash_noise(wx * 0.8f, wz * 0.8f) * 1.5f;
            terrain_set_height(&g_scene_terrain, gx, gz, h);
        }
    }
    vox_terrain_stamp(&g_scene_terrain, POO_POO_HUB_X, POO_POO_HUB_Z, 250.0f, 18.0f, 1.0f);
    vox_terrain_stamp(&g_scene_terrain, POO_POO_MARINA_X, POO_POO_MARINA_Z, 180.0f, 9.0f, 1.0f);
    vox_terrain_stamp(&g_scene_terrain, POO_POO_BEACH_X, POO_POO_BEACH_Z, 260.0f, 2.0f, 1.0f);
    vox_terrain_stamp(&g_scene_terrain, -60.0f, 100.0f, 240.0f, 24.0f, 0.7f);
    vox_terrain_stamp(&g_scene_terrain, 320.0f, 250.0f, 260.0f, 34.0f, 0.8f);
    vox_terrain_stamp(&g_scene_terrain, POO_POO_VOLCANO_X, POO_POO_VOLCANO_Z, 280.0f, 68.0f, 1.0f);
    vox_terrain_stamp(&g_scene_terrain, POO_POO_VOLCANO_X, POO_POO_VOLCANO_Z, 120.0f, 86.0f, 1.0f);
    vox_terrain_stamp(&g_scene_terrain, POO_POO_LIGHTHOUSE_X, POO_POO_LIGHTHOUSE_Z, 140.0f, 28.0f, 1.0f);
    vox_terrain_stamp(&g_scene_terrain, 710.0f, -410.0f, 120.0f, 22.0f, 0.9f);
    vox_terrain_smooth(&g_scene_terrain, 3, 0.46f);
    printf("[POO_POO_ISLAND] terrain initialized %dx%d cell=%.1f origin=(%.1f, %.1f)\n",
           g_scene_terrain.width, g_scene_terrain.height, g_scene_terrain.cell_size,
           g_scene_terrain.origin_x, g_scene_terrain.origin_z);
    g_scene_terrain_scene_id = SCENE_POO_POO_ISLAND;
}

static inline int scene_get_team_base_marker(int scene_id, int team_id,
                                             float *x, float *y, float *z,
                                             float *sx, float *sy, float *sz) {
    if (scene_id == SCENE_VOXWORLD) {
        *x = (team_id == 0) ? VOXWORLD_BASE_RED_X : VOXWORLD_BASE_BLUE_X;
        *z = VOXWORLD_BASE_Z;
        *y = voxworld_height_at(*x, *z) + 9.0f;
        *sx = 22.0f; *sy = 8.0f; *sz = 22.0f;
        return 1;
    }
    if (scene_id == SCENE_DUST_COMPOUND) {
        *x = (team_id == 0) ? -430.0f : 430.0f;
        *z = (team_id == 0) ? -220.0f : 220.0f;
        *y = dust_height_at(*x, *z) + 8.0f;
        *sx = 24.0f; *sy = 14.0f; *sz = 24.0f;
        return 1;
    }
    if (scene_id == SCENE_OIL_TANKER) {
        *x = (team_id == 0) ? -270.0f : 270.0f;
        *z = 0.0f;
        *y = 9.0f;
        *sx = 26.0f; *sy = 12.0f; *sz = 22.0f;
        return 1;
    }
    if (scene_id == SCENE_POO_POO_ISLAND) {
        *x = (team_id == 0) ? -540.0f : 620.0f;
        *z = (team_id == 0) ? -220.0f : 460.0f;
        *y = poo_poo_island_height_at(*x, *z) + 9.0f;
        *sx = 26.0f; *sy = 14.0f; *sz = 26.0f;
        return 1;
    }
    if (scene_id == SCENE_STADIUM) {
        *x = (team_id == 0) ? -310.0f : 310.0f;
        *z = 0.0f;
        *y = stadium_height_at(*x, *z) + 8.0f;
        *sx = 24.0f; *sy = 12.0f; *sz = 24.0f;
        return 1;
    }
    return 0;
}

static inline int get_ctf_pedestal_anchor(int scene_id, int team_id,
                                          float *center_x, float *center_y, float *center_z,
                                          float *size_x, float *size_y, float *size_z,
                                          float *top_y,
                                          float *flag_x, float *flag_y, float *flag_z) {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float sx = 0.0f, sy = 0.0f, sz = 0.0f;

    if (scene_id == SCENE_OIL_TANKER) {
        x = (team_id == 0) ? -270.0f : 270.0f;
        z = 0.0f;
        sx = 18.0f; sy = 6.0f; sz = 18.0f;
        y = 7.0f;
    } else if (!scene_get_team_base_marker(scene_id, team_id, &x, &y, &z, &sx, &sy, &sz)) {
        return 0;
    } else {
        sx = fmaxf(14.0f, sx * 0.5f);
        sy = fmaxf(6.0f, sy * 0.5f);
        sz = fmaxf(14.0f, sz * 0.5f);
    }

    float pedestal_top_y = y + sy * 0.5f;
    float home_flag_x = x;
    float home_flag_y = pedestal_top_y + 6.0f;
    float home_flag_z = z;

    if (center_x) *center_x = x;
    if (center_y) *center_y = y;
    if (center_z) *center_z = z;
    if (size_x) *size_x = sx;
    if (size_y) *size_y = sy;
    if (size_z) *size_z = sz;
    if (top_y) *top_y = pedestal_top_y;
    if (flag_x) *flag_x = home_flag_x;
    if (flag_y) *flag_y = home_flag_y;
    if (flag_z) *flag_z = home_flag_z;
    return 1;
}

static inline int get_ctf_flag_home(int scene_id, int team_id, float *x, float *y, float *z) {
    if (get_ctf_pedestal_anchor(scene_id, team_id,
                                NULL, NULL, NULL,
                                NULL, NULL, NULL,
                                NULL,
                                x, y, z)) return 1;
    if (scene_id == SCENE_VOXWORLD) {
        *x = (team_id == 0) ? voxworld_flag_home_red.x : voxworld_flag_home_blue.x;
        *z = (team_id == 0) ? voxworld_flag_home_red.y : voxworld_flag_home_blue.y;
        *y = voxworld_height_at(*x, *z) + 8.0f;
        return 1;
    }
    float sx = 0.0f, sy = 0.0f, sz = 0.0f;
    if (scene_get_team_base_marker(scene_id, team_id, x, y, z, &sx, &sy, &sz)) return 1;
    return 0;
}

static inline int get_ctf_capture_zone(int scene_id, int team_id, float *x, float *y, float *z, float *radius) {
    if (!get_ctf_flag_home(scene_id, team_id, x, y, z)) return 0;
    if (radius) *radius = (scene_id == SCENE_OIL_TANKER) ? 42.0f : 46.0f;
    return 1;
}

static inline void scene_spawn_point(int scene_id, int slot, float *out_x, float *out_y, float *out_z) {
    if (scene_id == SCENE_GARAGE_OSAKA) {
        float offsets[] = {-20.0f, 0.0f, 20.0f, -10.0f, 10.0f};
        int idx = slot % 5;
        *out_x = offsets[idx];
        *out_y = 2.0f;
        *out_z = 20.0f;
        return;
    }
    if (scene_id == SCENE_VOXWORLD) {
        const Vec2 *pts = voxworld_spawn_points_ffa;
        int count = (int)(sizeof(voxworld_spawn_points_ffa) / sizeof(Vec2));
        int idx = slot % count;
        *out_x = pts[idx].x;
        *out_z = pts[idx].y;
        *out_y = voxworld_height_at(*out_x, *out_z) + 6.0f;
        return;
    }
    if (scene_id == SCENE_DUST_COMPOUND) {
        int count = (int)(sizeof(dust_spawn_points_dm) / sizeof(Vec2));
        int idx = slot % count;
        *out_x = dust_spawn_points_dm[idx].x;
        *out_z = dust_spawn_points_dm[idx].y;
        *out_y = dust_height_at(*out_x, *out_z) + 5.5f;
        return;
    }
    if (scene_id == SCENE_OIL_TANKER) {
        int count = (int)(sizeof(tanker_spawn_points_dm) / sizeof(Vec2));
        int idx = slot % count;
        *out_x = tanker_spawn_points_dm[idx].x;
        *out_z = tanker_spawn_points_dm[idx].y;
        *out_y = 6.0f;
        return;
    }
    if (scene_id == SCENE_POO_POO_ISLAND) {
        int count = (int)(sizeof(poo_poo_island_spawn_points_dm) / sizeof(Vec2));
        int idx = slot % count;
        *out_x = poo_poo_island_spawn_points_dm[idx].x;
        *out_z = poo_poo_island_spawn_points_dm[idx].y;
        *out_y = poo_poo_island_height_at(*out_x, *out_z) + 6.0f;
        return;
    }
    if (scene_id == SCENE_STADIUM) {
        int count = (int)(sizeof(stadium_spawn_points_dm) / sizeof(Vec2));
        int idx = slot % count;
        *out_x = stadium_spawn_points_dm[idx].x;
        *out_z = stadium_spawn_points_dm[idx].y;
        *out_y = stadium_height_at(*out_x, *out_z) + 6.0f;
        return;
    }
    if (scene_id == SCENE_STORY_CAVE) {
        *out_x = 0.0f;
        *out_y = 4.0f;
        *out_z = -720.0f;
        return;
    }
    if (slot % 2 == 0) {
        *out_x = 0.0f; *out_z = 0.0f; *out_y = 80.0f;
    } else {
        float ang = phys_rand_f() * 6.28f;
        *out_x = sinf(ang) * 500.0f;
        *out_z = cosf(ang) * 500.0f;
        *out_y = 20.0f;
    }
}

static inline void scene_spawn_for_player(PlayerState *p, float *out_x, float *out_y, float *out_z) {
    if (p->scene_id != SCENE_VOXWORLD && p->scene_id != SCENE_DUST_COMPOUND &&
        p->scene_id != SCENE_OIL_TANKER && p->scene_id != SCENE_STADIUM &&
        p->scene_id != SCENE_STORY_CAVE &&
        p->scene_id != SCENE_POO_POO_ISLAND) {
        scene_spawn_point(p->scene_id, p->id, out_x, out_y, out_z);
        return;
    }
    const Vec2 *pts = voxworld_spawn_points_ffa;
    int count = (int)(sizeof(voxworld_spawn_points_ffa) / sizeof(Vec2));
    int team_mode = phys_team_mode_enabled();
    int team = p->team_id;
    if (team_mode && (team != 0 && team != 1)) team = (p->id % 2);

    if (p->scene_id == SCENE_DUST_COMPOUND) {
        pts = dust_spawn_points_dm;
        count = (int)(sizeof(dust_spawn_points_dm) / sizeof(Vec2));
    } else if (p->scene_id == SCENE_OIL_TANKER) {
        pts = tanker_spawn_points_dm;
        count = (int)(sizeof(tanker_spawn_points_dm) / sizeof(Vec2));
    } else if (p->scene_id == SCENE_STADIUM) {
        pts = stadium_spawn_points_dm;
        count = (int)(sizeof(stadium_spawn_points_dm) / sizeof(Vec2));
    } else if (p->scene_id == SCENE_POO_POO_ISLAND) {
        pts = poo_poo_island_spawn_points_dm;
        count = (int)(sizeof(poo_poo_island_spawn_points_dm) / sizeof(Vec2));
    }

    if (team_mode && p->scene_id == SCENE_VOXWORLD) {
        if (team == 0) {
            pts = voxworld_spawn_points_red;
            count = (int)(sizeof(voxworld_spawn_points_red) / sizeof(Vec2));
        } else if (team == 1) {
            pts = voxworld_spawn_points_blue;
            count = (int)(sizeof(voxworld_spawn_points_blue) / sizeof(Vec2));
        }
    } else if (team_mode && p->scene_id == SCENE_DUST_COMPOUND) {
        if (team == 0) {
            pts = dust_spawn_points_attack;
            count = (int)(sizeof(dust_spawn_points_attack) / sizeof(Vec2));
        } else if (team == 1) {
            pts = dust_spawn_points_defend;
            count = (int)(sizeof(dust_spawn_points_defend) / sizeof(Vec2));
        }
    } else if (team_mode && p->scene_id == SCENE_OIL_TANKER) {
        if (team == 0) {
            pts = tanker_spawn_points_red;
            count = (int)(sizeof(tanker_spawn_points_red) / sizeof(Vec2));
        } else if (team == 1) {
            pts = tanker_spawn_points_blue;
            count = (int)(sizeof(tanker_spawn_points_blue) / sizeof(Vec2));
        }
    } else if (team_mode && p->scene_id == SCENE_STADIUM) {
        if (team == 0) {
            pts = stadium_spawn_points_red;
            count = (int)(sizeof(stadium_spawn_points_red) / sizeof(Vec2));
        } else if (team == 1) {
            pts = stadium_spawn_points_blue;
            count = (int)(sizeof(stadium_spawn_points_blue) / sizeof(Vec2));
        }
    } else if (team_mode && p->scene_id == SCENE_POO_POO_ISLAND) {
        if (team == 0) {
            pts = poo_poo_island_spawn_points_red;
            count = (int)(sizeof(poo_poo_island_spawn_points_red) / sizeof(Vec2));
        } else if (team == 1) {
            pts = poo_poo_island_spawn_points_blue;
            count = (int)(sizeof(poo_poo_island_spawn_points_blue) / sizeof(Vec2));
        }
    }

    int idx = (p->id + (int)(p->deaths * 3)) % count;
    *out_x = pts[idx].x;
    *out_z = pts[idx].y;
    *out_y = (p->scene_id == SCENE_DUST_COMPOUND)
        ? (dust_height_at(*out_x, *out_z) + 5.5f)
        : (p->scene_id == SCENE_OIL_TANKER
            ? 6.0f
            : (p->scene_id == SCENE_STADIUM
                ? (stadium_height_at(*out_x, *out_z) + 6.0f)
                : (p->scene_id == SCENE_POO_POO_ISLAND ? (poo_poo_island_height_at(*out_x, *out_z) + 6.0f)
                                                        : (voxworld_height_at(*out_x, *out_z) + 6.0f))));

    if (team_mode && (p->scene_id == SCENE_OIL_TANKER || p->scene_id == SCENE_STADIUM) && p->deaths == 0) {
        printf("[%s] team spawn sets red=%d blue=%d\n",
               p->scene_id == SCENE_OIL_TANKER ? "OIL_TANKER" : "STADIUM",
               p->scene_id == SCENE_OIL_TANKER ? (int)(sizeof(tanker_spawn_points_red) / sizeof(Vec2)) : (int)(sizeof(stadium_spawn_points_red) / sizeof(Vec2)),
               p->scene_id == SCENE_OIL_TANKER ? (int)(sizeof(tanker_spawn_points_blue) / sizeof(Vec2)) : (int)(sizeof(stadium_spawn_points_blue) / sizeof(Vec2)));
    }
}

static inline void scene_force_spawn(PlayerState *p) {
    float sx = 0.0f, sy = 0.0f, sz = 0.0f;
    phys_set_scene(p->scene_id);
    scene_spawn_for_player(p, &sx, &sy, &sz);
    p->x = sx; p->y = sy; p->z = sz;
    p->vx = 0.0f; p->vy = 0.0f; p->vz = 0.0f;
}

static inline void scene_safety_check(PlayerState *p) {
    if (!isfinite(p->x) || !isfinite(p->y) || !isfinite(p->z)) {
        scene_force_spawn(p);
        return;
    }
    if (p->scene_id == SCENE_GARAGE_OSAKA) {
        if (p->y < GARAGE_KILL_Y ||
            p->x < -GARAGE_BOUNDS_X || p->x > GARAGE_BOUNDS_X ||
            p->z < -GARAGE_BOUNDS_Z || p->z > GARAGE_BOUNDS_Z) {
            scene_force_spawn(p);
        }
        return;
    }
    if (p->scene_id == SCENE_STADIUM) {
        if (p->y < STADIUM_KILL_Y ||
            p->x < -STADIUM_BOUNDS_X || p->x > STADIUM_BOUNDS_X ||
            p->z < -STADIUM_BOUNDS_Z || p->z > STADIUM_BOUNDS_Z) {
            scene_force_spawn(p);
        }
        return;
    }
    if (p->scene_id == SCENE_VOXWORLD) {
        if (p->y < VOXWORLD_KILL_Y ||
            p->x < -VOXWORLD_BOUNDS_X || p->x > VOXWORLD_BOUNDS_X ||
            p->z < -VOXWORLD_BOUNDS_Z || p->z > VOXWORLD_BOUNDS_Z) {
            scene_force_spawn(p);
        }
        return;
    }
    if (p->scene_id == SCENE_DUST_COMPOUND) {
        if (p->y < DUST_KILL_Y ||
            p->x < -DUST_BOUNDS_X || p->x > DUST_BOUNDS_X ||
            p->z < -DUST_BOUNDS_Z || p->z > DUST_BOUNDS_Z) {
            scene_force_spawn(p);
        }
        return;
    }
    if (p->scene_id == SCENE_OIL_TANKER) {
        if (p->y < TANKER_KILL_Y ||
            p->x < -TANKER_BOUNDS_X || p->x > TANKER_BOUNDS_X ||
            p->z < -TANKER_BOUNDS_Z || p->z > TANKER_BOUNDS_Z) {
            scene_force_spawn(p);
        }
        return;
    }
    if (p->scene_id == SCENE_POO_POO_ISLAND) {
        if (p->y < POO_POO_ISLAND_KILL_Y ||
            p->x < -POO_POO_ISLAND_BOUNDS_X || p->x > POO_POO_ISLAND_BOUNDS_X ||
            p->z < -POO_POO_ISLAND_BOUNDS_Z || p->z > POO_POO_ISLAND_BOUNDS_Z) {
            scene_force_spawn(p);
        }
        return;
    }
    if (p->scene_id == SCENE_STORY_CAVE) {
        if (p->y < STORY_CAVE_KILL_Y ||
            p->x < -STORY_CAVE_BOUNDS_X || p->x > STORY_CAVE_BOUNDS_X ||
            p->z < -STORY_CAVE_BOUNDS_Z || p->z > STORY_CAVE_BOUNDS_Z) {
            scene_force_spawn(p);
        }
    }
}

static inline int scene_portal_active(int scene_id) {
    return scene_id == SCENE_GARAGE_OSAKA || scene_id == SCENE_STADIUM ||
           scene_id == SCENE_VOXWORLD || scene_id == SCENE_DUST_COMPOUND ||
           scene_id == SCENE_OIL_TANKER || scene_id == SCENE_POO_POO_ISLAND;
}

static inline int portal_resolve_destination(int current_scene, int portal_id, int slot,
                                             int *out_scene, float *out_x, float *out_y, float *out_z) {
    if (!out_scene || !out_x || !out_y || !out_z) return 0;
    if (current_scene == SCENE_GARAGE_OSAKA && portal_id == PORTAL_ID_GARAGE_EXIT) {
        *out_scene = SCENE_STADIUM;
        scene_spawn_point(*out_scene, slot, out_x, out_y, out_z);
        return 1;
    }
    if (current_scene == SCENE_STADIUM && portal_id == PORTAL_ID_GARAGE_EXIT) {
        *out_scene = SCENE_GARAGE_OSAKA;
        scene_spawn_point(*out_scene, slot, out_x, out_y, out_z);
        return 1;
    }
    if (current_scene == SCENE_GARAGE_OSAKA && portal_id == PORTAL_ID_GARAGE_TO_VOXWORLD) {
        *out_scene = SCENE_VOXWORLD;
        *out_x = -420.0f;
        *out_y = 8.0f;
        *out_z = 180.0f;
        return 1;
    }
    if (current_scene == SCENE_GARAGE_OSAKA && portal_id == PORTAL_ID_GARAGE_TO_DUST) {
        *out_scene = SCENE_DUST_COMPOUND;
        *out_x = DUST_ATTACK_SPAWN_X + 20.0f;
        *out_y = 9.0f;
        *out_z = DUST_ATTACK_SPAWN_Z;
        return 1;
    }
    if (current_scene == SCENE_GARAGE_OSAKA && portal_id == PORTAL_ID_GARAGE_TO_TANKER) {
        *out_scene = SCENE_OIL_TANKER;
        *out_x = -265.0f;
        *out_y = 6.0f;
        *out_z = 0.0f;
        return 1;
    }
    if (current_scene == SCENE_GARAGE_OSAKA && portal_id == PORTAL_ID_GARAGE_TO_POO_POO_ISLAND) {
        *out_scene = SCENE_POO_POO_ISLAND;
        *out_x = POO_POO_HUB_X;
        *out_y = poo_poo_island_height_at(POO_POO_HUB_X, POO_POO_HUB_Z) + 7.0f;
        *out_z = POO_POO_HUB_Z;
        return 1;
    }
    if (current_scene == SCENE_STADIUM && portal_id == PORTAL_ID_STADIUM_TO_VOXWORLD) {
        *out_scene = SCENE_VOXWORLD;
        *out_x = STADIUM_EDGE_TELEPORT_X;
        *out_y = STADIUM_EDGE_TELEPORT_Y;
        *out_z = STADIUM_EDGE_TELEPORT_Z;
        return 1;
    }
    if (current_scene == SCENE_VOXWORLD && portal_id == PORTAL_ID_VOXWORLD_TO_STADIUM) {
        *out_scene = SCENE_STADIUM;
        *out_x = STADIUM_EDGE_PORTAL_X - 20.0f;
        *out_y = STADIUM_EDGE_PORTAL_Y;
        *out_z = STADIUM_EDGE_PORTAL_Z;
        return 1;
    }
    if (current_scene == SCENE_DUST_COMPOUND && portal_id == PORTAL_ID_DUST_TO_GARAGE) {
        *out_scene = SCENE_GARAGE_OSAKA;
        *out_x = GARAGE_DUST_PORTAL_X + 10.0f;
        *out_y = GARAGE_DUST_PORTAL_Y;
        *out_z = GARAGE_DUST_PORTAL_Z;
        return 1;
    }
    if (current_scene == SCENE_OIL_TANKER && portal_id == PORTAL_ID_TANKER_TO_GARAGE) {
        *out_scene = SCENE_GARAGE_OSAKA;
        *out_x = GARAGE_TANKER_PORTAL_X + 10.0f;
        *out_y = GARAGE_TANKER_PORTAL_Y;
        *out_z = GARAGE_TANKER_PORTAL_Z;
        return 1;
    }
    if (current_scene == SCENE_POO_POO_ISLAND && portal_id == PORTAL_ID_POO_POO_ISLAND_TO_GARAGE) {
        *out_scene = SCENE_GARAGE_OSAKA;
        *out_x = GARAGE_POO_POO_PORTAL_X - 10.0f;
        *out_y = GARAGE_POO_POO_PORTAL_Y;
        *out_z = GARAGE_POO_POO_PORTAL_Z;
        return 1;
    }
    return 0;
}

static inline void scene_portal_info(int scene_id, float *out_x, float *out_y, float *out_z, float *out_radius) {
    if (scene_id == SCENE_GARAGE_OSAKA) {
        *out_x = GARAGE_PORTAL_X;
        *out_y = GARAGE_PORTAL_Y;
        *out_z = GARAGE_PORTAL_Z;
        *out_radius = GARAGE_PORTAL_RADIUS;
    } else if (scene_id == SCENE_STADIUM) {
        *out_x = STADIUM_PORTAL_X;
        *out_y = STADIUM_PORTAL_Y;
        *out_z = STADIUM_PORTAL_Z;
        *out_radius = STADIUM_PORTAL_RADIUS;
    } else if (scene_id == SCENE_VOXWORLD) {
        *out_x = VOXWORLD_PORTAL_X;
        *out_y = VOXWORLD_PORTAL_Y;
        *out_z = VOXWORLD_PORTAL_Z;
        *out_radius = VOXWORLD_PORTAL_RADIUS;
    } else if (scene_id == SCENE_DUST_COMPOUND) {
        *out_x = DUST_PORTAL_X;
        *out_y = DUST_PORTAL_Y;
        *out_z = DUST_PORTAL_Z;
        *out_radius = DUST_PORTAL_RADIUS;
    } else if (scene_id == SCENE_OIL_TANKER) {
        *out_x = TANKER_PORTAL_X;
        *out_y = TANKER_PORTAL_Y;
        *out_z = TANKER_PORTAL_Z;
        *out_radius = TANKER_PORTAL_RADIUS;
    } else if (scene_id == SCENE_POO_POO_ISLAND) {
        *out_x = POO_POO_ISLAND_PORTAL_X;
        *out_y = POO_POO_ISLAND_PORTAL_Y;
        *out_z = POO_POO_ISLAND_PORTAL_Z;
        *out_radius = POO_POO_ISLAND_PORTAL_RADIUS;
    } else {
        *out_x = 0.0f; *out_y = 0.0f; *out_z = 0.0f; *out_radius = 0.0f;
    }
}

static inline const VehiclePad *scene_vehicle_pads(int scene_id, int *out_count) {
    if (scene_id == SCENE_GARAGE_OSAKA) {
        if (out_count) *out_count = (int)(sizeof(garage_vehicle_pads) / sizeof(VehiclePad));
        return garage_vehicle_pads;
    }
    if (scene_id == SCENE_VOXWORLD) {
        if (out_count) *out_count = (int)(sizeof(voxworld_vehicle_pads) / sizeof(VehiclePad));
        return voxworld_vehicle_pads;
    }
    if (out_count) *out_count = 0;
    return NULL;
}

static inline const Vec2 *voxworld_get_flag_homes(int *out_count) {
    static const Vec2 homes[2] = { {voxworld_flag_home_red.x, voxworld_flag_home_red.y}, {voxworld_flag_home_blue.x, voxworld_flag_home_blue.y} };
    if (out_count) *out_count = 2;
    return homes;
}

static inline const VoxRouteAnchor *voxworld_get_route_anchors(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(voxworld_route_anchors) / sizeof(VoxRouteAnchor));
    return voxworld_route_anchors;
}

static inline const VoxRouteAnchor *dust_get_route_anchors(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(dust_route_anchors) / sizeof(VoxRouteAnchor));
    return dust_route_anchors;
}

static inline const VoxRouteAnchor *dust_get_objective_anchors(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(dust_objective_anchors) / sizeof(VoxRouteAnchor));
    return dust_objective_anchors;
}

static inline const Vec2 *dust_get_spawn_points_attack(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(dust_spawn_points_attack) / sizeof(Vec2));
    return dust_spawn_points_attack;
}

static inline const Vec2 *dust_get_spawn_points_defend(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(dust_spawn_points_defend) / sizeof(Vec2));
    return dust_spawn_points_defend;
}

static inline const Vec2 *dust_get_spawn_points_dm(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(dust_spawn_points_dm) / sizeof(Vec2));
    return dust_spawn_points_dm;
}

static inline const Vec2 *poo_poo_island_get_spawn_points(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(poo_poo_island_spawn_points_dm) / sizeof(Vec2));
    return poo_poo_island_spawn_points_dm;
}

static inline const VoxRouteAnchor *poo_poo_island_get_route_anchors(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(poo_poo_island_route_anchors) / sizeof(VoxRouteAnchor));
    return poo_poo_island_route_anchors;
}

static inline const VoxRouteAnchor *poo_poo_island_get_landmark_anchors(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(poo_poo_island_landmark_anchors) / sizeof(VoxRouteAnchor));
    return poo_poo_island_landmark_anchors;
}

static inline const VoxRouteAnchor *poo_poo_island_get_hub_anchors(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(poo_poo_island_hub_anchors) / sizeof(VoxRouteAnchor));
    return poo_poo_island_hub_anchors;
}

static inline const VoxRouteAnchor *poo_poo_island_get_scenic_anchors(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(poo_poo_island_scenic_anchors) / sizeof(VoxRouteAnchor));
    return poo_poo_island_scenic_anchors;
}

static inline const VoxRouteAnchor *poo_poo_island_get_activity_anchors(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(poo_poo_island_activity_anchors) / sizeof(VoxRouteAnchor));
    return poo_poo_island_activity_anchors;
}

static inline int scene_portal_triggered(PlayerState *p, int *out_portal_id) {
    if (!scene_portal_active(p->scene_id)) return 0;

    if (p->scene_id == SCENE_GARAGE_OSAKA) {
        float dx_vox = p->x - GARAGE_VOX_PORTAL_X;
        float dz_vox = p->z - GARAGE_VOX_PORTAL_Z;
        float dist_sq_vox = dx_vox * dx_vox + dz_vox * dz_vox;
        if (dist_sq_vox <= (GARAGE_VOX_PORTAL_RADIUS * GARAGE_VOX_PORTAL_RADIUS)) {
            if (out_portal_id) *out_portal_id = PORTAL_ID_GARAGE_TO_VOXWORLD;
            return 1;
        }
        float dx_tanker = p->x - GARAGE_TANKER_PORTAL_X;
        float dz_tanker = p->z - GARAGE_TANKER_PORTAL_Z;
        float dist_sq_tanker = dx_tanker * dx_tanker + dz_tanker * dz_tanker;
        if (dist_sq_tanker <= (GARAGE_TANKER_PORTAL_RADIUS * GARAGE_TANKER_PORTAL_RADIUS)) {
            if (out_portal_id) *out_portal_id = PORTAL_ID_GARAGE_TO_TANKER;
            return 1;
        }
        float dx_dust = p->x - GARAGE_DUST_PORTAL_X;
        float dz_dust = p->z - GARAGE_DUST_PORTAL_Z;
        float dist_sq_dust = dx_dust * dx_dust + dz_dust * dz_dust;
        if (dist_sq_dust <= (GARAGE_DUST_PORTAL_RADIUS * GARAGE_DUST_PORTAL_RADIUS)) {
            if (out_portal_id) *out_portal_id = PORTAL_ID_GARAGE_TO_DUST;
            return 1;
        }
        float dx_poo = p->x - GARAGE_POO_POO_PORTAL_X;
        float dz_poo = p->z - GARAGE_POO_POO_PORTAL_Z;
        float dist_sq_poo = dx_poo * dx_poo + dz_poo * dz_poo;
        if (dist_sq_poo <= (GARAGE_POO_POO_PORTAL_RADIUS * GARAGE_POO_POO_PORTAL_RADIUS)) {
            if (out_portal_id) *out_portal_id = PORTAL_ID_GARAGE_TO_POO_POO_ISLAND;
            return 1;
        }
    }

    if (p->scene_id == SCENE_STADIUM) {
        float dx_main = p->x - STADIUM_PORTAL_X;
        float dz_main = p->z - STADIUM_PORTAL_Z;
        float dist_sq_main = dx_main * dx_main + dz_main * dz_main;
        if (dist_sq_main <= (STADIUM_PORTAL_RADIUS * STADIUM_PORTAL_RADIUS)) {
            if (out_portal_id) *out_portal_id = PORTAL_ID_GARAGE_EXIT;
            return 1;
        }

        float dx_edge = p->x - STADIUM_EDGE_PORTAL_X;
        float dz_edge = p->z - STADIUM_EDGE_PORTAL_Z;
        float dist_sq_edge = dx_edge * dx_edge + dz_edge * dz_edge;
        if (dist_sq_edge <= (STADIUM_EDGE_PORTAL_RADIUS * STADIUM_EDGE_PORTAL_RADIUS)) {
            if (out_portal_id) *out_portal_id = PORTAL_ID_STADIUM_TO_VOXWORLD;
            return 1;
        }
        return 0;
    }

    float portal_x = 0.0f, portal_y = 0.0f, portal_z = 0.0f, portal_radius = 0.0f;
    scene_portal_info(p->scene_id, &portal_x, &portal_y, &portal_z, &portal_radius);
    if (portal_radius <= 0.0f) return 0;
    float dx = p->x - portal_x;
    float dz = p->z - portal_z;
    float dist_sq = dx * dx + dz * dz;
    if (dist_sq <= (portal_radius * portal_radius)) {
        if (out_portal_id) {
            *out_portal_id = (p->scene_id == SCENE_VOXWORLD)
                ? PORTAL_ID_VOXWORLD_TO_STADIUM
                : (p->scene_id == SCENE_DUST_COMPOUND
                    ? PORTAL_ID_DUST_TO_GARAGE
                    : (p->scene_id == SCENE_OIL_TANKER
                        ? PORTAL_ID_TANKER_TO_GARAGE
                        : (p->scene_id == SCENE_POO_POO_ISLAND ? PORTAL_ID_POO_POO_ISLAND_TO_GARAGE : PORTAL_ID_GARAGE_EXIT)));
        }
        return 1;
    }
    return 0;
}

static inline int scene_near_vehicle_pad(int scene_id, float x, float z, float max_dist, int *out_idx) {
    int count = 0;
    const VehiclePad *pads = scene_vehicle_pads(scene_id, &count);
    if (!pads || count == 0) return 0;
    float best_dist_sq = max_dist * max_dist;
    int best_idx = -1;
    for (int i = 0; i < count; i++) {
        float dx = x - pads[i].x;
        float dz = z - pads[i].z;
        float dist_sq = dx * dx + dz * dz;
        if (dist_sq <= best_dist_sq) {
            best_dist_sq = dist_sq;
            best_idx = i;
        }
    }
    if (best_idx >= 0) {
        if (out_idx) *out_idx = best_idx;
        return 1;
    }
    return 0;
}

static inline void apply_friction_2d(Vec2 *vel, float friction, float dt) {
    float speed = sqrtf(vel->x * vel->x + vel->y * vel->y);
    if (speed <= 0.0001f) return;
    float drop = speed * friction * dt;
    float newspeed = speed - drop;
    if (newspeed < 0.0f) newspeed = 0.0f;
    float ratio = newspeed / speed;
    vel->x *= ratio;
    vel->y *= ratio;
}

static inline float norm_yaw_deg(float yaw) {
    while (yaw >= 360.0f) yaw -= 360.0f;
    while (yaw < 0.0f) yaw += 360.0f;
    return yaw;
}

static inline float clamp_pitch_deg(float pitch) {
    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;
    return pitch;
}

static inline float angle_diff(float a, float b) {
    float d = a - b;
    while (d < -180) d += 360;
    while (d > 180) d -= 360;
    return d;
}

void reflect_vector(float *vx, float *vy, float *vz, float nx, float ny, float nz) {
    float dot = (*vx * nx) + (*vy * ny) + (*vz * nz);
    *vx = *vx - 2.0f * dot * nx;
    *vy = *vy - 2.0f * dot * ny;
    *vz = *vz - 2.0f * dot * nz;
}

static inline void katana_forward_dir(float yaw_deg, float pitch_deg, float *out_x, float *out_y, float *out_z) {
    float r = -yaw_deg * 0.0174533f;
    float rp = pitch_deg * 0.0174533f;
    float dx = sinf(r) * cosf(rp);
    float dy = sinf(rp);
    float dz = -cosf(r) * cosf(rp);
    if (dy > KATANA_DASH_UPWARD_LIMIT) dy = KATANA_DASH_UPWARD_LIMIT;
    if (dy < -KATANA_DASH_UPWARD_LIMIT) dy = -KATANA_DASH_UPWARD_LIMIT;
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len < 0.0001f) { dx = 0.0f; dy = 0.0f; dz = -1.0f; len = 1.0f; }
    *out_x = dx / len; *out_y = dy / len; *out_z = dz / len;
}

static inline int katana_dash_target_seen(const PlayerState *p, int target_id) {
    for (int i = 0; i < p->dash_hit_count && i < KATANA_DASH_HIT_MAX; i++) {
        if (p->dash_hit_targets[i] == target_id) return 1;
    }
    return 0;
}

static inline void katana_dash_remember_target(PlayerState *p, int target_id) {
    if (p->dash_hit_count >= KATANA_DASH_HIT_MAX) return;
    p->dash_hit_targets[p->dash_hit_count++] = target_id;
}

static inline void phys_set_death_direction(PlayerState *target, float incoming_x, float incoming_z) {
    float away_x = incoming_x;
    float away_z = incoming_z;
    float len = sqrtf(away_x * away_x + away_z * away_z);
    if (len < 0.0001f) {
        float vel_len = sqrtf(target->vx * target->vx + target->vz * target->vz);
        if (vel_len > 0.01f) {
            away_x = target->vx / vel_len;
            away_z = target->vz / vel_len;
        } else {
            float yaw_rad = -target->yaw * 0.0174533f;
            away_x = -sinf(yaw_rad);
            away_z = cosf(yaw_rad);
        }
    } else {
        away_x /= len;
        away_z /= len;
    }
    target->death_dir_x = away_x;
    target->death_dir_z = away_z;
}

static inline void phys_enter_death_state(PlayerState *attacker, PlayerState *target, unsigned int now_ms, unsigned int respawn_delay_ms, float incoming_x, float incoming_z) {
    const float DEATH_PLANAR_CARRY = 0.10f;
    const float DEATH_PLANAR_PUSH = 0.35f;
    const float DEATH_UPWARD_LAUNCH = 0.14f;

    if (!target || target->state == STATE_DEAD) return;
    if (attacker) {
        attacker->kills++;
        attacker->accumulated_reward += 1000.0f;
        attacker->hit_feedback = 30;
    }
    target->deaths++;
    target->state = STATE_DEAD;
    target->health = 0;
    target->in_shoot = 0;
    target->in_reload = 0;
    target->in_use = 0;
    target->in_jump = 0;
    target->in_ability = 0;
    target->is_shooting = 0;
    target->attack_cooldown = 0;
    target->reload_timer = 0;
    target->dash_timer = 0;
    target->dash_vx = target->dash_vy = target->dash_vz = 0.0f;
    target->stunned_until_ms = 0;
    target->stun_immune_until_ms = 0;
    target->death_time_ms = now_ms;
    target->death_duration_ms = respawn_delay_ms;
    target->respawn_time = now_ms + respawn_delay_ms;
    phys_set_death_direction(target, incoming_x, incoming_z);
    target->vx = target->vx * DEATH_PLANAR_CARRY + target->death_dir_x * DEATH_PLANAR_PUSH;
    target->vz = target->vz * DEATH_PLANAR_CARRY + target->death_dir_z * DEATH_PLANAR_PUSH;
    target->vy = DEATH_UPWARD_LAUNCH;
}

static inline void katana_apply_damage(PlayerState *attacker, PlayerState *target, int damage, int hit_feedback, unsigned int now_ms, unsigned int respawn_delay_ms) {
    if (!target->active || target->state == STATE_DEAD) return;
    attacker->accumulated_reward += 10.0f;
    target->shield_regen_timer = SHIELD_REGEN_DELAY;
    attacker->hit_feedback = hit_feedback;
    if (target->shield > 0) {
        if (target->shield >= damage) { target->shield -= damage; damage = 0; }
        else { damage -= target->shield; target->shield = 0; }
    }
    target->health -= damage;
    if (target->health <= 0) {
        float incoming_x = target->x - attacker->x;
        float incoming_z = target->z - attacker->z;
        phys_enter_death_state(attacker, target, now_ms, respawn_delay_ms, incoming_x, incoming_z);
    }
}

static inline void katana_try_slash(PlayerState *p, PlayerState *targets, unsigned int now_ms, unsigned int respawn_delay_ms) {
    float fx, fy, fz;
    katana_forward_dir(p->yaw, p->pitch, &fx, &fy, &fz);
    float origin_x = p->x;
    float origin_y = p->y + 2.0f;
    float origin_z = p->z;
    float min_dot = cosf((KATANA_SLASH_ARC_DEG * 0.5f) * 0.0174533f);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        PlayerState *target = &targets[i];
        if (target == p) continue;
        if (!target->active || target->state == STATE_DEAD) continue;
        if (target->scene_id != p->scene_id) continue;
        if (phys_is_friendly(p, target)) continue;
        float tx = target->x - origin_x;
        float ty = (target->y + 2.0f) - origin_y;
        float tz = target->z - origin_z;
        float dist_sq = tx*tx + ty*ty + tz*tz;
        if (dist_sq > (KATANA_SLASH_RANGE * KATANA_SLASH_RANGE)) continue;
        float dist = sqrtf(dist_sq);
        if (dist <= 0.0001f) continue;
        float inv_dist = 1.0f / dist;
        float dot = fx * (tx * inv_dist) + fy * (ty * inv_dist) + fz * (tz * inv_dist);
        if (dot < min_dot) continue;
        katana_apply_damage(p, target, KATANA_SLASH_DAMAGE, 12, now_ms, respawn_delay_ms);
        target->vx += fx * 0.35f;
        target->vz += fz * 0.35f;
    }
}

static inline int katana_try_start_dash(PlayerState *p) {
    if (p->current_weapon != WPN_KATANA) return 0;
    if (p->ability_cooldown > 0 || p->dash_timer > 0 || p->in_vehicle) return 0;
    float dx, dy, dz;
    katana_forward_dir(p->yaw, p->pitch, &dx, &dy, &dz);
    p->dash_timer = KATANA_DASH_TIME;
    p->ability_cooldown = KATANA_DASH_COOLDOWN;
    p->dash_vx = dx * KATANA_DASH_SPEED;
    p->dash_vy = dy * KATANA_DASH_SPEED * 0.45f;
    p->dash_vz = dz * KATANA_DASH_SPEED;
    p->dash_hit_count = 0;
    for (int i = 0; i < KATANA_DASH_HIT_MAX; i++) p->dash_hit_targets[i] = -1;
    p->recoil_anim = 0.45f;
    p->is_shooting = 4;
    return 1;
}

static inline void katana_update_dash_damage(PlayerState *p, PlayerState *targets, unsigned int now_ms, unsigned int respawn_delay_ms) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        PlayerState *target = &targets[i];
        if (target == p) continue;
        if (!target->active || target->state == STATE_DEAD) continue;
        if (target->scene_id != p->scene_id) continue;
        if (phys_is_friendly(p, target)) continue;
        if (katana_dash_target_seen(p, i)) continue;
        float dx = target->x - p->x;
        float dy = (target->y + 2.0f) - (p->y + 2.0f);
        float dz = target->z - p->z;
        float dist_sq = dx*dx + dy*dy + dz*dz;
        if (dist_sq > KATANA_DASH_HIT_RADIUS_SQ) continue;
        katana_dash_remember_target(p, i);
        katana_apply_damage(p, target, KATANA_DASH_DAMAGE, 18, now_ms, respawn_delay_ms);
        target->vx += p->dash_vx * 0.08f;
        target->vz += p->dash_vz * 0.08f;
    }
}

static inline int trace_map_boxes(float x1, float y1, float z1, float x2, float y2, float z2,
              float *out_x, float *out_y, float *out_z, float *nx, float *ny, float *nz) {
    for(int i=1; i<map_count; i++) {
        Box b = map_geo[i];
        if (x2 > b.x - b.w/2 && x2 < b.x + b.w/2 &&
            z2 > b.z - b.d/2 && z2 < b.z + b.d/2 &&
            y2 > b.y - b.h/2 && y2 < b.y + b.h/2) {
            float dx = x1 - b.x; float dz = z1 - b.z;
            float w = b.w; float d = b.d;
            if (fabs(dx)/w > fabs(dz)/d) {
                *nx = (dx > 0) ? 1.0f : -1.0f; *ny = 0.0f; *nz = 0.0f;
                *out_x = (dx > 0) ? b.x + b.w/2 + 0.1f : b.x - b.w/2 - 0.1f;
                *out_y = y2; *out_z = z2;
            } else {
                *nx = 0.0f; *ny = 0.0f; *nz = (dz > 0) ? 1.0f : -1.0f;
                *out_x = x2; *out_y = y2;
                *out_z = (dz > 0) ? b.z + b.d/2 + 0.1f : b.z - b.d/2 - 0.1f;
            }
            return 1;
        }
    }
    float terrain_ground = 0.0f;
    int terrain_ok = 0;
    if (g_scene_terrain.active && terrain_contains_world(&g_scene_terrain, x2, z2)) {
        terrain_ground = terrain_sample_height(&g_scene_terrain, x2, z2);
        terrain_ok = 1;
    }
    if (y2 < 0.0f || (terrain_ok && y2 < terrain_ground)) {
        *nx = 0.0f; *ny = 1.0f; *nz = 0.0f;
        *out_x = x2;
        *out_y = (terrain_ok ? terrain_ground : 0.0f) + 0.1f;
        *out_z = z2;
        return 1;
    }
    return 0;
}

int trace_map(float x1, float y1, float z1, float x2, float y2, float z2,
              float *out_x, float *out_y, float *out_z, float *nx, float *ny, float *nz) {
    return trace_map_boxes(x1, y1, z1, x2, y2, z2, out_x, out_y, out_z, nx, ny, nz);
}

static inline float phys_sample_ground_height(float x, float z, int *out_source_terrain) {
    float h = 0.0f;
    int source_terrain = 0;
    if (g_scene_terrain.active && terrain_contains_world(&g_scene_terrain, x, z)) {
        h = terrain_sample_height(&g_scene_terrain, x, z);
        source_terrain = 1;
    }
    for (int i = 1; i < map_count; i++) {
        Box b = map_geo[i];
        if (x > b.x - b.w/2 && x < b.x + b.w/2 && z > b.z - b.d/2 && z < b.z + b.d/2) {
            float top = b.y + b.h / 2.0f;
            if (top > h || !source_terrain) {
                h = top;
                source_terrain = 0;
            }
        }
    }
    if (out_source_terrain) *out_source_terrain = source_terrain;
    return h;
}

int check_hit_location(float ox, float oy, float oz, float dx, float dy, float dz, PlayerState *target) {
    if (!target->active) return 0;
    float tx = target->x; float tz = target->z;
    float h_size = target->in_vehicle ? 4.0f : HEAD_SIZE;
    float h_off = target->in_vehicle ? 2.0f : HEAD_OFFSET;
    float head_y = target->y + h_off;
    float vx = tx - ox, vy = head_y - oy, vz = tz - oz;
    float t = vx*dx + vy*dy + vz*dz;
    if (t > 0) {
        float cx = ox + dx*t, cy = oy + dy*t, cz = oz + dz*t;
        float dist_sq = (tx-cx)*(tx-cx) + (head_y-cy)*(head_y-cy) + (tz-cz)*(tz-cz);
        if (dist_sq < (h_size*h_size)) return 2;
    }
    float body_y = target->y + 2.0f;
    vx = tx - ox; vy = body_y - oy; vz = tz - oz;
    t = vx*dx + vy*dy + vz*dz;
    if (t > 0) {
        float cx = ox + dx*t, cy = oy + dy*t, cz = oz + dz*t;
        float dist_sq = (tx-cx)*(tx-cx) + (body_y-cy)*(body_y-cy) + (tz-cz)*(tz-cz);
        if (dist_sq < 7.2f) return 1; 
    }
    return 0;
}

static inline int phys_try_melee_strike(PlayerState *attacker, PlayerState *targets, int base_damage, int hit_feedback, int allow_headshot_multiplier, unsigned int now_ms, unsigned int respawn_delay_ms) {
    float r = -attacker->yaw * 0.0174533f;
    float rp = attacker->pitch * 0.0174533f;
    float dx = sinf(r) * cosf(rp);
    float dy = sinf(rp);
    float dz = -cosf(r) * cosf(rp);
    int hit_any = 0;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (attacker == &targets[i]) continue;
        if (!targets[i].active || targets[i].state == STATE_DEAD) continue;
        if (targets[i].scene_id != attacker->scene_id) continue;
        if (phys_is_friendly(attacker, &targets[i])) continue;

        float kx = attacker->x - targets[i].x;
        float ky = attacker->y - targets[i].y;
        float kz = attacker->z - targets[i].z;
        if ((kx*kx + ky*ky + kz*kz) > MELEE_RANGE_SQ + 22.0f) continue;

        int hit_type = check_hit_location(attacker->x, attacker->y + EYE_HEIGHT, attacker->z, dx, dy, dz, &targets[i]);
        if (hit_type <= 0) continue;

        int damage = base_damage;
        attacker->hit_feedback = hit_feedback;
        if (allow_headshot_multiplier && hit_type == 2 && targets[i].shield <= 0) {
            damage *= 3;
            attacker->hit_feedback = 20;
        }
        katana_apply_damage(attacker, &targets[i], damage, attacker->hit_feedback, now_ms, respawn_delay_ms);
        hit_any = 1;
    }

    return hit_any;
}

void apply_friction(PlayerState *p) {
    if (p->dash_timer > 0) return;
    float speed = sqrtf(p->vx*p->vx + p->vz*p->vz);
    if (speed < 0.001f) { p->vx = 0; p->vz = 0; return; }
    
    float drop = 0;
    if (!p->in_vehicle && p->on_ground) {
        if (p->crouching) {
            if (speed > 0.75f) drop = speed * SLIDE_FRICTION;
            else drop = speed * (FRICTION * 3.0f); 
        } else {
            float control = (speed < STOP_SPEED) ? STOP_SPEED : speed;
            drop = control * FRICTION; 
        }
    }
    float newspeed = speed - drop;
    if (newspeed < 0) newspeed = 0;
    newspeed /= speed;
    p->vx *= newspeed; p->vz *= newspeed;
}

static inline float buggy_drive_force_for_speed(float speed_norm) {
    if (speed_norm < 0.0f) speed_norm = 0.0f;
    if (speed_norm > 1.0f) speed_norm = 1.0f;

    if (speed_norm < BUGGY_TRANSMISSION_BAND1_END) {
        float t = speed_norm / BUGGY_TRANSMISSION_BAND1_END;
        return 1.30f + (0.92f - 1.30f) * t;
    }
    if (speed_norm < BUGGY_TRANSMISSION_BAND2_END) {
        float t = (speed_norm - BUGGY_TRANSMISSION_BAND1_END) /
                  (BUGGY_TRANSMISSION_BAND2_END - BUGGY_TRANSMISSION_BAND1_END);
        return 0.92f + (0.62f - 0.92f) * t;
    }
    if (speed_norm < BUGGY_TRANSMISSION_BAND3_END) {
        float t = (speed_norm - BUGGY_TRANSMISSION_BAND2_END) /
                  (BUGGY_TRANSMISSION_BAND3_END - BUGGY_TRANSMISSION_BAND2_END);
        return 0.62f + (0.84f - 0.62f) * t;
    }

    float t = (speed_norm - BUGGY_TRANSMISSION_BAND3_END) / (1.0f - BUGGY_TRANSMISSION_BAND3_END);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    t = t * t * (3.0f - 2.0f * t); // smoothstep taper
    return 0.84f + (0.06f - 0.84f) * t;
}

static inline void buggy_sample_wheel_heights(const BuggyState *b, float heights[4]) {
    float r = -b->yaw * (3.14159265358979323846f / 180.0f);
    float fwd_x = sinf(r), fwd_z = -cosf(r);
    float right_x = cosf(r), right_z = sinf(r);
    const float half_wb = BUGGY_WHEELBASE * 0.5f;
    const float half_tw = BUGGY_TRACK_WIDTH * 0.5f;
    const float ox[4] = { half_wb,  half_wb, -half_wb, -half_wb };
    const float oz[4] = { -half_tw, half_tw, -half_tw,  half_tw };
    for (int i = 0; i < 4; i++) {
        float wx = b->x + fwd_x * ox[i] + right_x * oz[i];
        float wz = b->z + fwd_z * ox[i] + right_z * oz[i];
        float h = terrain_sample_height(&g_scene_terrain, wx, wz);
        if (h < 0.0f) h = 0.0f;
        heights[i] = h;
    }
}

static inline void simulate_buggy_state(BuggyState *b, float throttle, float steer, float dt, int apply_input) {
    if (!b || !b->active) return;

    if (throttle > 1.0f) throttle = 1.0f;
    if (throttle < -1.0f) throttle = -1.0f;
    if (steer > 1.0f) steer = 1.0f;
    if (steer < -1.0f) steer = -1.0f;

    float dt_scale = dt / 0.016f;
    if (dt_scale < 0.0f) dt_scale = 0.0f;

    if (!apply_input) {
        throttle = 0.0f;
        steer = 0.0f;
    }

    float r = -b->yaw * (3.14159265358979323846f / 180.0f);
    float fwd_x = sinf(r);
    float fwd_z = -cosf(r);
    float right_x = cosf(r);
    float right_z = sinf(r);

    float forward_speed = b->vx * fwd_x + b->vz * fwd_z;
    float lateral_speed = b->vx * right_x + b->vz * right_z;
    float speed_norm = fabsf(forward_speed) / BUGGY_TOP_SPEED;
    if (speed_norm > 1.0f) speed_norm = 1.0f;

    if (throttle > 0.01f) {
        float drive = BUGGY_BASE_DRIVE_FORCE * buggy_drive_force_for_speed(speed_norm) * throttle * dt_scale;
        forward_speed += drive;
    } else if (throttle < -0.01f) {
        if (forward_speed > 0.03f) {
            float brake_strength = 1.0f + (forward_speed / BUGGY_TOP_SPEED) * 0.55f;
            forward_speed -= BUGGY_BRAKE_FRICTION * (-throttle) * brake_strength * dt_scale;
            if (forward_speed < 0.0f) forward_speed = 0.0f;
        } else {
            float rev_norm = fabsf(forward_speed) / BUGGY_REVERSE_TOP_SPEED;
            if (rev_norm > 1.0f) rev_norm = 1.0f;
            float rev_force = BUGGY_BASE_DRIVE_FORCE * 0.72f * buggy_drive_force_for_speed(rev_norm);
            forward_speed += throttle * rev_force * dt_scale;
        }
    } else {
        if (forward_speed > 0.0f) {
            forward_speed -= BUGGY_COAST_FRICTION * dt_scale;
            if (forward_speed < 0.0f) forward_speed = 0.0f;
        } else if (forward_speed < 0.0f) {
            forward_speed += (BUGGY_COAST_FRICTION * 1.15f) * dt_scale;
            if (forward_speed > 0.0f) forward_speed = 0.0f;
        }
    }

    if (forward_speed > BUGGY_TOP_SPEED) forward_speed = BUGGY_TOP_SPEED;
    if (forward_speed < -BUGGY_REVERSE_TOP_SPEED) forward_speed = -BUGGY_REVERSE_TOP_SPEED;

    float abs_norm = fabsf(forward_speed) / BUGGY_TOP_SPEED;
    if (abs_norm > 1.0f) abs_norm = 1.0f;

    float steer_rate = BUGGY_TURN_RATE_LOW + (BUGGY_TURN_RATE_HIGH - BUGGY_TURN_RATE_LOW) * abs_norm;
    float steer_authority = 0.38f + 0.62f * abs_norm;
    float steer_sign = (forward_speed < -0.03f) ? -1.0f : 1.0f;
    float steer_response = apply_input ? 0.22f : 0.10f;
    b->steer += (steer - b->steer) * steer_response * dt_scale;
    if (b->steer > 1.0f) b->steer = 1.0f;
    if (b->steer < -1.0f) b->steer = -1.0f;
    b->yaw = norm_yaw_deg(b->yaw + b->steer * steer_rate * steer_authority * steer_sign * dt_scale);

    float grip = BUGGY_LATERAL_GRIP * (0.62f + 0.68f * abs_norm) * dt_scale;
    if (grip > 1.0f) grip = 1.0f;
    lateral_speed *= (1.0f - grip);

    float r2 = -b->yaw * (3.14159265358979323846f / 180.0f);
    float new_fwd_x = sinf(r2);
    float new_fwd_z = -cosf(r2);
    float new_right_x = cosf(r2);
    float new_right_z = sinf(r2);
    b->vx = new_fwd_x * forward_speed + new_right_x * lateral_speed;
    b->vz = new_fwd_z * forward_speed + new_right_z * lateral_speed;

    if (!b->grounded) b->vy -= BUGGY_GRAVITY * dt_scale;
    b->x += b->vx;
    b->y += b->vy;
    b->z += b->vz;

    float heights[4];
    buggy_sample_wheel_heights(b, heights);
    float front_avg = 0.5f * (heights[0] + heights[1]);
    float rear_avg = 0.5f * (heights[2] + heights[3]);
    float left_avg = 0.5f * (heights[0] + heights[2]);
    float right_avg = 0.5f * (heights[1] + heights[3]);
    float target_pitch = atanf((front_avg - rear_avg) / BUGGY_WHEELBASE) * (180.0f / 3.14159265358979323846f);
    float target_roll = atanf((left_avg - right_avg) / BUGGY_TRACK_WIDTH) * (180.0f / 3.14159265358979323846f);
    if (target_pitch > 28.0f) target_pitch = 28.0f;
    if (target_pitch < -28.0f) target_pitch = -28.0f;
    if (target_roll > 22.0f) target_roll = 22.0f;
    if (target_roll < -22.0f) target_roll = -22.0f;
    float support_max = heights[0];
    for (int i = 1; i < 4; i++) if (heights[i] > support_max) support_max = heights[i];
    float support_y = support_max + BUGGY_WHEEL_RADIUS + BUGGY_CHASSIS_CLEARANCE;
    if (b->y <= support_y + 0.05f) {
        b->y = support_y;
        b->vy = 0.0f;
        b->grounded = 1;
        b->pitch += (target_pitch - b->pitch) * 0.35f;
        b->roll += (target_roll - b->roll) * 0.35f;
    } else {
        b->grounded = 0;
        b->pitch *= 0.995f;
        b->roll *= 0.995f;
    }
}

static inline void simulate_buggy_drive(PlayerState *p, float throttle, float steer, float dt) {
    if (!p || !p->in_vehicle) return;
    BuggyState temp = {0};
    temp.active = 1;
    temp.x = p->x; temp.y = p->y; temp.z = p->z;
    temp.vx = p->vx; temp.vy = p->vy; temp.vz = p->vz;
    temp.yaw = p->yaw;
    temp.grounded = p->on_ground;
    simulate_buggy_state(&temp, throttle, steer, dt, 1);
    p->x = temp.x; p->y = temp.y; p->z = temp.z;
    p->vx = temp.vx; p->vy = temp.vy; p->vz = temp.vz;
    p->yaw = temp.yaw;
    p->on_ground = temp.grounded;
}

void accelerate(PlayerState *p, float wish_x, float wish_z, float wish_speed, float accel) {
    float speed = sqrtf(p->vx*p->vx + p->vz*p->vz);
    if (p->crouching && speed > 0.75f && p->on_ground) return;
    if (p->crouching && p->on_ground && speed < 0.75f && wish_speed > CROUCH_SPEED) wish_speed = CROUCH_SPEED;
    float current_speed = (p->vx * wish_x) + (p->vz * wish_z);
    float add_speed = wish_speed - current_speed;
    if (add_speed <= 0) return;
    float acc_speed = accel * MAX_SPEED; 
    if (acc_speed > add_speed) acc_speed = add_speed;
    p->vx += acc_speed * wish_x; p->vz += acc_speed * wish_z;
}

void resolve_collision(PlayerState *p) {
    float pw = p->in_vehicle ? 3.0f : PLAYER_WIDTH;
    float ph = p->in_vehicle ? 3.0f : (p->crouching ? (PLAYER_HEIGHT / 2.0f) : PLAYER_HEIGHT);
    p->on_ground = 0;
    g_last_ground_source_terrain = 0;

    float ground_floor = 0.0f;
    int terrain_ok = 0;
    if (g_scene_terrain.active && terrain_contains_world(&g_scene_terrain, p->x, p->z)) {
        terrain_ok = 1;
        ground_floor = terrain_sample_height(&g_scene_terrain, p->x, p->z);
        if (ground_floor < 0.0f) ground_floor = 0.0f;
    }
    if (p->y < 0.0f || (terrain_ok && p->y < ground_floor)) {
        p->y = terrain_ok ? ground_floor : 0.0f;
        p->vy = 0.0f;
        p->on_ground = 1;
        g_last_ground_source_terrain = terrain_ok ? 1 : 0;
    }
    for(int i=1; i<map_count; i++) {
        Box b = map_geo[i];
        if (p->x + pw > b.x - b.w/2 && p->x - pw < b.x + b.w/2 &&
            p->z + pw > b.z - b.d/2 && p->z - pw < b.z + b.d/2) {
            if (p->y < b.y + b.h/2 && p->y + ph > b.y - b.h/2) {
                float prev_y = p->y - p->vy;
                if (prev_y >= b.y + b.h/2) {
                    p->y = b.y + b.h/2; p->vy = 0; p->on_ground = 1;
                    g_last_ground_source_terrain = 0;
                } else {
                    float dx = p->x - b.x; float dz = p->z - b.z;
                    float w = (b.w > 0.1f) ? b.w : 1.0f;
                    float d = (b.d > 0.1f) ? b.d : 1.0f;
                    if (fabs(dx)/w > fabs(dz)/d) { 
                        p->vx = 0; p->x = (dx > 0) ? b.x + b.w/2 + pw : b.x - b.w/2 - pw;
                    } else { 
                        p->vz = 0; p->z = (dz > 0) ? b.z + b.d/2 + pw : b.z - b.d/2 - pw;
                    }
                }
            }
        }
    }
}

void phys_respawn(PlayerState *p, unsigned int now) {
    p->active = 1; p->state = STATE_ALIVE;
    p->health = 100; p->shield = 100; p->respawn_time = 0; p->in_vehicle = 0;
    p->katana_slash_timer = 0;
    p->dash_timer = 0;
    p->dash_vx = p->dash_vy = p->dash_vz = 0.0f;
    p->dash_hit_count = 0;
    p->use_was_down = 0;
    if (p->scene_id != SCENE_GARAGE_OSAKA && p->scene_id != SCENE_STADIUM &&
        p->scene_id != SCENE_VOXWORLD && p->scene_id != SCENE_DUST_COMPOUND &&
        p->scene_id != SCENE_OIL_TANKER && p->scene_id != SCENE_POO_POO_ISLAND) {
        p->scene_id = SCENE_GARAGE_OSAKA;
    }
    scene_spawn_for_player(p, &p->x, &p->y, &p->z);
    p->current_weapon = WPN_MAGNUM;
    for(int i=0; i<MAX_WEAPONS; i++) p->ammo[i] = WPN_STATS[i].ammo_max;
    p->storm_charges = 0;
    p->carried_flag_team_id = -1;
    p->ctf_melee_cooldown_ms = 0;
    p->ability_cooldown = 0;
    p->portal_cooldown_until_ms = 0;
    p->stunned_until_ms = 0;
    p->stun_immune_until_ms = 0;
    p->death_time_ms = 0;
    p->death_duration_ms = 0;
    p->death_dir_x = 0.0f;
    p->death_dir_z = 0.0f;
    if (p->is_bot) {
        PlayerState *winner = get_best_bot();
        if (winner && winner != p) evolve_bot(p, winner);
    }
}

static inline void spawn_projectile(Projectile *projectiles, PlayerState *p, int damage, int bounces, float speed_mult) {
    for(int i=0; i<MAX_PROJECTILES; i++) {
        if (!projectiles[i].active) {
            Projectile *proj = &projectiles[i];
            proj->active = 1;
            proj->owner_id = p->id;
            proj->damage = damage;
            proj->bounces_left = bounces;
            proj->scene_id = (unsigned char)p->scene_id;

            float r = -p->yaw * 0.0174533f; float rp = p->pitch * 0.0174533f;
            float speed = 4.0f * speed_mult;
            proj->vx = sinf(r) * cosf(rp) * speed;
            proj->vy = sinf(rp) * speed;
            proj->vz = -cosf(r) * cosf(rp) * speed;
            proj->x = p->x;
            proj->y = p->y + EYE_HEIGHT;
            proj->z = p->z;
            return;
        }
    }
}

void update_weapons(PlayerState *p, PlayerState *targets, Projectile *projectiles, int shoot, int reload, int ability_press, unsigned int now_ms, unsigned int respawn_delay_ms) {
    if (p->in_vehicle) return; 
    if (p->reload_timer > 0) p->reload_timer--;
    if (p->attack_cooldown > 0) p->attack_cooldown--;
    if (p->is_shooting > 0) p->is_shooting--;
    if (p->ability_cooldown > 0) p->ability_cooldown--;

    if (p->katana_slash_timer > 0) p->katana_slash_timer--;
    if (p->dash_timer > 0) {
        p->vx = p->dash_vx;
        p->vy = p->dash_vy;
        p->vz = p->dash_vz;
        katana_update_dash_damage(p, targets, now_ms, respawn_delay_ms);
        p->dash_timer--;
        p->is_shooting = 2;
        if (p->dash_timer <= 0) {
            p->dash_timer = 0;
            p->dash_vx = p->dash_vy = p->dash_vz = 0.0f;
        }
    }

    int w = p->current_weapon;
    if (ability_press) {
        if (w == WPN_KATANA) katana_try_start_dash(p);
        else if (p->ability_cooldown == 0 && p->storm_charges == 0) {
            p->storm_charges = 5;
            p->ability_cooldown = 480;
        }
    }

    if (reload && p->reload_timer == 0 && w != WPN_KNIFE && w != WPN_KATANA) {
        if (p->ammo[w] < WPN_STATS[w].ammo_max) {
            if (p->ammo[w] > 0) p->reload_timer = RELOAD_TIME_TACTICAL;
            else p->reload_timer = RELOAD_TIME_FULL; 
        }
    }
    if (p->reload_timer == 1) p->ammo[w] = WPN_STATS[w].ammo_max;
    if (p->dash_timer > 0) return;
    if (shoot && p->attack_cooldown == 0 && p->reload_timer == 0) {
        if (p->storm_charges > 0 && w == WPN_SNIPER) {
            int storm_damage = (int)(WPN_STATS[w].dmg * 0.7f);
            spawn_projectile(projectiles, p, storm_damage, 1, 1.5f);
            p->storm_charges--;
            p->attack_cooldown = 8;
            p->recoil_anim = 0.5f;
            return;
        }
        if (w != WPN_KNIFE && w != WPN_KATANA && p->ammo[w] <= 0) p->reload_timer = RELOAD_TIME_FULL;
        else {
            p->is_shooting = 5; p->recoil_anim = 1.0f;
            p->attack_cooldown = WPN_STATS[w].rof;
            if (w != WPN_KNIFE && w != WPN_KATANA) p->ammo[w]--;
            if (w == WPN_KATANA) {
                p->katana_slash_timer = KATANA_SLASH_ACTIVE_TICKS;
                p->recoil_anim = 0.35f;
                katana_try_slash(p, targets, now_ms, respawn_delay_ms);
                return;
            }
            if (w == WPN_KNIFE) {
                phys_try_melee_strike(p, targets, WPN_STATS[w].dmg, 10, 1, now_ms, respawn_delay_ms);
                return;
            }
            
            float r = -p->yaw * 0.0174533f; float rp = p->pitch * 0.0174533f;
            float dx = sinf(r) * cosf(rp); float dy = sinf(rp); float dz = -cosf(r) * cosf(rp);
            if (WPN_STATS[w].spr > 0) {
                dx += phys_rand_f() * WPN_STATS[w].spr;
                dy += phys_rand_f() * WPN_STATS[w].spr;
                dz += phys_rand_f() * WPN_STATS[w].spr;
            }

            for(int i=0; i<MAX_CLIENTS; i++) {
                if (p == &targets[i]) continue;
                if (!targets[i].active || targets[i].state == STATE_DEAD) continue;
                if (targets[i].scene_id != p->scene_id) continue;
                if (phys_is_friendly(p, &targets[i])) continue;
                int hit_type = check_hit_location(p->x, p->y + EYE_HEIGHT, p->z, dx, dy, dz, &targets[i]);
                if (hit_type > 0) {
                    printf("🔫 HIT! Dmg: %d on Target %d\n", WPN_STATS[w].dmg, i);
                    int damage = WPN_STATS[w].dmg;
                    if (hit_type == 2 && targets[i].shield <= 0) { damage *= 3; p->hit_feedback = 20;
                    } else { p->hit_feedback = 10; } 
                    katana_apply_damage(p, &targets[i], damage, p->hit_feedback, now_ms, respawn_delay_ms);
                }
            }
        }
    }
}

void phys_store_history(ServerState *server, int client_id, unsigned int now) {
    if (client_id < 0 || client_id >= MAX_CLIENTS) return;
    int slot = (now / 16) % LAG_HISTORY; 
    server->history[client_id][slot].active = 1;
    server->history[client_id][slot].timestamp = now;
    server->history[client_id][slot].x = server->players[client_id].x;
    server->history[client_id][slot].y = server->players[client_id].y;
    server->history[client_id][slot].z = server->players[client_id].z;
}

int phys_resolve_rewind(ServerState *server, int client_id, unsigned int target_time, float *out_pos) {
    LagRecord *hist = server->history[client_id];
    for(int i=0; i<LAG_HISTORY; i++) {
        if (!hist[i].active) continue;
        if (hist[i].timestamp == target_time) { 
            out_pos[0] = hist[i].x; out_pos[1] = hist[i].y; out_pos[2] = hist[i].z;
            return 1;
        }
    }
    return 0;
}
#endif
