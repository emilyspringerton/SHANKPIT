#define SDL_MAIN_HANDLED
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <netdb.h>
#endif

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <GL/glu.h>

#include "../../../packages/common/protocol.h"
#include "../../../packages/common/physics.h"
#include "../../../packages/simulation/local_game.h"

#define STATE_LOBBY 0
#define STATE_GAME_NET 1
#define STATE_GAME_LOCAL 2
#define STATE_LISTEN_SERVER 99

char SERVER_HOST[64] = "s.farthq.com";
int SERVER_PORT = 6969;

int app_state = STATE_LOBBY;
int wpn_req = 1; 
int my_client_id = -1;

float cam_yaw = 0.0f;
float cam_pitch = 0.0f;
float current_fov = 75.0f;

#define Z_FAR 8000.0f

int sock = -1;
struct sockaddr_in server_addr;

void draw_char(char c, float x, float y, float s) {
    glLineWidth(2.0f); glBegin(GL_LINES); // Thicker text for Cyberpunk feel
    if(c>='0'&&c<='9'){ glVertex2f(x,y+s);glVertex2f(x+s,y+s);glVertex2f(x+s,y);glVertex2f(x,y);glVertex2f(x,y+s); }
    else if(c=='A'){glVertex2f(x,y);glVertex2f(x,y+s/2);glVertex2f(x,y+s/2);glVertex2f(x+s,y+s/2);glVertex2f(x+s,y+s/2);glVertex2f(x+s,y);glVertex2f(x,y+s/2);glVertex2f(x+s/2,y+s);glVertex2f(x+s/2,y+s);glVertex2f(x+s,y+s/2);}
    else if(c=='B'){glVertex2f(x,y);glVertex2f(x,y+s);glVertex2f(x,y+s);glVertex2f(x+s*0.8,y+s);glVertex2f(x+s*0.8,y+s);glVertex2f(x+s,y+s*0.75);glVertex2f(x+s,y+s*0.75);glVertex2f(x+s*0.8,y+s/2);glVertex2f(x+s*0.8,y+s/2);glVertex2f(x,y+s/2);glVertex2f(x,y+s/2);glVertex2f(x+s*0.8,y+s/2);glVertex2f(x+s*0.8,y+s/2);glVertex2f(x+s,y+s/4);glVertex2f(x+s,y+s/4);glVertex2f(x+s*0.8,y);glVertex2f(x+s*0.8,y);glVertex2f(x,y);}
    else if(c=='D'){glVertex2f(x,y);glVertex2f(x,y+s);glVertex2f(x,y+s);glVertex2f(x+s*0.8,y+s);glVertex2f(x+s*0.8,y+s);glVertex2f(x+s,y+s/2);glVertex2f(x+s,y+s/2);glVertex2f(x+s*0.8,y+s);glVertex2f(x+s*0.8,y+s);glVertex2f(x,y);}
    else if(c=='J'){glVertex2f(x+s,y+s);glVertex2f(x+s,y);glVertex2f(x+s,y);glVertex2f(x,y);glVertex2f(x,y);glVertex2f(x,y+s/2);}
    else if(c=='O'){glVertex2f(x,y);glVertex2f(x+s,y);glVertex2f(x+s,y);glVertex2f(x+s,y+s);glVertex2f(x+s,y+s);glVertex2f(x,y+s);glVertex2f(x,y+s);glVertex2f(x,y);}
    else if(c=='I'){glVertex2f(x+s/2,y);glVertex2f(x+s/2,y+s);glVertex2f(x,y);glVertex2f(x+s,y);glVertex2f(x,y+s);glVertex2f(x+s,y+s);}
    else if(c=='N'){glVertex2f(x,y);glVertex2f(x,y+s);glVertex2f(x,y+s);glVertex2f(x+s,y);glVertex2f(x+s,y);glVertex2f(x+s,y+s);}
    else if(c=='S'){glVertex2f(x+s,y+s);glVertex2f(x,y+s);glVertex2f(x,y+s);glVertex2f(x,y+s/2);glVertex2f(x,y+s/2);glVertex2f(x+s,y+s/2);glVertex2f(x+s,y+s/2);glVertex2f(x+s,y);glVertex2f(x+s,y);glVertex2f(x,y);}
    else if(c=='E'){glVertex2f(x+s,y);glVertex2f(x,y);glVertex2f(x,y);glVertex2f(x,y+s);glVertex2f(x,y+s);glVertex2f(x+s,y+s);glVertex2f(x,y+s/2);glVertex2f(x+s*0.8,y+s/2);}
    else if(c=='R'){glVertex2f(x,y);glVertex2f(x,y+s);glVertex2f(x,y+s);glVertex2f(x+s,y+s);glVertex2f(x+s,y+s);glVertex2f(x+s,y+s/2);glVertex2f(x+s,y+s/2);glVertex2f(x,y+s/2);glVertex2f(x,y+s/2);glVertex2f(x+s,y);}
    else if(c=='V'){glVertex2f(x,y+s);glVertex2f(x+s/2,y);glVertex2f(x+s/2,y);glVertex2f(x+s,y+s);}
    else if(c==' '){} 
    else { glVertex2f(x,y);glVertex2f(x+s,y);glVertex2f(x+s,y);glVertex2f(x+s,y+s);glVertex2f(x+s,y+s);glVertex2f(x,y+s);glVertex2f(x,y+s);glVertex2f(x,y); }
    glEnd();
}
void draw_string(const char* str, float x, float y, float size) { while(*str) { draw_char(*str, x, y, size); x += size * 1.5f; str++; } }

