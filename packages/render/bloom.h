#ifndef SHANKPIT_BLOOM_H
#define SHANKPIT_BLOOM_H

// bloom.h -- real screen-space bloom/glow post-processing (S493, founder real-time: "how do we
// get more realistic lighting? glow effect post processing? ... we should see like the lighting
// kind of move a bit with the glow effect of the hps light").
//
// Real, found-first distinction: the "glow" that already existed before this (draw_light_glow_
// billboard, apps/lobby/src/main.c) is a camera-facing SPRITE drawn at each light fixture's own
// position -- not true bloom. It doesn't react to what's actually bright on screen, doesn't
// bleed past its own fixed billboard size, and does nothing for the SHADER_HPS_LIGHT/SHADER_
// IPS_LIGHT emissive glow passes (material_shaders.h) that already make those surfaces render
// bright. This module is the real thing: render the frame to an off-screen texture, extract the
// genuinely bright pixels (a real luminance threshold, so it naturally picks up bright emissive
// surfaces AND the already-existing per-frame HPS flicker without any extra wiring), blur that
// extraction, then composite it back additively over the real frame -- the standard, real
// technique every modern engine's own bloom pass uses, scaled down to this engine's own real
// GL 2.0-via-hand-loaded-extensions constraints (same real "no GLEW/glad, SDL_GL_GetProcAddress
// by hand" convention gl_shader.h already established, for the same real Windows-cross-compile
// portability reason documented there).
//
// Deliberately whole-frame, not 3D-world-only: HUD/weapon-viewmodel/overlay elements draw INSIDE
// draw_scene before the frame is done, and separating "3D world" from "2D HUD" render targets
// would need a much larger restructure of draw_scene's own control flow for a live, working
// render loop -- a real, accepted tradeoff (HUD elements picking up a slight bloom too), not an
// oversight; this game's own neon/retro-CRT aesthetic (the existing cyan Matrix floor, amber HPS
// glow) is a real, plausible visual fit for that anyway.

#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_opengl_glext.h>

// bloom_init loads the GL extension entry points this module needs (FBO/renderbuffer functions,
// beyond what gl_shader_load_extensions already loads) and compiles its own two real shader
// programs (bright-pass threshold extract, separable blur). Returns 1 on success, 0 on any
// failure (old driver/software renderer) -- callers must skip bloom_begin_scene/
// bloom_end_scene_and_composite entirely and render straight to the default framebuffer when this
// returns 0, same "never call into an unloaded function pointer" contract gl_shader_load_
// extensions already established.
int bloom_init(void);

// bloom_begin_scene binds the internal scene FBO and sets the GL viewport to (width, height) --
// call this as the very FIRST thing each frame, before any other rendering (including the
// existing glClear calls, which will then correctly target this FBO instead of the real
// backbuffer). Reallocates the FBO's own textures/renderbuffer automatically if (width, height)
// differs from the last call (a real, necessary response to this window being resizable --
// SDL_WINDOW_RESIZABLE, apps/lobby/src/main.c's own real window-creation flags). A no-op if
// bloom_init previously failed or was never called.
void bloom_begin_scene(int width, int height);

// bloom_end_scene_and_composite unbinds the scene FBO, runs the real bright-pass-extract + 2-pass
// separable blur (at a reduced internal resolution -- a real, deliberate glow-radius/performance
// tradeoff, not a mistake), then composites the result back onto the REAL default framebuffer:
// the original full-resolution scene texture drawn first (a plain textured full-screen quad,
// fixed-function, GL_MODULATE with white -- no shader needed for a straight copy), the blurred
// bright-pass drawn on top with additive blending (glBlendFunc(GL_ONE, GL_ONE)) so bright areas
// genuinely glow past their own edges instead of just being redrawn brighter in place. Call this
// as the very LAST thing each frame, replacing nothing -- everything that used to render straight
// to the backbuffer between bloom_begin_scene and this call now renders into the FBO instead,
// with identical visual result for anything below the bloom threshold. A no-op (falls through to
// whatever was already drawn to the default framebuffer, i.e. nothing extra happens) if bloom_init
// previously failed.
void bloom_end_scene_and_composite(int width, int height);

#endif // SHANKPIT_BLOOM_H
