#ifndef PROTOCOL_H
#define PROTOCOL_H

#ifdef _WIN32
    #include <winsock2.h>
#elif defined(__has_include)
    #if __has_include(<netinet/in.h>)
        #include <netinet/in.h>
    #else
        typedef struct sockaddr_in {
            unsigned short sin_family;
            unsigned short sin_port;
            unsigned int sin_addr_s_addr;
            unsigned char sin_zero[8];
        } sockaddr_in;
    #endif
#else
    #include <netinet/in.h>
#endif

#define MAX_CLIENTS 70
#define MAX_WEAPONS 6
#define MAX_PROJECTILES 1024
#define MAX_HELICOPTERS 8
#define MAX_BUGGIES 16
#define LAG_HISTORY 64

#define SCENE_GARAGE_OSAKA 0
#define SCENE_STADIUM 1
#define SCENE_VOXWORLD 2
#define SCENE_DUST_COMPOUND 3
#define SCENE_CITY 4
#define SCENE_OIL_TANKER 5
#define SCENE_POO_POO_ISLAND 6
#define SCENE_STORY_CAVE 7

#define PACKET_CONNECT 0
#define PACKET_USERCMD 1
#define PACKET_SNAPSHOT 2
#define PACKET_WELCOME  3
#define PACKET_DISCONNECT 4

#define STATE_ALIVE 0
#define STATE_DEAD  1
#define STATE_SPECTATOR 2

#define WPN_KNIFE 0
#define WPN_MAGNUM 1
#define WPN_AR 2
#define WPN_SHOTGUN 3
#define WPN_SNIPER 4
#define WPN_KATANA 5

#define RELOAD_TIME_FULL 60      
#define RELOAD_TIME_TACTICAL 42  
#define SHIELD_REGEN_DELAY 180 

typedef struct {
    int active;
    unsigned int last_heard_ms;
} ClientMeta;

typedef struct {
    unsigned char type;
    unsigned char client_id;
    unsigned short sequence;
    unsigned int timestamp;
    unsigned char entity_count; 
    unsigned char scene_id;
} NetHeader;

typedef struct {
    unsigned int sequence;
    unsigned int timestamp;
    unsigned short msec;
    float fwd; float str;
    float yaw; float pitch;    
    unsigned int buttons;
    int weapon_idx;
} UserCmd;

#define BTN_JUMP   1
#define BTN_ATTACK 2
#define BTN_CROUCH 4
#define BTN_RELOAD 8
#define BTN_USE    16
#define BTN_ABILITY_1 32
#define BTN_VEHICLE_2 64

#define VEH_NONE  0
#define VEH_BUGGY 1
#define VEH_BIKE  2
#define VEH_HELICOPTER 3

typedef struct {
    int id;
    int dmg; int rof; int cnt; float spr; int ammo_max;
} WeaponStats;

static const WeaponStats WPN_STATS[MAX_WEAPONS] = {
    {WPN_KNIFE,   200, 20, 1, 0.0f,  0},   
    {WPN_MAGNUM,  45, 25, 1, 0.0f,  8},   
    {WPN_AR,      20, 6,  1, 0.04f, 30},  
    {WPN_SHOTGUN, 128, 17, 8, 0.15f, 8},   
    {WPN_SNIPER,  101, 52, 1, 0.0f,  5},   
    {WPN_KATANA,   40, 28, 1, 0.0f,  0}    
};

typedef struct {
    int active; float x, y, z; float vx, vy, vz; int owner_id;
    int bounces_left;
    int damage;
    unsigned char scene_id;
} Projectile;

typedef struct {
    unsigned char id; 
    unsigned char scene_id;
    unsigned char is_bot;
    signed char team_id;
    unsigned int last_seq;
    float x, y, z; float yaw, pitch;
    unsigned char current_weapon;
    unsigned char state;
    unsigned char health;
    unsigned char shield;
    unsigned char is_shooting;
    unsigned char crouching;
    float reward_feedback; 
    unsigned char ammo;
    unsigned char in_vehicle;
    signed char carried_flag_team_id;
    unsigned char hit_feedback; 
    unsigned char storm_charges;
    unsigned short kills;
    unsigned short deaths;
    unsigned short death_elapsed_ms;
    unsigned short death_duration_ms;
    float death_dir_x;
    float death_dir_z;
} NetPlayer;

typedef struct {
    unsigned char id;
    unsigned char scene_id;
    unsigned char active;
    unsigned char grounded;
    float x, y, z;
    float vx, vy, vz;
    float yaw;
    float pitch_visual;
    float roll_visual;
    float rotor_angle;
    float rotor_speed;
    unsigned char health;
    signed char occupant_player_id;
} NetHelicopter;

typedef struct {
    unsigned char id;
    unsigned char scene_id;
    unsigned char active;
    unsigned char grounded;
    float x, y, z;
    float vx, vy, vz;
    float yaw;
    float pitch;
    float roll;
    float steer;
    signed char occupant_player_id;
} NetBuggy;

typedef struct {
    int version;
    float w_aggro;
    float w_strafe; float w_jump; float w_slide; float w_turret; float w_repel;      
} BotGenome;