#define MAX_TRAILS 4096 
#define GRID_SIZE 50.0f
typedef struct { int cx, cz; float life; } Trail;
Trail trails[MAX_TRAILS];
int trail_head = 0;

void add_trail(int x, int z) {
    int prev = (trail_head - 1 + MAX_TRAILS) % MAX_TRAILS;
    if (trails[prev].cx == x && trails[prev].cz == z && trails[prev].life > 0.9f) return;
    trails[trail_head].cx = x; trails[trail_head].cz = z;
    trails[trail_head].life = 1.0f;
    trail_head = (trail_head + 1) % MAX_TRAILS;
}

void update_and_draw_trails() {
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for(int i=0; i<MAX_CLIENTS; i++) {
        PlayerState *p = &local_state.players[i];
        if (p->active && p->on_ground) {
            int gx = (int)floorf(p->x / GRID_SIZE) * (int)GRID_SIZE + (int)(GRID_SIZE/2);
            int gz = (int)floorf(p->z / GRID_SIZE) * (int)GRID_SIZE + (int)(GRID_SIZE/2);
            add_trail(gx, gz);
        }
    }
    glLineWidth(2.0f);
    for(int i=0; i<MAX_TRAILS; i++) {
        if (trails[i].life > 0) {
            float s = (GRID_SIZE / 2.0f) - 4.0f;
            // Trails are now HOT PINK
            glColor4f(1.0f, 0.0f, 0.8f, trails[i].life); 
            glBegin(GL_LINE_LOOP);
            glVertex3f(trails[i].cx - s, 0.2f, trails[i].cz - s);
            glVertex3f(trails[i].cx + s, 0.2f, trails[i].cz - s);
            glVertex3f(trails[i].cx + s, 0.2f, trails[i].cz + s);
            glVertex3f(trails[i].cx - s, 0.2f, trails[i].cz + s);
            glEnd();
            trails[i].life -= 0.02f;
        }
    }
    glDisable(GL_BLEND);
}

void draw_grid() {
    glLineWidth(1.0f); glBegin(GL_LINES); 
    // THE MATRIX FLOOR (Cyan)
    glColor3f(0.0f, 1.0f, 1.0f); 
    for(int i=-4000; i<=4000; i+=50) { 
        glVertex3f(i, 0.1f, -4000); glVertex3f(i, 0.1f, 4000);
        glVertex3f(-4000, 0.1f, i); glVertex3f(4000, 0.1f, i); 
    }
    glEnd();
}

