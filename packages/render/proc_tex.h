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

/* S459-16, founder real-time: "we will want materials for concrete and wood and default textures
 * for those procedurally iterate the defaults" / "for the materials if you could have a some kind
 * of metal or shiny texture i dunno" -- three more real, seamlessly-tileable defaults, same
 * "generate at compile/init, no image files" convention as proctex_make_wall_brick_rgba above.
 * Metal's own real shininess comes from the specular shader pass (packages/render/
 * material_shaders.h), not from this texture -- this just gives it a real brushed-streak base
 * coat so it doesn't look flat-gray even where the highlight isn't hitting. */
void proctex_make_concrete_rgba(ProcTexture *t, int w, int h, uint32_t seed);

/* Rock/stone -- a world material this native build had no generator for, so a level naming
 * "rock"/"stone"/"granite"/"cliff" silently fell back to brick (material_texture_for_name's own
 * documented default). No checked-in level names one yet; the fallback is why that was invisible. Same file-free, compile-time, GL_REPEAT-tiled convention as concrete above: strata bands
 * on whole-cycle sines plus a wrapped blotch lattice, so the pattern meets itself at every edge. */
void proctex_make_rock_rgba(ProcTexture *t, int w, int h, uint32_t seed);
void proctex_make_wood_rgba(ProcTexture *t, int w, int h, uint32_t seed);
void proctex_make_metal_rgba(ProcTexture *t, int w, int h, uint32_t seed);
void proctex_make_ips_panel_rgba(ProcTexture *t, int w, int h, uint32_t seed);
void proctex_make_hps_bulb_rgba(ProcTexture *t, int w, int h, uint32_t seed);

#endif
