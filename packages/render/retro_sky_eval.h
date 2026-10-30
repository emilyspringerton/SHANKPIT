#ifndef RETRO_SKY_EVAL_H
#define RETRO_SKY_EVAL_H

/* retro_sky_eval.h -- the two purely mathematical things the sky knows: where the sun is and what
 * colour its fog is. Split out of retro_sky.h because that header pulls in proc_tex.h, which needs
 * <SDL2/SDL_opengl.h>: every consumer that only wants to compute LIGHT (retro_lighting.c, and the
 * lighting chain tests) was being forced to have a GPU header to do arithmetic. Declaring them here
 * keeps the dependency honest -- retro_sky.h includes this too, so there is still exactly one
 * prototype for each.
 *
 * Implemented in retro_sky.c alongside the dome drawing, which is why that file remains GL-bound;
 * lighting_chain_test.c stubs these two functions when linking headlessly. */

void retro_sky_eval_sun_dir(float time_sec, float *out_x, float *out_y, float *out_z);
void retro_sky_eval_fog_rgb(float time_sec, float *out_r, float *out_g, float *out_b);

#endif