// --- NEON BRUTALIST BLOCK RENDERER ---
void draw_map() {
    // Enable blending for that glassy look if we wanted, but solid matte is cleaner for now
    // glDisable(GL_BLEND);
    
    for(int i=1; i<map_count; i++) {
        Box b = map_geo[i];
        
        // 1. PROCEDURAL NEON COLOR
        // Generate a color based on world position. 
        // This creates gradients across the city.
        float nr = 0.5f + 0.5f * sinf(b.x * 0.005f + b.y * 0.01f);
        float ng = 0.5f + 0.5f * sinf(b.z * 0.005f + 2.0f);
        float nb = 0.5f + 0.5f * sinf(b.x * 0.005f + 4.0f);
        
        // Boost brightness for the "Neon" effect
        if(nr > 0.8f) nr = 1.0f;
        if(ng > 0.8f) ng = 1.0f;
        if(nb > 0.8f) nb = 1.0f;

        glPushMatrix(); 
        glTranslatef(b.x, b.y, b.z); 
        glScalef(b.w, b.h, b.d);
        
        // 2. THE SOLID CORE (Deep Matte Black)
        // We render the faces dark so the edges pop.
        glBegin(GL_QUADS); 
        glColor3f(0.02f, 0.02f, 0.02f); // Almost black
        
        // Top
        glVertex3f(-0.5,0.5,0.5); glVertex3f(0.5,0.5,0.5); glVertex3f(0.5,0.5,-0.5); glVertex3f(-0.5,0.5,-0.5);
        // Bottom
        glVertex3f(-0.5,-0.5,0.5); glVertex3f(0.5,-0.5,0.5); glVertex3f(0.5,-0.5,-0.5); glVertex3f(-0.5,-0.5,-0.5);
        // Front
        glVertex3f(-0.5,-0.5,0.5); glVertex3f(0.5,-0.5,0.5); glVertex3f(0.5,0.5,0.5); glVertex3f(-0.5,0.5,0.5);
        // Back
        glVertex3f(-0.5,-0.5,-0.5); glVertex3f(0.5,-0.5,-0.5); glVertex3f(0.5,0.5,-0.5); glVertex3f(-0.5,0.5,-0.5);
        // Left
        glVertex3f(-0.5,-0.5,-0.5); glVertex3f(-0.5,-0.5,0.5); glVertex3f(-0.5,0.5,0.5); glVertex3f(-0.5,0.5,-0.5);
        // Right
        glVertex3f(0.5,-0.5,0.5); glVertex3f(0.5,-0.5,-0.5); glVertex3f(0.5,0.5,-0.5); glVertex3f(0.5,0.5,0.5);
        glEnd();
        
        // 3. THE NEON CAGE (Wireframe)
        glLineWidth(2.0f); 
        glColor3f(nr, ng, nb); 
        
        // Using LINE_LOOP for top/bottom and LINES for pillars is efficient enough
        // Top Loop
        glBegin(GL_LINE_LOOP);
        glVertex3f(-0.5, 0.5, 0.5); glVertex3f(0.5, 0.5, 0.5); glVertex3f(0.5, 0.5, -0.5); glVertex3f(-0.5, 0.5, -0.5);
        glEnd();
        
        // Bottom Loop
        glBegin(GL_LINE_LOOP);
        glVertex3f(-0.5, -0.5, 0.5); glVertex3f(0.5, -0.5, 0.5); glVertex3f(0.5, -0.5, -0.5); glVertex3f(-0.5, -0.5, -0.5);
        glEnd();
        
        // Vertical Pillars
        glBegin(GL_LINES);
        glVertex3f(-0.5, -0.5, 0.5); glVertex3f(-0.5, 0.5, 0.5);
        glVertex3f(0.5, -0.5, 0.5); glVertex3f(0.5, 0.5, 0.5);
        glVertex3f(0.5, -0.5, -0.5); glVertex3f(0.5, 0.5, -0.5);
        glVertex3f(-0.5, -0.5, -0.5); glVertex3f(-0.5, 0.5, -0.5);
        glEnd();

        glPopMatrix();
    }
}