typedef struct {
    int id;
    int scene_id;
    int active; int is_bot;
    int team_id;
    float x, y, z; float vx, vy, vz; float yaw, pitch; int on_ground;
    float in_fwd;
    float in_strafe;
    int in_jump; int in_shoot; int in_reload; int crouching; int in_use; int in_bike;
    int use_was_down; int bike_was_down;
    int in_ability;
    int current_weapon; int ammo[MAX_WEAPONS];
    int reload_timer; int attack_cooldown;
    int is_shooting; int jump_timer;
    int health; int shield; int shield_regen_timer; int state;
    int kills; int deaths; int hit_feedback; float recoil_anim;
    int level;
    int xp;
    int xp_to_next;
    unsigned int last_xp_award_time;
    int in_vehicle;
    int vehicle_type;
    int bike_gear;
    int vehicle_cooldown;
    unsigned int portal_cooldown_until_ms;
    float accumulated_reward; 
    BotGenome brain;
    unsigned int last_hit_time;
    unsigned int respawn_time;
    unsigned int death_time_ms;
    unsigned int death_duration_ms;
    float death_dir_x;
    float death_dir_z;
    int storm_charges;
    int ability_cooldown;
    int katana_slash_timer;
    int dash_timer;
    float dash_vx;
    float dash_vy;
    float dash_vz;
    int dash_hit_count;
    int dash_hit_targets[8];
    unsigned int stunned_until_ms;
    unsigned int stun_immune_until_ms;
    float run_phase;
    float run_weight;
    int carried_flag_team_id;
    unsigned int ctf_last_flag_event_ms;
    unsigned int ctf_melee_cooldown_ms;
    int ctf_bot_intent;
    float ctf_cumulative_reward;
    float ctf_last_reward;
    unsigned int ctf_last_stuck_ms;
    float ctf_last_objective_progress;
} PlayerState;

typedef struct {
    int active; unsigned int timestamp;
    float x, y, z;
    float vx, vy, vz;
} LagRecord;

typedef struct {
    float forward;
    float yaw;
    float strafe;
    int ascend;
    int descend;
} HeliInputState;

typedef struct {
    int active;
    int id;
    int scene_id;
    float x, y, z;
    float vx, vy, vz;
    float yaw;
    float pitch_visual;
    float roll_visual;
    float rotor_angle;
    float rotor_speed;
    float collective;
    int health;
    int occupant_player_id;
    int grounded;
    HeliInputState input;
} HelicopterState;

typedef struct {
    int active;
    int id;
    int scene_id;
    float x, y, z;
    float vx, vy, vz;
    float yaw;
    float pitch;
    float roll;
    float steer;
    float throttle;
    int health;
    int occupant_player_id;
    int grounded;
} BuggyState;

typedef enum {
    FLAG_AT_HOME = 0,
    FLAG_CARRIED = 1,
    FLAG_DROPPED = 2
} FlagState;

typedef struct {
    int owning_team_id;
    int scene_id;
    float home_x, home_y, home_z;
    float x, y, z;
    int state;
    int carrier_id;
    unsigned int dropped_until_ms;
    unsigned int last_interaction_ms;
} CtfFlagState;

typedef struct {
    int active;
    int scene_id;
    int score_limit;
    int capture_scores[2];
    CtfFlagState flags[2];
    unsigned int event_counter;
} CtfMatchState;

typedef enum { MODE_DEATHMATCH=0, MODE_TDM=1, MODE_SURVIVAL=2, MODE_CTF=3, MODE_ODDBALL=4, MODE_LOCAL=98, MODE_NET=99, MODE_EVOLUTION=100, MODE_TDMB=101, MODE_TDMO=102, MODE_CTFB=103, MODE_CTFO=104, MODE_STORY=105 } GameMode;
typedef enum {
    STORY_PHASE_CUTSCENE = 0,
    STORY_PHASE_PLAYING = 1,
    STORY_PHASE_RIFT_OPENING = 2,
    STORY_PHASE_SWARM = 3,
    STORY_PHASE_AFTER_SWARM = 4,
    STORY_PHASE_FAILED = 5,
    STORY_PHASE_COMPLETE = 6
} StoryPhase;

#define STORY_MAX_SWARM_ENEMIES 24

typedef enum {
    STORY_ENEMY_NONE = 0,
    STORY_ENEMY_RIFT_HOUND = 1,
    STORY_ENEMY_SHAMBLER_TROOPER = 2,
    STORY_ENEMY_GORE_BRUTE = 3
} StoryEnemyType;

typedef struct {
    int active;
    int defeated;
    float x, y, z;
    float yaw;
    float health;
    float max_health;
    unsigned int last_attack_ms;
    unsigned int hurt_flash_until_ms;
} StoryBossState;

typedef struct {
    int active;
    int type;
    float x, y, z;
    float vx, vy, vz;
    float yaw;
    float radius;
    float height;
    float health;
    float max_health;
    float speed;
    float attack_radius;
    int damage;
    unsigned int spawn_ms;
    unsigned int last_attack_ms;
    unsigned int hurt_flash_until_ms;
    unsigned int death_ms;
    int dying;
} StoryEnemy;

typedef struct {
    int active;
    float x, y, z;
    float yaw;
    float radius;
    unsigned int opened_ms;
    unsigned int next_spew_ms;
    int spew_count;
    int wave_spawned;
    unsigned int pulse_seed;
    unsigned int last_spew_ms;
} StoryRiftState;

typedef struct {
    PlayerState players[MAX_CLIENTS];
    Projectile projectiles[MAX_PROJECTILES];
    HelicopterState helicopters[MAX_HELICOPTERS];
    BuggyState buggies[MAX_BUGGIES];
    LagRecord history[MAX_CLIENTS][LAG_HISTORY];
    int server_tick;
    int game_mode;
    int scene_id;
    int pending_scene;
    int transition_timer;
    int team_scores[2];
    int score_limit;
    int match_over;
    int winning_team;
    int story_phase;
    unsigned int story_phase_start_ms;
    StoryBossState story_boss;
    StoryRiftState story_rift;
    StoryEnemy story_swarm[STORY_MAX_SWARM_ENEMIES];
    CtfMatchState ctf;
    struct sockaddr_in clients[MAX_CLIENTS];
    ClientMeta client_meta[MAX_CLIENTS];
} ServerState;

#endif
