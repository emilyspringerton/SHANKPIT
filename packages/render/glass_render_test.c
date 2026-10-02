/* Live GL check for SHADER_GLASS (card #454, "glass alpha pass"): draws an opaque orange box,
   then a glass pane in front of it via the real draw_material_glass_box, reads pixels back and
   asserts (a) the pane over empty background is tinted but NOT opaque, (b) the pane over the
   orange box blends (orange still dominant, shifted toward cyan), (c) glass never writes depth.
   Run under Xvfb: make test-glass-render (skips cleanly if no GL context). */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <math.h>
#include "material_shaders.h"
#include "gl_shader.h"

int on_glass_tint(int c) { static const int t[4] = {150, 225, 235, 70}; return t[c]; }

static void persp(float *m, float fovy, float asp, float n, float f) {
    float t = 1.0f / tanf(fovy * 0.5f);
    for (int i = 0; i < 16; i++) m[i] = 0;
    m[0] = t / asp; m[5] = t; m[10] = (f + n) / (n - f); m[11] = -1; m[14] = 2 * f * n / (n - f);
}

int main(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { printf("SKIP: no video\n"); return 0; }
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window *w = SDL_CreateWindow("t", 0, 0, 256, 256, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext c = w ? SDL_GL_CreateContext(w) : NULL;
    if (!c || !gl_shader_load_extensions()) { printf("SKIP: no GL/shaders\n"); return 0; }
    glass_shader_init();
    if (!g_glass_shader_ready) { printf("FAIL: glass shader not ready\n"); return 1; }

    float mvp[16]; persp(mvp, 1.0f, 1.0f, 0.1f, 100.0f);   /* camera at origin looking -z */
    float light[3] = {0.3f, 0.8f, 0.5f}, cam[3] = {0, 0, 0};
    glViewport(0, 0, 256, 256);
    glClearColor(0.1f, 0.1f, 0.1f, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION); glLoadMatrixf(mvp);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glColor3f(1.0f, 0.5f, 0.0f);   /* opaque orange box, right half, z=-6 */
    glBegin(GL_QUADS);
    glVertex3f(0, -1.5f, -6); glVertex3f(3, -1.5f, -6); glVertex3f(3, 1.5f, -6); glVertex3f(0, 1.5f, -6);
    glEnd();
    /* wide thin glass pane at z=-4 covering both halves */
    draw_material_glass_box(0, 0, -4, 8, 4, 0.1f, mvp, light, cam);
    unsigned char px_bg[4], px_or[4]; float dep;
    glReadPixels(64, 128, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px_bg);    /* left: pane over background */
    glReadPixels(192, 128, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px_or);   /* right: pane over orange */
    glReadPixels(64, 128, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &dep);
    printf("bg+glass  = %3d %3d %3d\norange+glass = %3d %3d %3d\ndepth over bare glass = %.3f\n",
           px_bg[0], px_bg[1], px_bg[2], px_or[0], px_or[1], px_or[2], dep);
    int fail = 0;
    /* bg (25,25,25) must move toward tint (150,225,235) but stay far from it (alpha<=0.85 and ~0.27 nominal) */
    if (!(px_bg[2] > 60 && px_bg[2] < 200 && px_bg[2] > px_bg[0])) { printf("FAIL: bg tint/alpha\n"); fail = 1; }
    if (!(px_or[0] > px_or[2] && px_or[2] > 60)) { printf("FAIL: orange not seen through glass\n"); fail = 1; }
    if (dep < 0.999f) { printf("FAIL: glass wrote depth\n"); fail = 1; }
    printf(fail ? "FAIL\n" : "OK\n");
    return fail;
}