void draw_buggy_model() {
    // Chassis - Cyber Grey
    glColor3f(0.2f, 0.2f, 0.2f);
    glPushMatrix(); glScalef(2.0f, 1.0f, 3.5f); 
    glBegin(GL_QUADS); 
    glVertex3f(-1,1,1); glVertex3f(1,1,1); glVertex3f(1,1,-1); glVertex3f(-1,1,-1); 
    glVertex3f(-1,-1,1); glVertex3f(1,-1,1); glVertex3f(1,1,1); glVertex3f(-1,1,1); 
    glVertex3f(-1,-1,-1);
    glVertex3f(-1,1,-1); glVertex3f(1,1,-1); glVertex3f(1,-1,-1); 
    glVertex3f(1,-1,-1); glVertex3f(1,1,-1); glVertex3f(1,1,1); glVertex3f(1,-1,1); 
    glVertex3f(-1,-1,1); glVertex3f(-1,1,1); glVertex3f(-1,1,-1); glVertex3f(-1,-1,-1); 
    glEnd(); 
    
    // Neon Trim for Buggy
    glLineWidth(2.0f); glColor3f(1.0f, 0.0f, 0.0f); // Red Trim
    glBegin(GL_LINES);
    glVertex3f(-1,1,1); glVertex3f(1,1,1);
    glVertex3f(1,1,1); glVertex3f(1,1,-1);
    glVertex3f(1,1,-1); glVertex3f(-1,1,-1);
    glVertex3f(-1,1,-1); glVertex3f(-1,1,1);
    glEnd();
    
    glPopMatrix();
    
    // Wheels - Neon Blue Rims
    glColor3f(0.1f, 0.1f, 0.1f);
    float wx[] = {-2.2, 2.2, -2.2, 2.2};
    float wz[] = {2.5, 2.5, -2.5, -2.5};
    for(int i=0; i<4; i++) {
        glPushMatrix();
        glTranslatef(wx[i], -0.5f, wz[i]); glScalef(0.8f, 1.5f, 1.5f);
        glBegin(GL_QUADS); 
        glColor3f(0.1f, 0.1f, 0.1f); // Tire
        glVertex3f(-0.5,0.5,0.5); glVertex3f(0.5,0.5,0.5); glVertex3f(0.5,0.5,-0.5); glVertex3f(-0.5,0.5,-0.5);
        glVertex3f(-0.5,-0.5,0.5); glVertex3f(0.5,-0.5,0.5); glVertex3f(0.5,0.5,-0.5); glVertex3f(-0.5,0.5,-0.5);
        glVertex3f(-0.5,-0.5,-0.5); glVertex3f(-0.5,0.5,-0.5); glVertex3f(0.5,0.5,-0.5); glVertex3f(0.5,-0.5,-0.5);
        glVertex3f(0.5,-0.5,-0.5);
        glVertex3f(0.5,0.5,-0.5); glVertex3f(0.5,0.5,0.5); glVertex3f(0.5,-0.5,0.5);
        glVertex3f(-0.5,-0.5,0.5); glVertex3f(-0.5,0.5,0.5); glVertex3f(-0.5,0.5,-0.5); glVertex3f(-0.5,-0.5,-0.5);
        glVertex3f(0.5,0.5,0.5); glVertex3f(0.5,-0.5,0.5); glVertex3f(-0.5,-0.5,0.5); glVertex3f(-0.5,0.5,0.5);
        glEnd(); 
        
        // Rim Line
        glLineWidth(2.0f); glColor3f(0.0f, 1.0f, 1.0f); 
        glBegin(GL_LINE_LOOP);
        glVertex3f(-0.51, 0.5, 0.5); glVertex3f(-0.51, -0.5, 0.5); glVertex3f(-0.51, -0.5, -0.5); glVertex3f(-0.51, 0.5, -0.5);
        glEnd();
        
        glPopMatrix();
    }
}

void draw_gun_model(int weapon_id) {
    switch(weapon_id) {
        case WPN_KNIFE:   glColor3f(0.8f, 0.8f, 0.9f); glScalef(0.05f, 0.05f, 0.8f); break;
        case WPN_MAGNUM:  glColor3f(0.4f, 0.4f, 0.4f); glScalef(0.15f, 0.2f, 0.5f); break;
        case WPN_AR:      glColor3f(0.2f, 0.3f, 0.2f); glScalef(0.1f, 0.15f, 1.2f); break;
        case WPN_SHOTGUN: glColor3f(0.5f, 0.3f, 0.2f); glScalef(0.25f, 0.15f, 0.8f); break;
        case WPN_SNIPER:  glColor3f(0.1f, 0.1f, 0.15f); glScalef(0.08f, 0.12f, 2.0f); break;
    }
    glBegin(GL_QUADS); 
    glVertex3f(-1,1,1);
    glVertex3f(1,1,1); glVertex3f(1,1,-1); glVertex3f(-1,1,-1); 
    glVertex3f(-1,-1,1); glVertex3f(1,-1,1); glVertex3f(1,1,1); glVertex3f(-1,1,1); 
    glVertex3f(-1,-1,-1); glVertex3f(-1,1,-1); glVertex3f(1,1,-1); glVertex3f(1,-1,-1); 
    glVertex3f(1,-1,-1); glVertex3f(1,1,-1); glVertex3f(1,1,1); glVertex3f(1,-1,1); 
    glVertex3f(-1,-1,1); glVertex3f(-1,1,1); glVertex3f(-1,1,-1); glVertex3f(-1,-1,-1); 
    glEnd();
    
    // Wireframe Gun
    glLineWidth(1.0f); glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_LINES);
    glVertex3f(-1,1,1); glVertex3f(1,1,1);
    glVertex3f(1,1,1); glVertex3f(1,1,-1);
    glVertex3f(-1,1,1); glVertex3f(-1,-1,1);
    glEnd();
}

void draw_weapon_p(PlayerState *p) {
    if (p->in_vehicle) return; 
    glPushMatrix();
    glLoadIdentity();
    float kick = p->recoil_anim * 0.2f;
    float reload_dip = (p->reload_timer > 0) ? sinf(p->reload_timer * 0.2f) * 0.5f - 0.5f : 0.0f;
    float speed = sqrtf(p->vx*p->vx + p->vz*p->vz);
    float bob = sinf(SDL_GetTicks() * 0.015f) * speed * 0.15f; 
    float x_offset = (current_fov < 50.0f) ? 0.25f : 0.4f;
    glTranslatef(x_offset, -0.5f + kick + reload_dip + (bob * 0.5f), -1.2f + (kick * 0.5f) + bob);
    glRotatef(-p->recoil_anim * 10.0f, 1, 0, 0);
    draw_gun_model(p->current_weapon);
    glPopMatrix();
}

