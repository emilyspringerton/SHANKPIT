#ifndef PHYSICS_H
#define PHYSICS_H
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include "protocol.h"

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
#define BUGGY_MAX_SPEED 2.5f    
#define BUGGY_ACCEL 0.08f       
#define BUGGY_FRICTION 0.03f    
#define BUGGY_GRAVITY 0.15f     

#define EYE_HEIGHT 2.59f    
#define PLAYER_WIDTH 0.97f  
#define PLAYER_HEIGHT 6.47f 
#define HEAD_SIZE 1.94f     
#define HEAD_OFFSET 2.42f   
#define MELEE_RANGE_SQ 250.0f 

void evolve_bot(PlayerState *loser, PlayerState *winner);
PlayerState* get_best_bot();

typedef struct { float x, y, z, w, h, d; } Box;

// --- THE BONEYARD (Phase 489: Compressed & Dense) ---
static Box map_geo[] = {
    {0.00, -2.00, 0.00, 1500.00, 4.00, 1500.00},
    {0.00, 30.00, 0.00, 40.00, 60.00, 40.00},
    {0.00, 62.00, 0.00, 60.00, 4.00, 60.00},
    {526.69, 32.01, -384.38, 22.98, 27.00, 35.12},
    {509.84, 14.36, -537.03, 20.82, 28.71, 37.77},
    {-346.11, 37.50, -415.62, 12.34, 24.39, 39.66},
    {464.05, 11.00, -362.24, 13.90, 22.01, 10.07},
    {8.55, 4.95, 579.26, 12.30, 9.91, 19.07},
    {-574.14, 42.92, -311.29, 10.49, 26.63, 38.53},
    {145.40, 12.01, -590.30, 15.34, 24.01, 21.10},
    {-547.66, 5.11, -556.99, 14.10, 10.23, 22.42},
    {454.48, 6.87, -124.31, 30.89, 13.74, 29.37},
    {359.63, 9.33, 131.32, 18.39, 18.67, 10.96},
    {494.03, 9.59, -452.03, 17.99, 19.18, 30.70},
    {-212.29, 23.31, -337.79, 26.43, 13.60, 18.45},
    {422.66, 25.27, 203.92, 28.02, 23.31, 38.07},
    {-240.93, 21.74, 490.36, 18.67, 15.92, 34.78},
    {1.18, 20.55, 60.10, 20.39, 8.19, 29.71},
    {12.63, 12.48, -579.33, 34.74, 24.96, 26.82},
    {433.00, 3.74, 461.07, 28.13, 7.49, 26.11},
    {112.22, 10.02, -318.04, 30.17, 20.04, 28.48},
    {391.63, 3.41, 500.27, 38.55, 6.83, 17.44},
    {-420.29, 29.55, -234.60, 21.01, 11.17, 32.00},
    {-263.07, 2.52, -213.72, 16.98, 5.04, 10.22},
    {427.22, 7.66, 436.54, 11.35, 15.32, 39.72},
    {223.56, 14.16, 506.04, 39.80, 28.31, 21.17},
    {347.27, 3.01, 515.05, 37.56, 6.02, 32.38},
    {-400.43, 4.22, 331.00, 27.28, 8.45, 38.31},
    {-278.25, 4.86, -344.64, 12.88, 9.73, 35.03},
    {-56.57, 9.74, 478.42, 22.21, 19.47, 25.45},
    {-147.35, 8.03, -242.80, 26.51, 16.06, 25.58},
    {60.79, 5.74, -10.38, 22.24, 11.48, 17.42},
    {-64.65, 14.33, 195.13, 29.48, 28.67, 38.27},
    {220.06, 25.34, -11.55, 39.33, 18.28, 33.74},
    {413.90, 7.42, 392.86, 25.77, 14.84, 33.91},
    {-477.11, 35.31, -453.18, 35.08, 28.00, 17.00},
    {-132.24, 26.25, -71.04, 38.96, 21.92, 18.36},
    {176.92, 8.51, 225.74, 37.09, 17.02, 17.72},
    {-61.02, 7.12, -492.72, 14.85, 14.24, 11.45},
    {492.10, 6.53, -475.63, 37.37, 13.05, 27.97},
    {-206.36, 3.33, -307.21, 14.06, 6.65, 12.71},
    {105.26, 10.76, -290.45, 29.54, 21.52, 31.89},
    {260.18, 4.61, -98.83, 19.21, 9.22, 29.58},
    {209.14, 11.76, 61.93, 18.46, 23.51, 15.51},
    {-524.76, 10.87, 421.32, 34.48, 21.74, 10.62},
    {-108.22, 3.60, -597.69, 38.44, 7.21, 27.85},
    {-441.38, 13.98, 50.43, 27.73, 27.96, 19.39},
    {-534.81, 13.51, 176.87, 10.11, 27.02, 26.98},
    {-494.15, 8.38, -121.60, 15.92, 16.75, 19.11},
    {-293.42, 12.87, 485.44, 33.87, 25.75, 29.92},
    {-153.51, 8.25, 231.20, 25.69, 16.50, 25.26},
    {438.89, 6.71, -370.68, 37.74, 13.42, 26.42},
    {-483.07, 5.28, -518.88, 34.50, 10.56, 35.00},
    {-197.72, 11.04, 172.50, 21.09, 22.07, 24.49},
    {233.58, 5.62, -529.42, 16.57, 11.24, 12.05},
    {337.38, 2.75, 451.53, 30.71, 5.50, 14.88},
    {224.17, 6.38, 76.38, 22.67, 12.76, 30.08},
    {307.63, 7.71, -201.87, 31.97, 15.42, 19.68},
    {167.81, 9.57, -421.86, 27.87, 19.14, 12.69},
    {549.50, 3.89, 69.38, 31.17, 7.79, 34.61},
    {244.09, 8.64, -173.56, 23.94, 17.27, 10.69},
    {33.06, 20.96, 287.42, 37.56, 10.45, 24.82},
    {356.18, 29.13, -464.15, 15.72, 20.94, 15.83},
    {186.93, 21.07, 250.25, 27.99, 18.09, 38.26},
    {360.65, 3.99, -247.30, 23.17, 7.98, 14.49},
    {219.30, 6.42, -295.57, 24.44, 12.85, 33.34},
    {453.61, 11.36, -189.07, 20.44, 22.72, 25.70},
    {323.19, 4.91, -431.41, 36.44, 9.81, 31.55},
    {-184.72, 30.08, -43.73, 19.72, 24.04, 11.56},
    {427.71, 35.41, 479.67, 25.66, 19.40, 19.74},
    {324.42, 4.18, 149.48, 31.15, 8.35, 16.69},
    {88.23, 10.57, -108.13, 37.00, 21.13, 33.30},
    {18.16, 8.47, -502.13, 18.83, 16.94, 31.05},
    {-397.68, 9.52, -322.93, 30.25, 19.04, 12.76},
    {130.03, 11.01, -512.01, 34.21, 22.03, 14.04},
    {562.59, 3.24, 574.73, 26.31, 6.47, 23.31},
    {371.83, 6.03, -195.98, 18.71, 12.05, 27.58},
    {375.99, 7.57, -230.49, 19.67, 15.13, 28.09},
    {93.83, 12.29, 181.43, 19.71, 24.58, 23.22},
    {110.09, 7.41, 438.73, 17.06, 14.83, 26.15},
    {122.64, 4.04, 257.63, 26.92, 8.09, 12.67},
    {-18.74, 11.35, 587.79, 25.35, 22.71, 10.66},
    {-106.00, 11.29, 291.31, 31.58, 22.57, 35.33},
    {-374.93, 3.67, 412.91, 32.46, 7.34, 16.82},
    {161.53, 6.49, 391.35, 22.05, 12.98, 13.12},
    {387.90, 7.52, 392.45, 16.02, 15.05, 17.41},
    {-322.69, 12.34, 57.01, 16.16, 24.68, 39.06},
    {217.06, 5.52, 285.29, 16.52, 11.04, 18.84},
    {435.89, 11.11, 143.86, 29.80, 22.23, 30.94},
    {593.43, 9.52, 30.93, 19.15, 19.04, 24.22},
    {-81.53, 5.25, 514.68, 15.55, 10.50, 35.70},
    {-380.57, 11.47, -152.16, 34.85, 22.94, 28.21},
    {-358.89, 31.63, -574.79, 12.82, 28.14, 12.80},
    {181.59, 6.33, -523.91, 26.57, 12.65, 37.93},
    {-56.12, 11.02, 57.28, 39.08, 22.04, 34.08},
    {-135.87, 3.84, -437.60, 26.21, 7.69, 16.66},
    {-467.90, 15.72, -251.73, 17.38, 8.87, 14.61},
    {201.21, 13.14, -419.70, 14.27, 26.28, 29.15},
    {568.33, 3.95, -235.04, 32.35, 7.91, 24.38},
    {86.46, 14.45, 210.02, 15.78, 28.89, 36.55},
    {-544.24, 6.40, 578.36, 33.20, 12.80, 14.74},
    {450.27, 5.61, -407.15, 36.01, 11.23, 20.23},
    {520.70, 5.95, -396.17, 10.37, 11.90, 22.21},
    {80.00, 10.00, 0.00, 15.00, 2.00, 15.00},
    {60.10, 13.00, 60.10, 15.00, 2.00, 15.00},
    {0.00, 16.00, 90.00, 15.00, 2.00, 15.00},
    {-67.18, 19.00, 67.18, 15.00, 2.00, 15.00},
    {-100.00, 22.00, 0.00, 15.00, 2.00, 15.00},
    {-74.25, 25.00, -74.25, 15.00, 2.00, 15.00},
    {-0.00, 28.00, -110.00, 15.00, 2.00, 15.00},
    {81.32, 31.00, -81.32, 15.00, 2.00, 15.00},
    {120.00, 34.00, -0.00, 15.00, 2.00, 15.00},
    {88.39, 37.00, 88.39, 15.00, 2.00, 15.00},
    {0.00, 40.00, 130.00, 15.00, 2.00, 15.00},
    {-95.46, 43.00, 95.46, 15.00, 2.00, 15.00},
    {-140.00, 46.00, 0.00, 15.00, 2.00, 15.00},
    {-102.53, 49.00, -102.53, 15.00, 2.00, 15.00},
    {-0.00, 52.00, -150.00, 15.00, 2.00, 15.00},
    {109.60, 55.00, -109.60, 15.00, 2.00, 15.00},
    {160.00, 58.00, -0.00, 15.00, 2.00, 15.00},
    {116.67, 61.00, 116.67, 15.00, 2.00, 15.00},
    {0.00, 64.00, 170.00, 15.00, 2.00, 15.00},
    {-123.74, 67.00, 123.74, 15.00, 2.00, 15.00}
};
static int map_count = sizeof(map_geo) / sizeof(Box);

