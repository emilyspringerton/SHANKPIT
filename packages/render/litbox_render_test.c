/* Live GL check for the per-fragment box lighting (#451/#452): renders a wide wall lit by (a) a
   point light fixture and (b) a flashlight spot, reads a row of pixels back and asserts the light
   falls off SMOOTHLY (no hard edge: adjacent pixels never jump), is brighter near the source than far
   from it, and that the spot is Gaussian (on-axis > off-axis, still no step at the old cone rim).
   Uses the exact shader the game runs (litbox_shader.h). make test-litbox-render, under Xvfb. */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "gl_shader.h"
#include "litbox_shader.h"

#define W 256
static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); g_fail++; } } while (0)

static void persp(float *m, float fovy, float asp, float n, float f) {
    float t = 1.0f / tanf(fovy * 0.5f);
    for (int i = 0; i < 16; i++) m[i] = 0;
    m[0] = t / asp; m[5] = t; m[10] = (f + n) / (n - f); m[11] = -1; m[14] = 2 * f * n / (n - f);
}

static GLuint prog;
static void U4(const char *n, int cnt, const float *v) { gl_uniform4fv_n(gl_get_uniform_location(prog, n), cnt, v); }

/* Draw the wall (centre 0,0,-10, 60x20x1; front face +z at z=-9.5) with the given lights and
   return the luminance (0..255) of the middle pixel row. */
static void render_row(int nspot, const float *spos, const float *sdir, int npoint, const float *ppos, const float *pcol, unsigned char *row) {
    glViewport(0, 0, W, W);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    float pm[16]; persp(pm, 1.0f, 1.0f, 0.1f, 200.0f);
    glMatrixMode(GL_PROJECTION); glLoadMatrixf(pm);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();

    gl_use_program(prog);
    gl_uniform1i(gl_get_uniform_location(prog, "u_tex"), 0);
    gl_uniform1i(gl_get_uniform_location(prog, "u_nspot"), nspot);
    gl_uniform1i(gl_get_uniform_location(prog, "u_npoint"), npoint);
    gl_uniform1f(gl_get_uniform_location(prog, "u_cone_cos"), cosf(27.0f * 0.0174533f));
    if (nspot) { U4("u_spot_pos", nspot, spos); U4("u_spot_dir", nspot, sdir); }
    if (npoint) { U4("u_point_pos", npoint, ppos); U4("u_point_col", npoint, pcol); }
    float c[3] = {0, 0, -10}, s[3] = {60, 20, 1}, a[3] = {1, 1, 1};
    gl_uniform3fv(gl_get_uniform_location(prog, "u_center"), c);
    gl_uniform3fv(gl_get_uniform_location(prog, "u_size"), s);
    gl_uniform3fv(gl_get_uniform_location(prog, "u_albedo"), a);

    glPushMatrix();
    glTranslatef(c[0], c[1], c[2]); glScalef(s[0], s[1], s[2]);
    glBegin(GL_QUADS);
    glNormal3f(0, 0, 1); glColor3f(0.05f, 0.05f, 0.05f);
    glTexCoord2f(0, 0); glVertex3f(-0.5f, -0.5f, 0.5f);
    glTexCoord2f(1, 0); glVertex3f(0.5f, -0.5f, 0.5f);
    glTexCoord2f(1, 1); glVertex3f(0.5f, 0.5f, 0.5f);
    glTexCoord2f(0, 1); glVertex3f(-0.5f, 0.5f, 0.5f);
    glEnd();
    glPopMatrix();
    gl_use_program(0);

    unsigned char px[W * 4];
    glReadPixels(0, W / 2, W, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    for (int x = 0; x < W; x++) row[x] = (unsigned char)((px[x*4] + px[x*4+1] + px[x*4+2]) / 3);
}

static int max_step(const unsigned char *row) {
    int m = 0;
    for (int x = 1; x < W; x++) { int d = abs((int)row[x] - (int)row[x - 1]); if (d > m) m = d; }
    return m;
}

int main(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { printf("SKIP: no video\n"); return 0; }
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window *win = SDL_CreateWindow("t", 0, 0, W, W, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext ctx = win ? SDL_GL_CreateContext(win) : NULL;
    if (!ctx || !gl_shader_load_extensions()) { printf("SKIP: no GL/shaders\n"); return 0; }
    GLuint vs = gl_compile_shader(GL_VERTEX_SHADER, g_litbox_vs_src);
    GLuint fs = gl_compile_shader(GL_FRAGMENT_SHADER, g_litbox_fs_src);
    prog = gl_link_program(vs, fs);
    if (!prog) { printf("FAIL: litbox shader did not link\n"); return 1; }

    GLuint tex; unsigned char white[4] = {255, 255, 255, 255};
    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glEnable(GL_TEXTURE_2D);

    unsigned char row[W];

    /* (a) point light fixture in front of the wall, towards the left edge of the view */
    float pp[4] = {-5.0f, 0.0f, -6.0f, 45.0f}, pc[4] = {1, 1, 1, 1};
    render_row(0, NULL, NULL, 1, pp, pc, row);
    int left = row[8], mid = row[W / 2], right = row[W - 8];
    CHECK(left > mid && mid > right, "point light falloff not monotonic: %d %d %d", left, mid, right);
    CHECK(left - right > 40, "point light has no real gradient: %d vs %d", left, right);
    CHECK(max_step(row) <= 6, "point light shows a hard edge (max adjacent step %d)", max_step(row));
    printf("point: left %d mid %d right %d, max step %d\n", left, mid, right, max_step(row));

    /* (b) flashlight at the origin aimed straight at the wall: Gaussian pool, no cone edge */
    float sp[4] = {0, 0, 0, 55.0f}, sd[4] = {0, 0, -1, 0};
    render_row(1, sp, sd, 0, NULL, NULL, row);
    int centre = row[W / 2], edge = row[8];
    CHECK(centre > edge + 40, "spot not brighter on axis: centre %d edge %d", centre, edge);
    CHECK(max_step(row) <= 6, "spot shows a hard edge (max adjacent step %d)", max_step(row));
    printf("spot: centre %d edge %d, max step %d\n", centre, edge, max_step(row));

    /* (c) no light => only the 0.05 vertex colour: the shader adds nothing on its own */
    render_row(0, NULL, NULL, 0, NULL, NULL, row);
    CHECK(row[W / 2] < 20, "unlit wall should stay dark, got %d", row[W / 2]);

    if (g_fail) { printf("litbox_render_test: %d FAILED\n", g_fail); return 1; }
    printf("OK\n");
    return 0;
}