void draw_head(int weapon_id) {
    switch(weapon_id) {
        case WPN_KNIFE:   glColor3f(0.8f, 0.8f, 0.9f); break;
        case WPN_MAGNUM:  glColor3f(0.4f, 0.4f, 0.4f); break;
        case WPN_AR:      glColor3f(0.2f, 0.3f, 0.2f); break;
        case WPN_SHOTGUN: glColor3f(0.5f, 0.3f, 0.2f); break;
        case WPN_SNIPER:  glColor3f(0.1f, 0.1f, 0.15f); break;
    }
    glBegin(GL_QUADS);
    glVertex3f(-0.4, 0.8, 0.4); glVertex3f(0.4, 0.8, 0.4); glVertex3f(0.4, 0, 0.4); glVertex3f(-0.4, 0, 0.4);
    glVertex3f(-0.4, 0.8, -0.4); glVertex3f(0.4, 0.8, -0.4);
    glVertex3f(0.4, 0, -0.4); glVertex3f(-0.4, 0, -0.4);
    glVertex3f(-0.4, 0.8, 0.4); glVertex3f(0.4, 0.8, 0.4); glVertex3f(0.4, 0.8, -0.4); glVertex3f(-0.4, 0.8, -0.4);
    glVertex3f(-0.4, 0, 0.4); glVertex3f(0.4, 0, 0.4); glVertex3f(0.4, 0, -0.4); glVertex3f(-0.4, 0, -0.4);
    glVertex3f(-0.4, 0.8, 0.4); glVertex3f(-0.4, 0, 0.4);
    glVertex3f(-0.4, 0, -0.4); glVertex3f(-0.4, 0.8, -0.4);
    glVertex3f(0.4, 0.8, 0.4); glVertex3f(0.4, 0, 0.4); glVertex3f(0.4, 0, -0.4); glVertex3f(0.4, 0.8, -0.4);
    glEnd();
}

void draw_player_3rd(PlayerState *p) {
    glPushMatrix();
    glTranslatef(p->x, p->y + 2.0f, p->z);
    glRotatef(-p->yaw, 0, 1, 0);
    if (p->in_vehicle) {
        draw_buggy_model();
    } else {
        if(p->health <= 0) glColor3f(0.2, 0, 0); else glColor3f(1, 0, 0);
        glPushMatrix(); glScalef(0.97f, 2.91f, 0.97f); 
        glBegin(GL_QUADS);
        glVertex3f(-0.5,-0.5,0.5); glVertex3f(0.5,-0.5,0.5); glVertex3f(0.5,0.5,0.5); glVertex3f(-0.5,0.5,0.5);
        glVertex3f(-0.5,0.5,0.5); glVertex3f(0.5,0.5,0.5); glVertex3f(0.5,0.5,-0.5); glVertex3f(-0.5,0.5,-0.5);
        glVertex3f(-0.5,-0.5,-0.5); glVertex3f(0.5,-0.5,-0.5); glVertex3f(0.5,0.5,-0.5); glVertex3f(-0.5,0.5,-0.5);
        glVertex3f(-0.5,-0.5,-0.5); glVertex3f(-0.5,-0.5,0.5); glVertex3f(-0.5,0.5,0.5);
        glVertex3f(-0.5,0.5,-0.5);
        glVertex3f(0.5,-0.5,0.5); glVertex3f(0.5,-0.5,-0.5); glVertex3f(0.5,0.5,-0.5); glVertex3f(0.5,0.5,0.5);
        glEnd(); glPopMatrix();
        glPushMatrix(); glTranslatef(0, 1.54f, 0); draw_head(p->current_weapon); glPopMatrix();
        glPushMatrix(); glTranslatef(0.5f, 1.0f, 0.5f);
        glRotatef(p->pitch, 1, 0, 0);   
        glScalef(0.8f, 0.8f, 0.8f); draw_gun_model(p->current_weapon); glPopMatrix(); 
    }
    glPopMatrix();
}

// --- NEW HELPER: Wireframe Circle ---
void draw_circle(float x, float y, float r, int segments) {
    glBegin(GL_LINE_LOOP);
    for(int i=0; i<segments; i++) {
        float theta = 2.0f * 3.1415926f * (float)i / (float)segments;
        float cx = r * cosf(theta);
        float cy = r * sinf(theta);
        glVertex2f(x + cx, y + cy);
    }
    glEnd();
}