float phys_rand_f() { return ((float)(rand()%1000)/500.0f) - 1.0f; }

static inline float angle_diff(float a, float b) {
    float d = a - b;
    while (d < -180) d += 360;
    while (d > 180) d -= 360;
    return d;
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

void apply_friction(PlayerState *p) {
    float speed = sqrtf(p->vx*p->vx + p->vz*p->vz);
    if (speed < 0.001f) { p->vx = 0; p->vz = 0; return; }
    
    float drop = 0;
    if (p->in_vehicle) {
        drop = speed * BUGGY_FRICTION;
    } 
    else if (p->on_ground) {
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

void accelerate(PlayerState *p, float wish_x, float wish_z, float wish_speed, float accel) {
    if (p->in_vehicle) {
        float current_speed = (p->vx * wish_x) + (p->vz * wish_z);
        float add_speed = wish_speed - current_speed;
        if (add_speed <= 0) return;
        float acc_speed = accel * BUGGY_MAX_SPEED;
        if (acc_speed > add_speed) acc_speed = add_speed;
        p->vx += acc_speed * wish_x; p->vz += acc_speed * wish_z;
        return;
    }
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
    if (p->y < 0) { p->y = 0; p->vy = 0; p->on_ground = 1; }
    for(int i=1; i<map_count; i++) {
        Box b = map_geo[i];
        if (p->x + pw > b.x - b.w/2 && p->x - pw < b.x + b.w/2 &&
            p->z + pw > b.z - b.d/2 && p->z - pw < b.z + b.d/2) {
            if (p->y < b.y + b.h/2 && p->y + ph > b.y - b.h/2) {
                float prev_y = p->y - p->vy;
                if (prev_y >= b.y + b.h/2) {
                    p->y = b.y + b.h/2; p->vy = 0; p->on_ground = 1;
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
    if (rand()%2 == 0) { p->x = 0; p->z = 0; p->y = 80; } 
    else { float ang = phys_rand_f() * 6.28f;
        p->x = sinf(ang) * 500; p->z = cosf(ang) * 500; p->y = 20;
    }
    p->current_weapon = WPN_MAGNUM;
    for(int i=0; i<MAX_WEAPONS; i++) p->ammo[i] = WPN_STATS[i].ammo_max;
    if (p->is_bot) {
        PlayerState *winner = get_best_bot();
        if (winner && winner != p) evolve_bot(p, winner);
    }
}

void update_weapons(PlayerState *p, PlayerState *targets, int shoot, int reload) {
    if (p->in_vehicle) return; 
    if (p->reload_timer > 0) p->reload_timer--;
    if (p->attack_cooldown > 0) p->attack_cooldown--;
    if (p->is_shooting > 0) p->is_shooting--;

    int w = p->current_weapon;
    if (reload && p->reload_timer == 0 && w != WPN_KNIFE) {
        if (p->ammo[w] < WPN_STATS[w].ammo_max) {
            if (p->ammo[w] > 0) p->reload_timer = RELOAD_TIME_TACTICAL;
            else p->reload_timer = RELOAD_TIME_FULL; 
        }
    }
    if (p->reload_timer == 1) p->ammo[w] = WPN_STATS[w].ammo_max;
    if (shoot && p->attack_cooldown == 0 && p->reload_timer == 0) {
        if (w != WPN_KNIFE && p->ammo[w] <= 0) p->reload_timer = RELOAD_TIME_FULL;
        else {
            p->is_shooting = 5; p->recoil_anim = 1.0f;
            p->attack_cooldown = WPN_STATS[w].rof;
            if (w != WPN_KNIFE) p->ammo[w]--;
            
            float r = -p->yaw * 0.0174533f; float rp = p->pitch * 0.0174533f;
            float dx = sinf(r) * cosf(rp); float dy = sinf(rp); float dz = -cosf(r) * cosf(rp);
            if (WPN_STATS[w].spr > 0) {
                dx += phys_rand_f() * WPN_STATS[w].spr;
                dy += phys_rand_f() * WPN_STATS[w].spr;
                dz += phys_rand_f() * WPN_STATS[w].spr;
            }

            for(int i=0; i<MAX_CLIENTS; i++) {
                if (p == &targets[i]) continue;
                if (!targets[i].active) continue;
                if (w == WPN_KNIFE) {
                    float kx = p->x - targets[i].x;
                    float ky = p->y - targets[i].y; float kz = p->z - targets[i].z;
                    if ((kx*kx + ky*ky + kz*kz) > MELEE_RANGE_SQ + 22.0f ) continue;
                }
                int hit_type = check_hit_location(p->x, p->y + EYE_HEIGHT, p->z, dx, dy, dz, &targets[i]);
                if (hit_type > 0) {
                    printf("🔫 HIT! Dmg: %d on Target %d\n", WPN_STATS[w].dmg, i);
                    int damage = WPN_STATS[w].dmg;
                    p->accumulated_reward += 10.0f;
                    targets[i].shield_regen_timer = SHIELD_REGEN_DELAY;
                    if (hit_type == 2 && targets[i].shield <= 0) { damage *= 3; p->hit_feedback = 20;
                    } else { p->hit_feedback = 10; } 
                    
                    if (targets[i].shield > 0) {
                        if (targets[i].shield >= damage) { targets[i].shield -= damage; damage = 0; } 
                        else { damage -= targets[i].shield; targets[i].shield = 0; }
                    }
                    targets[i].health -= damage;
                    if(targets[i].health <= 0) {
                        p->kills++; targets[i].deaths++; 
                        p->accumulated_reward += 1000.0f;
                        p->hit_feedback = 30; // KILL CONFIRM (Triggers Double Ring)
                        phys_respawn(&targets[i], 0);
                    }
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
