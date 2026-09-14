#ifndef MATERIAL_SHADERS_H
#define MATERIAL_SHADERS_H

// material_shaders.h -- SHANKPIT's real, native, compiled-in material shader registry (S459-16,
// founder real-time: "im sure somehow this is going to need to play with shaders too so build
// that in from day 1 ... we are trying to actually make this look like a real game" / "build the
// shaders in to the native and then refer to the shaders from NOCK directly? a shader registry
// too but for now it will just be like the ffi names or whatever not the real shader code" /
// "vs0 are any correct real shaders" / "build it in we dont have endless iterations").
//
// The real, load-bearing split this header exists to enforce: IDUNA/NOCK's own shankpit_materials
// table stores a material's own ShaderName (a plain string, e.g. "standard") -- NEVER GLSL
// source. This header is the one real place that name resolves to actual shader code. Adding a
// new named shader (VS1's own real, deferred "flourescent light" emissive material, founder:
// "if we could make a material and quickly turn it into a flourescent light that would be
// amazing... think of that as vs1") means adding a new SHADER_* constant + program here, and a
// matching new row in IDUNA's own real shader-name validation (internal/shankpit/
// material_store.go's own validShaderNames) -- NOT a schema change, NOT a NOCK code-editor field.
//
// SHADER_STANDARD (VS0, this pass's own real, complete, working shader): a real Blinn-Phong
// specular-highlight pass, additively blended on TOP of each box's own existing per-vertex CPU-
// computed lighting (apps/lobby's own retro_eval_brush_lighting_rgb pipeline, completely
// untouched) rather than replacing it -- same real "additive overlay, not a full lighting
// rewrite" technique weapon 7's own draw_flashlight_beam already proved safe and cheap. A
// material with near-zero specular (brick/concrete/wood) costs nothing extra: draw_material_
// specular_box's own caller skips the draw call entirely below MATERIAL_SPECULAR_DRAW_THRESHOLD.
//
// Same real gl_shader.c/DynamicVBO foundation gband_draw_skinned (S144-02) and weapon 7's own
// draw_flashlight_beam (S459-14) already proved -- genuinely reused, not reinvented a third time.

#include "gl_shader.h"
#include <math.h>
#include <string.h>

#define SHADER_STANDARD "standard"
#define MATERIAL_SPECULAR_DRAW_THRESHOLD 0.12f /* below this, the highlight is visually
    negligible -- brick(0.04)/concrete(0.03)/wood(0.06) all skip the draw call entirely;
    metal(0.85) always draws */

static GLuint g_material_shader_program = 0;
static DynamicVBO g_material_shader_vbo;
static int g_material_shader_ready = 0;

static const char *g_material_shader_vs_src =
    "#version 120\n"
    "attribute vec3 a_pos;\n"
    "attribute vec3 a_normal;\n"
    "uniform mat4 u_mvp;\n"
    "varying vec3 v_normal;\n"
    "varying vec3 v_world_pos;\n"
    "void main() {\n"
    "    v_normal = a_normal;\n"
    "    v_world_pos = a_pos;\n"
    "    gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
    "}\n";

// Real Blinn-Phong specular term ONLY (no ambient/diffuse -- those are already baked into the
// box's own real, existing CPU-lit color this pass draws additively on top of). u_light_dir
// points FROM the surface TOWARD the light (already normalized by the caller, taken from
// RetroLightingState's own real sun_dir/moon_dir depending on day/night -- the exact same
// dominant light direction every other box already shades against, so the highlight lands on the
// correct side of a box, not an arbitrary fixed direction).
static const char *g_material_shader_fs_src =
    "#version 120\n"
    "uniform vec3 u_light_dir;\n"
    "uniform vec3 u_cam_pos;\n"
    "uniform float u_specular;\n"
    "uniform float u_shininess;\n"
    "varying vec3 v_normal;\n"
    "varying vec3 v_world_pos;\n"
    "void main() {\n"
    "    vec3 n = normalize(v_normal);\n"
    "    vec3 v = normalize(u_cam_pos - v_world_pos);\n"
    "    vec3 l = normalize(u_light_dir);\n"
    "    vec3 h = normalize(l + v);\n"
    "    float spec = pow(max(dot(n, h), 0.0), u_shininess) * u_specular;\n"
    "    gl_FragColor = vec4(1.0, 0.97, 0.9, spec);\n"
    "}\n";