void draw_hud(PlayerState *p) {
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); gluOrtho2D(0, 1280, 0, 720);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glColor3f(0, 1, 0);
    if (current_fov < 50.0f) { glBegin(GL_LINES); glVertex2f(0, 360); glVertex2f(1280, 360); glVertex2f(640, 0); glVertex2f(640, 720); glEnd(); } 
    else { glLineWidth(2.0f); glBegin(GL_LINES); glVertex2f(630, 360); glVertex2f(650, 360); glVertex2f(640, 350); glVertex2f(640, 370); glEnd(); }
    
    // --- HIT INDICATORS ---
    if (p->hit_feedback > 0) {
        if (p->hit_feedback >= 25) glColor3f(1.0f, 0.0f, 0.0f); // RED (Kill/High Dmg)
        else glColor3f(0.0f, 1.0f, 0.0f); // GREEN (Normal)
        
        glLineWidth(2.0f);
        draw_circle(640, 360, 20.0f, 16); // Hit Ring
        
        // DOUBLE RING FOR KILL
        if (p->hit_feedback >= 25) {
            draw_circle(640, 360, 28.0f, 16); // Outer Kill Ring
        }
    }

    glColor3f(0.2f, 0, 0); glRectf(50, 50, 250, 70); glColor3f(1.0f, 0, 0);
    glRectf(50, 50, 50 + (p->health * 2), 70);
    glColor3f(0, 0, 0.2f); glRectf(50, 80, 250, 100); glColor3f(0.2f, 0.2f, 1.0f);
    glRectf(50, 80, 50 + (p->shield * 2), 100);
    
    if (p->in_vehicle) {
        glColor3f(0.0f, 1.0f, 0.0f);
        draw_string("BUGGY ONLINE", 50, 120, 12);
    }
    
    float raw_speed = sqrtf(p->vx*p->vx + p->vz*p->vz);
    char vel_buf[32]; sprintf(vel_buf, "VEL: %.2f", raw_speed);
    glColor3f(1.0f, 1.0f, 0.0f); draw_string(vel_buf, 1100, 50, 8); 

    glEnable(GL_DEPTH_TEST); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW); glPopMatrix();
}

void draw_scene(PlayerState *render_p) {
    glClearColor(0.02f, 0.02f, 0.05f, 1.0f); // DEEP SPACE BLUE BG
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); glLoadIdentity();
    float cam_y = render_p->in_vehicle ? 8.0f : (render_p->crouching ? 2.5f : EYE_HEIGHT);
    float cam_z_off = render_p->in_vehicle ? 10.0f : 0.0f;
    
    float rad = -cam_yaw * 0.01745f;
    float cx = sinf(rad) * cam_z_off;
    float cz = cosf(rad) * cam_z_off;
    
    glRotatef(-cam_pitch, 1, 0, 0); glRotatef(-cam_yaw, 0, 1, 0);
    glTranslatef(-(render_p->x - cx), -(render_p->y + cam_y), -(render_p->z - cz));
    
    draw_grid(); 
    update_and_draw_trails();
    draw_map();
    if (render_p->in_vehicle) draw_player_3rd(render_p);
    for(int i=1; i<MAX_CLIENTS; i++) if(local_state.players[i].active && i != my_client_id) draw_player_3rd(&local_state.players[i]);
    draw_weapon_p(render_p); draw_hud(render_p);
}

void net_init() {
    #ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
    #endif
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    #ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
    #else
    int flags = fcntl(sock, F_GETFL, 0); fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    #endif
}

