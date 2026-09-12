#ifndef PROC_TEX_H
#define PROC_TEX_H

#include <stddef.h>
#include <stdint.h>
#include <SDL2/SDL_opengl.h>

typedef struct ProcTexture {
    int width;
    int height;
    GLuint tex_id;
    unsigned char *pixels;
} ProcTexture;

void proctex_init(void);
int proc_tex_create(ProcTexture *t, int w, int h);
void proc_tex_destroy(ProcTexture *t);
void proc_tex_upload(ProcTexture *t);
void proc_tex_fill_emily_vibe(ProcTexture *t, float seed, float t_sec);
void proctex_make_noise_rgba(ProcTexture *t, int w, int h, uint32_t seed);
void proctex_make_glitch_marks_rgba(ProcTexture *t, int w, int h, uint32_t seed);
void proctex_upload_to_gl(ProcTexture *t);

/* Real, seamlessly-tileable world-surface textures (founder real-time, 2026-09-12: "lets start
 * adding textures to shankpit have the compile like generate the textures at compile for now" --
 * the 1982-graphics flat-vertex-color look draw_terrain/draw_map had until this pass). Unlike
 * proctex_make_noise_rgba/proctex_make_glitch_marks_rgba above (one-shot cosmetic overlays), these
 * two are meant to be GL_REPEAT-tiled across real world geometry, so their own noise lattice wraps
 * at the texture's edges instead of just cutting off. */
void proctex_make_ground_rgba(ProcTexture *t, int w, int h, uint32_t seed);
void proctex_make_wall_brick_rgba(ProcTexture *t, int w, int h, uint32_t seed);

#endif