static inline void material_shader_init(void) {
    if (!gl_shader_load_extensions()) {
        SDL_Log("material_shaders: GL extension loading failed -- specular pass disabled");
        return;
    }
    GLuint vs = gl_compile_shader(GL_VERTEX_SHADER, g_material_shader_vs_src);
    GLuint fs = gl_compile_shader(GL_FRAGMENT_SHADER, g_material_shader_fs_src);
    g_material_shader_program = gl_link_program(vs, fs);
    if (!g_material_shader_program) {
        SDL_Log("material_shaders: shader link failed -- specular pass disabled");
        return;
    }
    if (!gl_dynamic_vbo_init(&g_material_shader_vbo, 48)) { /* one box = 6 faces * 6 verts (2 tris) = 36 */
        SDL_Log("material_shaders: dynamic VBO init failed -- specular pass disabled");
        return;
    }
    g_material_shader_ready = 1;
    SDL_Log("material_shaders: SHADER_STANDARD (Blinn-Phong specular) ready");
}

// draw_material_specular_box draws one box's own real specular highlight, additively, on top of
// whatever's already been rendered there this frame (the caller draws the base lit+textured box
// FIRST, then this). x/y/z/w/h/d match physics.h's own real Box convention (center + full
// extents). mvp is camera-only (model is identity, the 36 verts below are already world-space --
// same "bake the transform into the verts" contract draw_flashlight_beam established).
static inline void draw_material_specular_box(float x, float y, float z, float w, float h, float d,
                                                float specular, float shininess,
                                                const float *mvp16, const float *light_dir,
                                                const float *cam_pos) {
    if (!g_material_shader_ready) return;
    if (specular < MATERIAL_SPECULAR_DRAW_THRESHOLD) return;

    float hw = w * 0.5f, hh = h * 0.5f, hd = d * 0.5f;
    // 6 faces, 2 triangles each, real per-face outward normals -- CCW winding matching every
    // other box-drawing call site's own existing GL_QUADS winding in this file (top/bottom/front/
    // back/left/right, same face order as draw_map's own textured box for easy visual comparison).
    float verts[36 * 6];
    int vi = 0;
#define V(px, py, pz, nx, ny, nz) \
    verts[vi++] = x + (px); verts[vi++] = y + (py); verts[vi++] = z + (pz); \
    verts[vi++] = (nx); verts[vi++] = (ny); verts[vi++] = (nz);
    /* top (+y) */
    V(-hw, hh, hd, 0, 1, 0) V(hw, hh, hd, 0, 1, 0) V(hw, hh, -hd, 0, 1, 0)
    V(-hw, hh, hd, 0, 1, 0) V(hw, hh, -hd, 0, 1, 0) V(-hw, hh, -hd, 0, 1, 0)
    /* bottom (-y) */
    V(-hw, -hh, -hd, 0, -1, 0) V(hw, -hh, -hd, 0, -1, 0) V(hw, -hh, hd, 0, -1, 0)
    V(-hw, -hh, -hd, 0, -1, 0) V(hw, -hh, hd, 0, -1, 0) V(-hw, -hh, hd, 0, -1, 0)
    /* front (+z) */
    V(-hw, -hh, hd, 0, 0, 1) V(hw, -hh, hd, 0, 0, 1) V(hw, hh, hd, 0, 0, 1)
    V(-hw, -hh, hd, 0, 0, 1) V(hw, hh, hd, 0, 0, 1) V(-hw, hh, hd, 0, 0, 1)
    /* back (-z) */
    V(hw, -hh, -hd, 0, 0, -1) V(-hw, -hh, -hd, 0, 0, -1) V(-hw, hh, -hd, 0, 0, -1)
    V(hw, -hh, -hd, 0, 0, -1) V(-hw, hh, -hd, 0, 0, -1) V(hw, hh, -hd, 0, 0, -1)
    /* left (-x) */
    V(-hw, -hh, -hd, -1, 0, 0) V(-hw, -hh, hd, -1, 0, 0) V(-hw, hh, hd, -1, 0, 0)
    V(-hw, -hh, -hd, -1, 0, 0) V(-hw, hh, hd, -1, 0, 0) V(-hw, hh, -hd, -1, 0, 0)
    /* right (+x) */
    V(hw, -hh, hd, 1, 0, 0) V(hw, -hh, -hd, 1, 0, 0) V(hw, hh, -hd, 1, 0, 0)
    V(hw, -hh, hd, 1, 0, 0) V(hw, hh, -hd, 1, 0, 0) V(hw, hh, hd, 1, 0, 0)
#undef V

    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    gl_use_program(g_material_shader_program);
    gl_uniform_matrix4fv(gl_get_uniform_location(g_material_shader_program, "u_mvp"), mvp16);
    gl_uniform3fv(gl_get_uniform_location(g_material_shader_program, "u_light_dir"), light_dir);
    gl_uniform3fv(gl_get_uniform_location(g_material_shader_program, "u_cam_pos"), cam_pos);
    gl_uniform1f(gl_get_uniform_location(g_material_shader_program, "u_specular"), specular);
    gl_uniform1f(gl_get_uniform_location(g_material_shader_program, "u_shininess"), shininess);
    gl_dynamic_vbo_draw(&g_material_shader_vbo, verts, 36, GL_TRIANGLES);
    gl_use_program(0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
}

// SHADER_IPS_LIGHT (founder real-time, 2026-09-14: "can we design a material for an IPS light its
// going to need a special shader build it in") -- a real, unlit EMISSIVE pass, genuinely
// different from SHADER_STANDARD's additive-highlight-on-top-of-lit-color treatment: a light
// fixture shouldn't itself go dark on its unlit side the way an ordinary wall correctly does.
// draw_map's own material pass (apps/lobby/src/main.c) forces this box's base per-face lit_r/g/b
// to full bright BEFORE texturing for ips_light boxes (skipping the normal day/night darkening
// entirely), and this pass then adds a real, soft, view-independent glow on top -- a cool white-
// blue tone matching a real IPS display panel's own backlight color, with a gentle center-hotspot
// falloff (fresnel-ish, brighter face-on than at grazing angles, same real "v of view dir vs
// normal" idea the specular pass above already uses for its own highlight) so a flat panel reads
// as glowing, not just flat-painted.
#define SHADER_IPS_LIGHT "ips_light"

static GLuint g_ips_light_shader_program = 0;
static DynamicVBO g_ips_light_shader_vbo;
static int g_ips_light_shader_ready = 0;

static const char *g_ips_light_fs_src =
    "#version 120\n"
    "uniform vec3 u_cam_pos;\n"
    "uniform vec3 u_glow_color;\n"
    "uniform float u_intensity;\n"
    "varying vec3 v_normal;\n"
    "varying vec3 v_world_pos;\n"
    "void main() {\n"
    "    vec3 n = normalize(v_normal);\n"
    "    vec3 v = normalize(u_cam_pos - v_world_pos);\n"
    "    float facing = max(dot(n, v), 0.0);\n"
    "    float glow = mix(0.55, 1.0, facing);\n"
    "    gl_FragColor = vec4(u_glow_color * u_intensity * glow, 1.0);\n"
    "}\n";

static inline void ips_light_shader_init(void) {
    if (!gl_shader_load_extensions()) {
        SDL_Log("material_shaders: GL extension loading failed -- ips_light pass disabled");
        return;
    }
    GLuint vs = gl_compile_shader(GL_VERTEX_SHADER, g_material_shader_vs_src);
    GLuint fs = gl_compile_shader(GL_FRAGMENT_SHADER, g_ips_light_fs_src);
    g_ips_light_shader_program = gl_link_program(vs, fs);
    if (!g_ips_light_shader_program) {
        SDL_Log("material_shaders: ips_light shader link failed -- pass disabled");
        return;
    }
    if (!gl_dynamic_vbo_init(&g_ips_light_shader_vbo, 48)) {
        SDL_Log("material_shaders: ips_light VBO init failed -- pass disabled");
        return;
    }
    g_ips_light_shader_ready = 1;
    SDL_Log("material_shaders: SHADER_IPS_LIGHT (emissive panel) ready");
}

// draw_material_emissive_box -- same real 36-vert box-with-outward-normals shape
// draw_material_specular_box already builds (kept as a literal, separate copy rather than a
// shared helper -- the two functions' own uniform sets genuinely differ, and this whole file's
// own established pattern is one self-contained function per named shader, not a shared/branching
// one). mvp is camera-only, same "bake the transform into the verts" contract as every other real
// box-drawing call site in this codebase.
static inline void draw_material_emissive_box(float x, float y, float z, float w, float h, float d,
                                                const float *mvp16, const float *cam_pos) {
    if (!g_ips_light_shader_ready) return;

    float hw = w * 0.5f, hh = h * 0.5f, hd = d * 0.5f;
    float verts[36 * 6];
    int vi = 0;
#define V(px, py, pz, nx, ny, nz) \
    verts[vi++] = x + (px); verts[vi++] = y + (py); verts[vi++] = z + (pz); \
    verts[vi++] = (nx); verts[vi++] = (ny); verts[vi++] = (nz);
    V(-hw, hh, hd, 0, 1, 0) V(hw, hh, hd, 0, 1, 0) V(hw, hh, -hd, 0, 1, 0)
    V(-hw, hh, hd, 0, 1, 0) V(hw, hh, -hd, 0, 1, 0) V(-hw, hh, -hd, 0, 1, 0)
    V(-hw, -hh, -hd, 0, -1, 0) V(hw, -hh, -hd, 0, -1, 0) V(hw, -hh, hd, 0, -1, 0)
    V(-hw, -hh, -hd, 0, -1, 0) V(hw, -hh, hd, 0, -1, 0) V(-hw, -hh, hd, 0, -1, 0)
    V(-hw, -hh, hd, 0, 0, 1) V(hw, -hh, hd, 0, 0, 1) V(hw, hh, hd, 0, 0, 1)
    V(-hw, -hh, hd, 0, 0, 1) V(hw, hh, hd, 0, 0, 1) V(-hw, hh, hd, 0, 0, 1)
    V(hw, -hh, -hd, 0, 0, -1) V(-hw, -hh, -hd, 0, 0, -1) V(-hw, hh, -hd, 0, 0, -1)
    V(hw, -hh, -hd, 0, 0, -1) V(-hw, hh, -hd, 0, 0, -1) V(hw, hh, -hd, 0, 0, -1)
    V(-hw, -hh, -hd, -1, 0, 0) V(-hw, -hh, hd, -1, 0, 0) V(-hw, hh, hd, -1, 0, 0)
    V(-hw, -hh, -hd, -1, 0, 0) V(-hw, hh, hd, -1, 0, 0) V(-hw, hh, -hd, -1, 0, 0)
    V(hw, -hh, hd, 1, 0, 0) V(hw, -hh, -hd, 1, 0, 0) V(hw, hh, -hd, 1, 0, 0)
    V(hw, -hh, hd, 1, 0, 0) V(hw, hh, -hd, 1, 0, 0) V(hw, hh, hd, 1, 0, 0)
#undef V

    static const float glow_color[3] = {0.75f, 0.88f, 1.0f}; /* cool white-blue, real IPS-panel backlight tone */
    const float intensity = 0.55f; /* additive -- kept moderate since the base pass is already forced full-bright */

    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    gl_use_program(g_ips_light_shader_program);
    gl_uniform_matrix4fv(gl_get_uniform_location(g_ips_light_shader_program, "u_mvp"), mvp16);
    gl_uniform3fv(gl_get_uniform_location(g_ips_light_shader_program, "u_cam_pos"), cam_pos);
    gl_uniform3fv(gl_get_uniform_location(g_ips_light_shader_program, "u_glow_color"), glow_color);
    gl_uniform1f(gl_get_uniform_location(g_ips_light_shader_program, "u_intensity"), intensity);
    gl_dynamic_vbo_draw(&g_ips_light_shader_vbo, verts, 36, GL_TRIANGLES);
    gl_use_program(0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
}

#endif
