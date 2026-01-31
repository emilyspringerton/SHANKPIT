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
typedef struct { float x, y; } Vec2;

// --- THE BONEYARD (Phase 489: Compressed & Dense) ---
static Box map_geo[] = {
    {0.00, -2.00, 0.00, 800.00, 4.00, 800.00},
    {400.00, 100.00, 0.00, 10.00, 200.00, 800.00},
    {-400.00, 100.00, 0.00, 10.00, 200.00, 800.00},
    {0.00, 100.00, 400.00, 800.00, 200.00, 10.00},
    {0.00, 100.00, -400.00, 800.00, 200.00, 10.00},
    {213.87, 8.96, 200.27, 15.00, 10.00, 15.00},
    {308.74, 7.12, -328.21, 15.00, 10.00, 15.00},
    {-238.16, 2.91, -230.20, 15.00, 10.00, 15.00},
    {92.42, 4.21, 263.73, 15.00, 10.00, 15.00},
    {-4.91, 9.89, 348.78, 15.00, 10.00, 15.00},
    {-150.07, 8.09, -176.99, 15.00, 10.00, 15.00},
    {-306.92, 3.73, 222.71, 15.00, 10.00, 15.00},
    {120.95, 4.60, 18.02, 15.00, 10.00, 15.00},
    {285.46, 9.59, -176.44, 15.00, 10.00, 15.00},
    {52.00, 9.55, 266.13, 15.00, 10.00, 15.00},
    {288.99, 9.24, -144.13, 15.00, 10.00, 15.00},
    {339.68, 3.44, -104.49, 15.00, 10.00, 15.00},
    {-129.92, 2.70, 329.81, 15.00, 10.00, 15.00},
    {-166.15, 6.06, 136.80, 15.00, 10.00, 15.00},
    {-105.63, 3.99, 203.16, 15.00, 10.00, 15.00},
    {-307.50, 2.08, 73.06, 15.00, 10.00, 15.00},
    {147.40, 5.61, -135.57, 15.00, 10.00, 15.00},
    {162.00, 9.95, 344.85, 15.00, 10.00, 15.00},
    {-114.92, 3.17, 146.63, 15.00, 10.00, 15.00},
    {336.06, 5.49, 59.15, 15.00, 10.00, 15.00},
    {-168.92, 3.70, 33.12, 15.00, 10.00, 15.00},
    {-338.01, 8.58, -117.71, 15.00, 10.00, 15.00},
    {-127.03, 5.75, 29.02, 15.00, 10.00, 15.00},
    {128.29, 7.18, -329.66, 15.00, 10.00, 15.00},
    {-18.08, 8.97, 172.50, 15.00, 10.00, 15.00},
    {225.41, 5.36, 170.34, 15.00, 10.00, 15.00},
    {337.22, 4.43, -14.90, 15.00, 10.00, 15.00},
    {-321.12, 8.33, 22.02, 15.00, 10.00, 15.00}
};
static int map_count = sizeof(map_geo) / sizeof(Box);

float phys_rand_f() { return ((float)(rand()%1000)/500.0f) - 1.0f; }

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

int trace_map(float x1, float y1, float z1, float x2, float y2, float z2,
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
    if (y2 < 0.0f) {
        *nx = 0.0f; *ny = 1.0f; *nz = 0.0f;
        *out_x = x2; *out_y = 0.1f; *out_z = z2;
        return 1;
    }
    return 0;
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
    p->storm_charges = 0;
    p->ability_cooldown = 0;
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

void update_weapons(PlayerState *p, PlayerState *targets, Projectile *projectiles, int shoot, int reload, int ability_press) {
    if (p->in_vehicle) return; 
    if (p->reload_timer > 0) p->reload_timer--;
    if (p->attack_cooldown > 0) p->attack_cooldown--;
    if (p->is_shooting > 0) p->is_shooting--;
    if (p->ability_cooldown > 0) p->ability_cooldown--;

    if (ability_press && p->ability_cooldown == 0 && p->storm_charges == 0) {
        p->storm_charges = 5;
        p->ability_cooldown = 480;
    }

    int w = p->current_weapon;
    if (reload && p->reload_timer == 0 && w != WPN_KNIFE) {
        if (p->ammo[w] < WPN_STATS[w].ammo_max) {
            if (p->ammo[w] > 0) p->reload_timer = RELOAD_TIME_TACTICAL;
            else p->reload_timer = RELOAD_TIME_FULL; 
        }
    }
    if (p->reload_timer == 1) p->ammo[w] = WPN_STATS[w].ammo_max;
    if (shoot && p->attack_cooldown == 0 && p->reload_timer == 0) {
        if (p->storm_charges > 0 && w == WPN_SNIPER) {
            int storm_damage = (int)(WPN_STATS[w].dmg * 0.7f);
            spawn_projectile(projectiles, p, storm_damage, 1, 1.5f);
            p->storm_charges--;
            p->attack_cooldown = 8;
            p->recoil_anim = 0.5f;
            return;
        }
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