void net_connect() {
    struct hostent *he = gethostbyname(SERVER_HOST);
    if (he) {
        server_addr.sin_family = AF_INET; 
        server_addr.sin_port = htons(SERVER_PORT); 
        memcpy(&server_addr.sin_addr, he->h_addr_list[0], he->h_length);
        char buffer[128];
        NetHeader *h = (NetHeader*)buffer;
        h->type = PACKET_CONNECT;
        sendto(sock, buffer, sizeof(NetHeader), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
        printf("Connected to %s...\n", SERVER_HOST);
    } else {
        printf("Failed to resolve %s\n", SERVER_HOST);
    }
}

UserCmd client_create_cmd(float fwd, float str, float yaw, float pitch, int shoot, int jump, int crouch, int reload, int use, int wpn_idx) {
    UserCmd cmd;
    memset(&cmd, 0, sizeof(UserCmd));
    static int seq = 0; cmd.sequence = ++seq; cmd.timestamp = SDL_GetTicks();
    cmd.yaw = yaw; cmd.pitch = pitch;
    cmd.fwd = fwd; cmd.str = str;
    if(shoot) cmd.buttons |= BTN_ATTACK; if(jump) cmd.buttons |= BTN_JUMP;
    if(crouch) cmd.buttons |= BTN_CROUCH;
    if(reload) cmd.buttons |= BTN_RELOAD;
    if(use) cmd.buttons |= BTN_USE;
    cmd.weapon_idx = wpn_idx; return cmd;
}

void net_send_cmd(UserCmd cmd) {
    char packet_data[256];
    int cursor = 0;
    NetHeader head; head.type = PACKET_USERCMD;
    head.client_id = 0; 
    memcpy(packet_data + cursor, &head, sizeof(NetHeader)); cursor += sizeof(NetHeader);
    unsigned char count = 1;
    memcpy(packet_data + cursor, &count, 1); cursor += 1;
    memcpy(packet_data + cursor, &cmd, sizeof(UserCmd)); cursor += sizeof(UserCmd);
    sendto(sock, packet_data, cursor, 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
}

void net_process_snapshot(char *buffer, int len) {
    int cursor = sizeof(NetHeader);
    unsigned char count = *(unsigned char*)(buffer + cursor); cursor++;
    
    for(int i=0; i<count; i++) {
        NetPlayer *np = (NetPlayer*)(buffer + cursor);
        cursor += sizeof(NetPlayer);
        
        int id = np->id;
        if (id > 0 && id < MAX_CLIENTS) {
            PlayerState *p = &local_state.players[id];
            p->active = 1;
            p->x = np->x; p->y = np->y; p->z = np->z;
            p->yaw = np->yaw; p->pitch = np->pitch;
            p->health = np->health;
            p->current_weapon = np->current_weapon;
            p->is_shooting = np->is_shooting;
            p->in_vehicle = np->in_vehicle;
            
            // --- SYNC HIT MARKER ---
            if (id == my_client_id) {
                if (np->hit_feedback > p->hit_feedback) p->hit_feedback = np->hit_feedback;
            } else {
                 p->hit_feedback = np->hit_feedback;
            }

            if (p->is_shooting) p->recoil_anim = 1.0f;
        } else if (id == 0) {
            local_state.players[0].ammo[local_state.players[0].current_weapon] = np->ammo;
            local_state.players[0].in_vehicle = np->in_vehicle; 
        }
    }
}

void net_tick() {
    char buffer[4096];
    struct sockaddr_in sender;
    socklen_t slen = sizeof(sender);
    int len = recvfrom(sock, buffer, 4096, 0, (struct sockaddr*)&sender, &slen);
    while (len > 0) {
        NetHeader *head = (NetHeader*)buffer;
        if (head->type == PACKET_SNAPSHOT) {
            net_process_snapshot(buffer, len);
        }
        if (head->type == PACKET_WELCOME) {
            my_client_id = head->client_id;
            printf("✅ JOINED SERVER AS CLIENT ID: %d\n", my_client_id);
        }
        len = recvfrom(sock, buffer, 4096, 0, (struct sockaddr*)&sender, &slen);
    }
}

int main(int argc, char* argv[]) {
    for(int i=1; i<argc; i++) {
        if(strcmp(argv[i], "--host") == 0 && i+1<argc) {
            strncpy(SERVER_HOST, argv[++i], 63);
        }
    }

    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window *win = SDL_CreateWindow("SHANKPIT [BUILD 173 - NEON BRUTALISM]", 100, 100, 1280, 720, SDL_WINDOW_OPENGL);
    SDL_GL_CreateContext(win);
    net_init();
    
    local_init_match(1, 0);
    
    int running = 1;
    while(running) {
        SDL_Event e;
        while(SDL_PollEvent(&e)) {
            if(e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED && app_state != STATE_LOBBY) SDL_SetRelativeMouseMode(SDL_TRUE);
            if (e.type == SDL_MOUSEBUTTONDOWN && app_state != STATE_LOBBY) SDL_SetRelativeMouseMode(SDL_TRUE);
            
            if (app_state == STATE_LOBBY) {
                if(e.type == SDL_KEYDOWN) {
                    if (e.key.keysym.sym == SDLK_d) { app_state = STATE_GAME_LOCAL; local_init_match(1, MODE_DEATHMATCH); }
                    if (e.key.keysym.sym == SDLK_b) { app_state = STATE_GAME_LOCAL; local_init_match(8, MODE_DEATHMATCH); }
                    if (e.key.keysym.sym == SDLK_k) { app_state = STATE_GAME_LOCAL; local_init_match(8, MODE_EVOLUTION); }
                    
                    if (e.key.keysym.sym == SDLK_j) { 
                        app_state = STATE_GAME_NET;
                        net_connect(); 
                        SDL_SetRelativeMouseMode(SDL_TRUE);
                        glMatrixMode(GL_PROJECTION); glLoadIdentity(); gluPerspective(75.0, 1280.0/720.0, 0.1, Z_FAR); 
                        glMatrixMode(GL_MODELVIEW); glEnable(GL_DEPTH_TEST);
                    }
                    
                    if (app_state == STATE_GAME_LOCAL) {
                        SDL_SetRelativeMouseMode(SDL_TRUE);
                        glMatrixMode(GL_PROJECTION); glLoadIdentity(); gluPerspective(75.0, 1280.0/720.0, 0.1, Z_FAR); 
                        glMatrixMode(GL_MODELVIEW); glEnable(GL_DEPTH_TEST);
                    }
                }
            } else {
                if(e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
                    app_state = STATE_LOBBY;
                    SDL_SetRelativeMouseMode(SDL_FALSE);
                    glDisable(GL_DEPTH_TEST); glMatrixMode(GL_PROJECTION); glLoadIdentity(); gluOrtho2D(0, 1280, 0, 720); glMatrixMode(GL_MODELVIEW);
                }
                if(e.type == SDL_MOUSEMOTION) {
                    float sens = (current_fov < 50.0f) ? 0.05f : 0.15f; 
                    cam_yaw -= e.motion.xrel * sens;
                    if(cam_yaw > 360) cam_yaw -= 360; if(cam_yaw < 0) cam_yaw += 360;
                    cam_pitch -= e.motion.yrel * sens;
                    if(cam_pitch > 89) cam_pitch = 89; if(cam_pitch < -89) cam_pitch = -89;
                }
            }
        }
        if (app_state != STATE_LOBBY) SDL_SetRelativeMouseMode(SDL_TRUE);
        if (app_state == STATE_LOBBY) {
             glClearColor(0.02f, 0.02f, 0.05f, 1.0f); // Dark Lobby
             glClear(GL_COLOR_BUFFER_BIT);
             glLoadIdentity(); glColor3f(0, 1, 1); // CYAN TEXT
             draw_string("SHANKPIT", 400, 500, 20);
             draw_string("D: DEMO", 400, 400, 10);
             draw_string("B: BATTLE", 400, 350, 10);
             draw_string("J: JOIN S.FARTHQ.COM", 400, 300, 10);
             SDL_GL_SwapWindow(win);
        } 
        else {
            const Uint8 *k = SDL_GetKeyboardState(NULL);
            float fwd=0, str=0;
            if(k[SDL_SCANCODE_W]) fwd-=1; if(k[SDL_SCANCODE_S]) fwd+=1;
            if(k[SDL_SCANCODE_D]) str+=1; if(k[SDL_SCANCODE_A]) str-=1;
            int jump = k[SDL_SCANCODE_SPACE]; int crouch = k[SDL_SCANCODE_LCTRL];
            int shoot = (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT));
            int reload = k[SDL_SCANCODE_R];
            int use = k[SDL_SCANCODE_E];
            if(k[SDL_SCANCODE_1]) wpn_req=0; if(k[SDL_SCANCODE_2]) wpn_req=1;
            if(k[SDL_SCANCODE_3]) wpn_req=2; if(k[SDL_SCANCODE_4]) wpn_req=3; if(k[SDL_SCANCODE_5]) wpn_req=4;

            float target_fov = (local_state.players[0].current_weapon == WPN_SNIPER && (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_RIGHT))) ? 20.0f : 75.0f;
            current_fov += (target_fov - current_fov) * 0.2f;
            glMatrixMode(GL_PROJECTION); glLoadIdentity(); gluPerspective(current_fov, 1280.0/720.0, 0.1, Z_FAR); 
            glMatrixMode(GL_MODELVIEW);
            if (app_state == STATE_GAME_NET) {
                local_update(fwd, str, cam_yaw, cam_pitch, shoot, wpn_req, jump, crouch, reload, NULL, 0);
                UserCmd cmd = client_create_cmd(fwd, str, cam_yaw, cam_pitch, shoot, jump, crouch, reload, use, wpn_req);
                net_send_cmd(cmd);
                net_tick();
            } else {
                local_state.players[0].in_use = use;
                if (use && local_state.players[0].vehicle_cooldown == 0) {
                     local_state.players[0].in_vehicle = !local_state.players[0].in_vehicle;
                     local_state.players[0].vehicle_cooldown = 30;
                }
                if(local_state.players[0].vehicle_cooldown > 0) local_state.players[0].vehicle_cooldown--;
                local_update(fwd, str, cam_yaw, cam_pitch, shoot, wpn_req, jump, crouch, reload, NULL, 0);
            }
            draw_scene(&local_state.players[0]);
            SDL_GL_SwapWindow(win);
        }
        SDL_Delay(16);
    }
    SDL_Quit();
    return 0;
}
