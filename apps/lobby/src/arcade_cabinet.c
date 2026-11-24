/* apps/lobby/src/arcade_cabinet.c -- see arcade_cabinet.h. Decisions are PARENA (lm-arcade-*); this is the model,
 * the proximity read and the forwarding to level_shell. */
#include "arcade_cabinet.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "level_shell.h"
#include "../../../packages/simulation/level_manager_rules.h"

void draw_string(const char *str, float x, float y, float size); /* main.c: the 5x7 turtle font, filled quads */

/* CONSTRUCT hub (assets/tyler_levels/construct.json): the spawner is (4, 1, 0), the mecha cube sits at the origin
 * and the Architect's Orb is anchored 9 m ahead of the spawn facing: 6.5, 1.0 is beside the spawn path, 2.7 m from
 * where the hero starts, so the prompt is up the moment the hub loads. */
#define CAB_X 6.5f
#define CAB_Z 1.0f
#define CAB_FACE_YAW (-90.0f) /* local +z (the front) -> world -x, toward the spawn */

static int g_in_hub = 0;
static int g_anchored = 0;
static float g_floor_y = 0.0f;
static int g_near = 0;
static unsigned int g_now_ms = 0;

void arcade_cabinet_position(float *x, float *z) {
    if (x) *x = CAB_X;
    if (z) *z = CAB_Z;
}

static int cm(float v) {
    if (v > 100000.0f) return 100000000;
    if (v < -100000.0f) return -100000000;
    return (int)lroundf(v * 100.0f);
}

static int near_cabinet(float hx, float hy, float hz) {
    return lm_arcade_near(cm(hx - CAB_X), cm(hy - g_floor_y), cm(hz - CAB_Z));
}

void arcade_cabinet_hub_entered(void) {
    level_shell_arcade_command(lm_cmd_arcade_enter());
    g_anchored = 0;
}

int arcade_cabinet_prompt(void) {
    return lm_arcade_prompt(g_in_hub, g_near, level_shell_state(), level_shell_load_failed());
}

int arcade_cabinet_use(int in_hub, float hx, float hy, float hz) {
    int cmd;
    if (!in_hub || !g_anchored) return 0;
    cmd = lm_arcade_use_cmd(1, near_cabinet(hx, hy, hz), level_shell_state(), level_shell_load_failed());
    if (!cmd) return 0;
    level_shell_arcade_command(cmd);
    return 1;
}

/* ---- the model: boxes in cabinet-local space (origin = floor centre, +z the front), flat-shaded by face --------- */

static void box(float cx, float cy, float cz, float w, float h, float d, float r, float g, float b) {
    float x0 = cx - w * 0.5f, x1 = cx + w * 0.5f, y0 = cy - h * 0.5f, y1 = cy + h * 0.5f, z0 = cz - d * 0.5f, z1 = cz + d * 0.5f;
    glBegin(GL_QUADS);
    glColor3f(r * 0.85f, g * 0.85f, b * 0.85f); /* front */
    glVertex3f(x0, y0, z1); glVertex3f(x1, y0, z1); glVertex3f(x1, y1, z1); glVertex3f(x0, y1, z1);
    glColor3f(r * 0.55f, g * 0.55f, b * 0.55f); /* back */
    glVertex3f(x0, y0, z0); glVertex3f(x0, y1, z0); glVertex3f(x1, y1, z0); glVertex3f(x1, y0, z0);
    glColor3f(r * 0.65f, g * 0.65f, b * 0.65f); /* left */
    glVertex3f(x0, y0, z0); glVertex3f(x0, y0, z1); glVertex3f(x0, y1, z1); glVertex3f(x0, y1, z0);
    glColor3f(r * 0.7f, g * 0.7f, b * 0.7f); /* right */
    glVertex3f(x1, y0, z0); glVertex3f(x1, y1, z0); glVertex3f(x1, y1, z1); glVertex3f(x1, y0, z1);
    glColor3f(r, g, b); /* top */
    glVertex3f(x0, y1, z1); glVertex3f(x1, y1, z1); glVertex3f(x1, y1, z0); glVertex3f(x0, y1, z0);
    glColor3f(r * 0.4f, g * 0.4f, b * 0.4f); /* bottom */
    glVertex3f(x0, y0, z1); glVertex3f(x0, y0, z0); glVertex3f(x1, y0, z0); glVertex3f(x1, y0, z1);
    glEnd();
}

