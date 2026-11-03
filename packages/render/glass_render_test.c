/* Live GL check for SHADER_GLASS (card #454, "glass alpha pass"): draws an opaque backing, then a
   glass pane in front of it via the real draw_material_glass_box, reads pixels back and asserts the
   contract the pass documents -- (a) the pane is tinted but NOT opaque, so what is behind it still
   changes what you see through it, (b) a saturated orange behind it still reads orange, (c) glass
   never writes depth. Run under Xvfb: make test-glass-render (skips cleanly if no GL context).

   (a) is measured differentially: the same scene is drawn twice with only the backing behind the
   pane changed, black then white, and the two readbacks must differ in the direction of the backing.
   An opaque pane would return identical pixels both times. This replaces the old absolute bound
   ("blue < 200"), which broke once the pass gained its additive reflection draw
   (draw_material_reflection_box, GL_SRC_ALPHA/GL_ONE) after this test was written: the reflection
   lifts every channel, so a legitimately translucent pane reads brighter than any fixed ceiling --
   measured 155/221/200 over the 26 background, i.e. roughly 60/81/83 from the alpha blend plus the
   reflected sky. A difference of two backings cannot be faked by an opaque plate, which is the
   property the founder actually asked for, and it needs no magic number from the shader. */
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

/* One frame: an opaque backing on the left of the pane's span, an opaque orange box on the right,
   then the glass pane across both. Only the backing colour varies between calls. */
static void draw_frame(float br, float bg, float bb, const float *mvp, const float *light, const float *cam) {
    glClearColor(0.1f, 0.1f, 0.1f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(mvp);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glColor3f(br, bg, bb);   /* backing behind the sampled left pixel, z=-6 */
    glBegin(GL_QUADS);
    glVertex3f(-3.5f, -1.5f, -6); glVertex3f(-0.05f, -1.5f, -6);
    glVertex3f(-0.05f, 1.5f, -6); glVertex3f(-3.5f, 1.5f, -6);
    glEnd();
    glColor3f(1.0f, 0.5f, 0.0f);   /* opaque orange box, right half, z=-6 */
    glBegin(GL_QUADS);
    glVertex3f(0, -1.5f, -6); glVertex3f(3, -1.5f, -6); glVertex3f(3, 1.5f, -6); glVertex3f(0, 1.5f, -6);
    glEnd();
    draw_material_glass_box(0, 0, -4, 8, 4, 0.1f, mvp, light, cam);
    glFinish();
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
    unsigned char over_black[4], over_white[4], px_or[4];
    float dep = 0.0f;
    glViewport(0, 0, 256, 256);

    draw_frame(0.0f, 0.0f, 0.0f, mvp, light, cam);
    glReadPixels(64, 128, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, over_black);   /* pane over black */
    glReadPixels(192, 128, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px_or);       /* pane over orange */
    /* depth where ONLY the pane covers cleared background: at (64,128) the backing quad sits
       behind it and would report the backing's depth, not the pane's */
    glReadPixels(64, 20, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &dep);

    draw_frame(1.0f, 1.0f, 1.0f, mvp, light, cam);
    glReadPixels(64, 128, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, over_white);   /* pane over white */

    printf("black+glass = %3d %3d %3d\nwhite+glass = %3d %3d %3d\norange+glass = %3d %3d %3d\n"
           "depth over bare glass = %.3f\n",
           over_black[0], over_black[1], over_black[2],
           over_white[0], over_white[1], over_white[2],
           px_or[0], px_or[1], px_or[2], dep);
    int fail = 0;

    /* the pane is visible over black at all (it tints, rather than being invisible or fully clear) */
    if (!(over_black[0] > 60 || over_black[1] > 60 || over_black[2] > 60)) {
        printf("FAIL: the pane is invisible over a black backing\n"); fail = 1;
    }
    /* the background still reaches the eye THROUGH the pane: an opaque plate would read the same
       over black and over white, so this is the real "not opaque" assertion */
    if (!(over_white[0] >= over_black[0] && over_white[1] >= over_black[1] && over_white[2] >= over_black[2])) {
        printf("FAIL: a brighter backing did not brighten the pane\n"); fail = 1;
    }
    if ((int)over_white[0] + over_white[1] + over_white[2] - (int)over_black[0] - over_black[1] - over_black[2] < 40) {
        printf("FAIL: the pane is effectively opaque (backing changed little)\n"); fail = 1;
    }
    if (!(px_or[0] > px_or[2] && px_or[2] > 60)) { printf("FAIL: orange not seen through glass\n"); fail = 1; }
    if (dep < 0.999f) { printf("FAIL: glass wrote depth\n"); fail = 1; }
    printf(fail ? "FAIL\n" : "OK\n");
    return fail;
}
