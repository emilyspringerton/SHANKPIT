#ifndef PROTOCOL_H
#define PROTOCOL_H

#define MAX_CLIENTS 70
#define MAX_WEAPONS 5
#define MAX_PROJECTILES 1024
#define LAG_HISTORY 64

#define PACKET_CONNECT 0
#define PACKET_USERCMD 1
#define PACKET_SNAPSHOT 2
#define PACKET_WELCOME  3

#define STATE_ALIVE 0
#define STATE_DEAD  1
#define STATE_SPECTATOR 2

#define WPN_KNIFE 0
#define WPN_MAGNUM 1
#define WPN_AR 2
#define WPN_SHOTGUN 3
#define WPN_SNIPER 4

#define RELOAD_TIME_FULL 60      
#define RELOAD_TIME_TACTICAL 42  
#define SHIELD_REGEN_DELAY 180 

typedef struct {
    unsigned char type;
    unsigned char client_id;
    unsigned short sequence;
    unsigned int timestamp;
    unsigned char entity_count; 
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

typedef struct {
    int id;
    int dmg; int rof; int cnt; float spr; int ammo_max;
} WeaponStats;

static const WeaponStats WPN_STATS[MAX_WEAPONS] = {
    {WPN_KNIFE,   200, 20, 1, 0.0f,  0},   
    {WPN_MAGNUM,  45, 25, 1, 0.0f,  6},   
    {WPN_AR,      20, 6,  1, 0.04f, 30},  
    {WPN_SHOTGUN, 128, 17, 8, 0.15f, 8},   
    {WPN_SNIPER,  101, 70, 1, 0.0f,  5}    
};

typedef struct {
    int active; float x, y, z; float vx, vy, vz; int owner_id;
    int bounces_left;
    int damage;
} Projectile;

typedef struct {
    unsigned char id; 
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
    unsigned char hit_feedback; 
    unsigned char storm_charges;
} NetPlayer;

typedef struct {
    int version;
    float w_aggro;
    float w_strafe; float w_jump; float w_slide; float w_turret; float w_repel;      
} BotGenome;

typedef struct {
    int id;
    int active; int is_bot;
    float x, y, z; float vx, vy, vz; float yaw, pitch; int on_ground;
    float in_fwd;
    float in_strafe;
    int in_jump; int in_shoot; int in_reload; int crouching; int in_use;
    int in_ability;
    int current_weapon; int ammo[MAX_WEAPONS];
    int reload_timer; int attack_cooldown;
    int is_shooting; int jump_timer;
    int health; int shield; int shield_regen_timer; int state;
    int kills; int deaths; int hit_feedback; float recoil_anim;
    int in_vehicle;      
    int vehicle_cooldown;
    float accumulated_reward; 
    BotGenome brain;
    unsigned int last_hit_time;
    unsigned int respawn_time;
    int storm_charges;
    int ability_cooldown;
} PlayerState;

typedef struct {
    int active; unsigned int timestamp;
    float x, y, z;
    float vx, vy, vz;
} LagRecord;

typedef enum { MODE_DEATHMATCH=0, MODE_TDM=1, MODE_SURVIVAL=2, MODE_CTF=3, MODE_ODDBALL=4, MODE_LOCAL=98, MODE_NET=99, MODE_EVOLUTION=100 } GameMode;

typedef struct {
    PlayerState players[MAX_CLIENTS];
    Projectile projectiles[MAX_PROJECTILES];
    LagRecord history[MAX_CLIENTS][LAG_HISTORY];
    int server_tick;
    int game_mode;
    struct sockaddr_in clients[MAX_CLIENTS];
    int client_active[MAX_CLIENTS];
} ServerState;

#endif