/* text on a +z face: width w across, centred at (cx, cy), z = the face plane */
static void face_text(const char *s, float cx, float cy, float z, float w, float r, float g, float b) {
    float units = (float)strlen(s) * 6.0f - 1.0f, k = w / units;
    glPushMatrix();
    glTranslatef(cx - w * 0.5f, cy + 3.5f * k, z);
    glScalef(k, k, 1.0f);
    glColor3f(r, g, b);
    draw_string(s, 0.0f, 0.0f, 1.0f);
    glPopMatrix();
}

static const float ATTRACT[8][3] = {
    {0.95f, 0.25f, 0.55f}, {0.98f, 0.62f, 0.15f}, {0.95f, 0.9f, 0.2f}, {0.3f, 0.9f, 0.45f},
    {0.2f, 0.8f, 0.95f},   {0.4f, 0.45f, 0.98f},  {0.75f, 0.35f, 0.95f}, {0.95f, 0.95f, 0.95f},
};

static void draw_screen(int screen, unsigned int t) {
    const float sz = 0.215f, sw = 0.8f, sh = 0.62f, sy = 1.62f;
    float p = (float)lm_arcade_pulse((int)t) / 255.0f;
    int ph = lm_arcade_attract_phase((int)t), i;
    float fr, fg, fb;
    if (screen == 2) { fr = 0.45f + 0.35f * p; fg = 0.04f; fb = 0.04f; }                    /* failed: red */
    else if (screen == 1) { fr = 0.1f; fg = 0.25f; fb = 0.55f + 0.2f * p; }                  /* loading: blue */
    else if (screen == 3) { fr = 0.16f; fg = 0.18f; fb = 0.26f; }                            /* paused: dim */
    else { fr = 0.07f; fg = 0.07f; fb = 0.16f; }                                             /* attract: dark base */
    box(0.0f, sy, sz, sw, sh, 0.03f, fr, fg, fb);
    if (screen == 0 || screen == 4) {
        /* attract: coloured bars scroll up the screen and the title cycles its colour */
        for (i = 0; i < 4; i++) {
            const float *c = ATTRACT[(ph + i * 2) & 7];
            float yy = sy - sh * 0.5f + 0.06f + fmodf((float)t * 0.0003f + (float)i * 0.25f, 1.0f) * (sh - 0.12f);
            box(0.0f, yy, sz + 0.02f, sw - 0.1f, 0.035f, 0.01f, c[0] * 0.7f, c[1] * 0.7f, c[2] * 0.7f);
        }
        face_text("BRAWLPIT", 0.0f, sy + 0.1f, sz + 0.03f, 0.64f, ATTRACT[ph][0], ATTRACT[ph][1], ATTRACT[ph][2]);
        if (((int)(t / 500)) & 1) face_text("INSERT COIN", 0.0f, sy - 0.14f, sz + 0.03f, 0.5f, 1.0f, 1.0f, 1.0f);
    } else if (screen == 1) {
        float prog = fmodf((float)t * 0.0008f, 1.0f);
        face_text("LOADING", 0.0f, sy + 0.1f, sz + 0.03f, 0.5f, 0.8f, 0.9f, 1.0f);
        box(-(sw - 0.2f) * 0.5f * (1.0f - prog), sy - 0.12f, sz + 0.02f, (sw - 0.2f) * prog + 0.01f, 0.04f, 0.01f, 0.4f, 0.8f, 1.0f);
    } else if (screen == 2) {
        face_text("OFFLINE", 0.0f, sy + 0.1f, sz + 0.03f, 0.5f, 1.0f, 0.6f, 0.55f);
        face_text("F: RETRY", 0.0f, sy - 0.12f, sz + 0.03f, 0.4f, 1.0f, 0.85f, 0.8f);
    } else {
        face_text("PAUSED", 0.0f, sy + 0.1f, sz + 0.03f, 0.45f, 0.75f, 0.8f, 0.95f);
        face_text("F: RESUME", 0.0f, sy - 0.12f, sz + 0.03f, 0.45f, 0.75f, 0.8f, 0.95f);
    }
}

static void draw_model(unsigned int t) {
    int state = level_shell_state(), failed = level_shell_load_failed();
    int screen = lm_arcade_screen(state, failed), light = lm_arcade_light(state, failed);
    float p = (float)lm_arcade_pulse((int)t) / 255.0f, lr, lg, lb;
    int ph = lm_arcade_attract_phase((int)t);
    const float *mc = ATTRACT[ph];
    /* cabinet shell */
    box(0.0f, 0.45f, 0.0f, 1.1f, 0.9f, 0.95f, 0.13f, 0.12f, 0.24f);                        /* lower body */
    box(0.0f, 1.6f, -0.12f, 1.1f, 1.45f, 0.6f, 0.11f, 0.1f, 0.2f);                         /* upper body, set back */
    box(0.0f, 1.62f, 0.17f, 0.96f, 0.82f, 0.06f, 0.02f, 0.02f, 0.04f);                     /* bezel */
    box(-0.56f, 1.2f, 0.0f, 0.03f, 2.4f, 0.9f, 0.9f, 0.2f, 0.55f);                         /* side stripes */
    box(0.56f, 1.2f, 0.0f, 0.03f, 2.4f, 0.9f, 0.9f, 0.2f, 0.55f);
    draw_screen(screen, t);
    /* control deck, angled toward the player */
    glPushMatrix();
    glTranslatef(0.0f, 0.98f, 0.38f);
    glRotatef(-12.0f, 1.0f, 0.0f, 0.0f);
    box(0.0f, 0.0f, 0.0f, 1.14f, 0.1f, 0.5f, 0.2f, 0.18f, 0.32f);
    box(-0.27f, 0.12f, 0.02f, 0.05f, 0.18f, 0.05f, 0.75f, 0.75f, 0.8f);                    /* joystick shaft */
    box(-0.27f, 0.25f, 0.02f, 0.13f, 0.1f, 0.13f, 0.95f, 0.15f + 0.2f * p, 0.2f);          /* ball top */
    box(0.13f, 0.07f, 0.04f, 0.12f, 0.05f, 0.12f, 0.95f, 0.2f + 0.3f * p, 0.2f);           /* buttons */
    box(0.33f, 0.07f, 0.04f, 0.12f, 0.05f, 0.12f, 0.95f, 0.85f, 0.15f + 0.3f * p);
    glPopMatrix();
    /* marquee, lit */
    box(0.0f, 2.42f, 0.0f, 1.1f, 0.34f, 0.8f, 0.15f + 0.4f * mc[0] * (0.6f + 0.4f * p), 0.08f + 0.25f * mc[1], 0.2f + 0.4f * mc[2] * (0.6f + 0.4f * p));
    face_text("BRAWLPIT", 0.0f, 2.46f, 0.402f, 0.95f, 1.0f, 1.0f, 1.0f);
    face_text("WOTAN", 0.0f, 2.35f, 0.402f, 0.5f, 1.0f, 0.88f, 0.4f);
    /* status light on the front of the lower body: green ready, amber loading/paused, red failed */
    if (light == 2) { lr = 0.4f + 0.6f * p; lg = 0.05f; lb = 0.05f; }
    else if (light == 1) { lr = 1.0f; lg = 0.55f + 0.3f * p; lb = 0.05f; }
    else { lr = 0.1f; lg = 0.55f + 0.45f * p; lb = 0.2f; }
    box(0.38f, 0.78f, 0.485f, 0.12f, 0.08f, 0.03f, lr, lg, lb);
}

/* additive glow: the screen's light spilling on the floor and a halo round the status light */
static void draw_glow(unsigned int t) {
    int state = level_shell_state(), failed = level_shell_load_failed();
    int screen = lm_arcade_screen(state, failed);
    float p = (float)lm_arcade_pulse((int)t) / 255.0f, a = 0.10f + 0.12f * p;
    const float *c = ATTRACT[lm_arcade_attract_phase((int)t)];
    float r = c[0], g = c[1], b = c[2];
    if (screen == 2) { r = 0.9f; g = 0.1f; b = 0.1f; }
    else if (screen == 1) { r = 0.3f; g = 0.6f; b = 1.0f; }
    else if (screen == 3) { r = 0.4f; g = 0.45f; b = 0.7f; a *= 0.5f; }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);
    glBegin(GL_QUADS);
    glColor4f(r, g, b, a);
    glVertex3f(-0.7f, 0.03f, 0.45f); glVertex3f(0.7f, 0.03f, 0.45f);
    glColor4f(r, g, b, 0.0f);
    glVertex3f(1.3f, 0.03f, 2.0f); glVertex3f(-1.3f, 0.03f, 2.0f);
    glEnd();
    glColor4f(r, g, b, a * 0.8f); /* a veil in front of the screen: the glass glows */
    glBegin(GL_QUADS);
    glVertex3f(-0.4f, 1.3f, 0.24f); glVertex3f(0.4f, 1.3f, 0.24f); glVertex3f(0.4f, 1.93f, 0.24f); glVertex3f(-0.4f, 1.93f, 0.24f);
    glEnd();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void arcade_cabinet_draw_world(unsigned int now_ms, int in_hub, float hx, float hy, float hz) {
    g_now_ms = now_ms;
    if (!in_hub) { g_in_hub = 0; g_anchored = 0; g_near = 0; return; }
    if (!g_anchored) { g_anchored = 1; g_floor_y = hy; } /* the hero stands on the hub's floor at its first frame */
    g_in_hub = 1;
    g_near = near_cabinet(hx, hy, hz);
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_POLYGON_BIT);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glPushMatrix();
    glTranslatef(CAB_X, g_floor_y, CAB_Z);
    glRotatef(CAB_FACE_YAW, 0.0f, 1.0f, 0.0f);
    draw_model(now_ms);
    draw_glow(now_ms);
    glPopMatrix();
    glPopAttrib();
}

/* the prompt, bottom centre above the world's HUD rows (virtual 1280x720, y up, turtle font 6 * size px a glyph) */
void arcade_cabinet_hud(void) {
    static const char *const lines[5] = {
        "", "F: PLAY BRAWLPIT", "LOADING BRAWLPIT...", "BRAWLPIT FAILED TO LOAD   F: TRY AGAIN", "F: RESUME BRAWLPIT",
    };
    int p = arcade_cabinet_prompt();
    const char *s, *why = level_shell_message();
    float size = 3.0f, w, pulse = 0.75f + 0.25f * ((float)lm_arcade_pulse((int)g_now_ms) / 255.0f);
    if (p <= 0 || p > 4) return;
    s = lines[p];
    w = (float)strlen(s) * size * 6.0f;
    glColor3f(0.0f, 0.0f, 0.0f);
    draw_string(s, (1280.0f - w) * 0.5f + size, 200.0f - size, size);
    if (p == 3) glColor3f(1.0f, 0.5f, 0.45f); else glColor3f(1.0f * pulse, 0.9f * pulse, 0.35f * pulse);
    draw_string(s, (1280.0f - w) * 0.5f, 200.0f, size);
    if (p == 3 && why[0]) { /* the level host's own reason, clipped to the screen */
        char line[100];
        snprintf(line, sizeof line, "%.96s", why);
        w = (float)strlen(line) * 2.0f * 6.0f;
        glColor3f(1.0f, 0.75f, 0.7f);
        draw_string(line, (1280.0f - w) * 0.5f, 170.0f, 2.0f);
    }
}
