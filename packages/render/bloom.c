// bloom.c -- see bloom.h for the real design rationale.
#include "bloom.h"
#include "gl_shader.h"
#include <SDL2/SDL.h>
#include <stddef.h>

// FBO/renderbuffer function pointers -- beyond what gl_shader.c already loads (that file's own
// p_gl* statics are private to its translation unit, so this module loads its own, same real
// "SDL_GL_GetProcAddress by hand, no GLEW/glad" convention, same reason: the Windows cross-
// compile target only gets GL 1.1 for free from opengl32.dll).
static PFNGLGENFRAMEBUFFERSPROC        p_glGenFramebuffers;
static PFNGLBINDFRAMEBUFFERPROC        p_glBindFramebuffer;
static PFNGLFRAMEBUFFERTEXTURE2DPROC   p_glFramebufferTexture2D;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC p_glCheckFramebufferStatus;
static PFNGLDELETEFRAMEBUFFERSPROC     p_glDeleteFramebuffers;
static PFNGLGENRENDERBUFFERSPROC       p_glGenRenderbuffers;
static PFNGLBINDRENDERBUFFERPROC       p_glBindRenderbuffer;
static PFNGLRENDERBUFFERSTORAGEPROC    p_glRenderbufferStorage;
static PFNGLFRAMEBUFFERRENDERBUFFERPROC p_glFramebufferRenderbuffer;
static PFNGLDELETERENDERBUFFERSPROC    p_glDeleteRenderbuffers;

static int g_bloom_ready = 0;
static GLuint g_bright_program = 0;
static GLuint g_blur_program = 0;
static DynamicVBO g_quad_vbo;

// Real, current allocated sizes -- 0 until the first real bloom_begin_scene call, forcing a real
// (re)allocation the first time and any time the window is resized (SDL_WINDOW_RESIZABLE).
static int g_scene_w = 0, g_scene_h = 0;
static int g_small_w = 0, g_small_h = 0; // bright/blur targets, a real, deliberate quarter-res downsample

static GLuint g_scene_fbo = 0, g_scene_tex = 0, g_scene_depth_rb = 0;
static GLuint g_bright_fbo = 0, g_bright_tex = 0;
static GLuint g_blur_fbo_a = 0, g_blur_tex_a = 0;
static GLuint g_blur_fbo_b = 0, g_blur_tex_b = 0;

// The bright-pass fragment shader's own hardcoded 0.82 luminance cutoff (Rec.709 weights, see
// g_bright_fs_src below) is tuned so this engine's own already-bright emissive materials
// (SHADER_HPS_LIGHT/SHADER_IPS_LIGHT surface tint, both already near or at 1.0 in at least one
// channel, material_shaders.h) cross it, while an ordinary lit wall (the real per-face lighting
// this same pass, S493, keeps in a moderate range once the enclosed-lighting fix stops it sitting
// near-max-bright indoors) does not.

static const char *g_quad_vs_src =
    "#version 120\n"
    "attribute vec3 a_pos;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = a_pos.xy * 0.5 + 0.5;\n"
    "    gl_Position = vec4(a_pos.xy, 0.0, 1.0);\n"
    "}\n";

static const char *g_bright_fs_src =
    "#version 120\n"
    "uniform sampler2D u_scene;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    vec3 c = texture2D(u_scene, v_uv).rgb;\n"
    "    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
    "    float over = max(lum - 0.82, 0.0);\n"
    "    float k = over / max(lum, 0.0001);\n"
    "    gl_FragColor = vec4(c * k, 1.0);\n"
    "}\n";

// Real, standard 5-tap linear-sampled separable Gaussian approximation (weights/offsets from the
// well-known "efficient Gaussian blur" technique exploiting bilinear texture filtering to halve
// the real sample count) -- cheap enough for a retro-styled engine's own real per-frame budget,
// run twice (horizontal then vertical, u_dir switches which) for a real 2D blur.
static const char *g_blur_fs_src =
    "#version 120\n"
    "uniform sampler2D u_tex;\n"
    // Two real scalar uniforms, not a vec2 -- gl_shader.h's own uniform-setter helpers
    // (gl_uniform1f/3fv/4fv/matrix4fv) have no 2-component variant, and GL raises
    // GL_INVALID_OPERATION (a silent no-op, not a crash) on a component-count mismatch between a
    // uniform's real GLSL-declared type and the setter function called against it -- confirmed
    // the wrong way once already in an earlier draft of this file (called gl_uniform3fv against
    // a vec2), which would have silently left u_texel_dir at its real default (0,0) forever,
    // i.e. a blur pass that samples the same texel 5 times and never actually blurs anything.
    "uniform float u_texel_x;\n"
    "uniform float u_texel_y;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    vec2 dir = vec2(u_texel_x, u_texel_y);\n"
    "    vec3 sum = texture2D(u_tex, v_uv).rgb * 0.227027;\n"
    "    vec2 off1 = dir * 1.384615;\n"
    "    vec2 off2 = dir * 3.230769;\n"
    "    sum += texture2D(u_tex, v_uv + off1).rgb * 0.316216;\n"
    "    sum += texture2D(u_tex, v_uv - off1).rgb * 0.316216;\n"
    "    sum += texture2D(u_tex, v_uv + off2).rgb * 0.070270;\n"
    "    sum += texture2D(u_tex, v_uv - off2).rgb * 0.070270;\n"
    "    gl_FragColor = vec4(sum, 1.0);\n"
    "}\n";

#define LOAD(var, name) do { \
        (var) = (void *)SDL_GL_GetProcAddress(name); \
        if (!(var)) { SDL_Log("bloom: missing GL entry point %s", name); ok = 0; } \
    } while (0)

static int bloom_load_fbo_extensions(void) {
    int ok = 1;
    LOAD(p_glGenFramebuffers, "glGenFramebuffers");
    LOAD(p_glBindFramebuffer, "glBindFramebuffer");
    LOAD(p_glFramebufferTexture2D, "glFramebufferTexture2D");
    LOAD(p_glCheckFramebufferStatus, "glCheckFramebufferStatus");
    LOAD(p_glDeleteFramebuffers, "glDeleteFramebuffers");
    LOAD(p_glGenRenderbuffers, "glGenRenderbuffers");
    LOAD(p_glBindRenderbuffer, "glBindRenderbuffer");
    LOAD(p_glRenderbufferStorage, "glRenderbufferStorage");
    LOAD(p_glFramebufferRenderbuffer, "glFramebufferRenderbuffer");
    LOAD(p_glDeleteRenderbuffers, "glDeleteRenderbuffers");
    return ok;
}

#undef LOAD

int bloom_init(void) {
    if (!gl_shader_load_extensions() || !bloom_load_fbo_extensions()) {
        SDL_Log("bloom: GL extension loading failed -- bloom disabled");
        return 0;
    }
    GLuint bvs = gl_compile_shader(GL_VERTEX_SHADER, g_quad_vs_src);
    GLuint bfs = gl_compile_shader(GL_FRAGMENT_SHADER, g_bright_fs_src);
    g_bright_program = gl_link_program(bvs, bfs);
    GLuint lvs = gl_compile_shader(GL_VERTEX_SHADER, g_quad_vs_src);
    GLuint lfs = gl_compile_shader(GL_FRAGMENT_SHADER, g_blur_fs_src);
    g_blur_program = gl_link_program(lvs, lfs);
    if (!g_bright_program || !g_blur_program) {
        SDL_Log("bloom: shader link failed -- bloom disabled");
        return 0;
    }
    // Full-screen quad, GL_TRIANGLE_STRIP order, NDC (-1..1) -- reuses DynamicVBO's own fixed
    // pos.xyz+normal.xyz (6 floats/vert) layout for real, proven infra reuse rather than hand-
    // rolling a second VBO type; the vertex shader above only declares a_pos (vec3, .xy used),
    // never a_normal, so the 3 trailing zeros per vertex are simply unread padding.
    if (!gl_dynamic_vbo_init(&g_quad_vbo, 4)) {
        SDL_Log("bloom: quad VBO init failed -- bloom disabled");
        return 0;
    }
    g_bloom_ready = 1;
    return 1;
}

static GLuint make_color_target(GLuint *out_fbo, int w, int h) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    p_glGenFramebuffers(1, out_fbo);
    p_glBindFramebuffer(GL_FRAMEBUFFER, *out_fbo);
    p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    return tex;
}

static void free_target(GLuint *fbo, GLuint *tex) {
    if (*tex) { glDeleteTextures(1, tex); *tex = 0; }
    if (*fbo) { p_glDeleteFramebuffers(1, fbo); *fbo = 0; }
}

// (re)allocate_targets -- real, necessary response to SDL_WINDOW_RESIZABLE (apps/lobby's own
// window creation flags): called from bloom_begin_scene whenever the requested size differs from
// what's currently allocated, INCLUDING the very first call (0,0 never matches a real size).
static void reallocate_targets(int width, int height) {
    if (width == g_scene_w && height == g_scene_h) return;
    free_target(&g_scene_fbo, &g_scene_tex);
    if (g_scene_depth_rb) { p_glDeleteRenderbuffers(1, &g_scene_depth_rb); g_scene_depth_rb = 0; }
    free_target(&g_bright_fbo, &g_bright_tex);
    free_target(&g_blur_fbo_a, &g_blur_tex_a);
    free_target(&g_blur_fbo_b, &g_blur_tex_b);

    g_scene_w = width; g_scene_h = height;
    // Quarter-res for the bright/blur chain -- a real, deliberate glow-radius/perf tradeoff (a
    // blur kernel at full scene resolution would need a much wider kernel, or many more passes,
    // to read as a soft glow at all -- downsampling first is the standard, real technique).
    g_small_w = width / 4 > 0 ? width / 4 : 1;
    g_small_h = height / 4 > 0 ? height / 4 : 1;

    g_scene_tex = make_color_target(&g_scene_fbo, g_scene_w, g_scene_h);
    p_glGenRenderbuffers(1, &g_scene_depth_rb);
    p_glBindRenderbuffer(GL_RENDERBUFFER, g_scene_depth_rb);
    p_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, g_scene_w, g_scene_h);
    p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_scene_depth_rb);
    if (p_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        SDL_Log("bloom: scene FBO incomplete -- bloom disabled");
        g_bloom_ready = 0;
    }

    g_bright_tex = make_color_target(&g_bright_fbo, g_small_w, g_small_h);
    g_blur_tex_a = make_color_target(&g_blur_fbo_a, g_small_w, g_small_h);
    g_blur_tex_b = make_color_target(&g_blur_fbo_b, g_small_w, g_small_h);

    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void bloom_begin_scene(int width, int height) {
    if (!g_bloom_ready) return;
    if (width <= 0 || height <= 0) return;
    reallocate_targets(width, height);
    if (!g_bloom_ready) return; // reallocate_targets may have just disabled it (incomplete FBO)
    p_glBindFramebuffer(GL_FRAMEBUFFER, g_scene_fbo);
    glViewport(0, 0, g_scene_w, g_scene_h);
}

void bloom_end_scene_and_composite(int width, int height) {
    if (!g_bloom_ready) return;
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (width <= 0 || height <= 0) return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);

    // Pass 1: bright-pass threshold extract, scene_tex -> bright_tex (quarter-res).
    p_glBindFramebuffer(GL_FRAMEBUFFER, g_bright_fbo);
    glViewport(0, 0, g_small_w, g_small_h);
    gl_use_program(g_bright_program);
    glBindTexture(GL_TEXTURE_2D, g_scene_tex);
    gl_dynamic_vbo_draw(&g_quad_vbo, (const float[]){
        -1.0f, -1.0f, 0.0f, 0, 0, 0,  1.0f, -1.0f, 0.0f, 0, 0, 0,
        -1.0f,  1.0f, 0.0f, 0, 0, 0,  1.0f,  1.0f, 0.0f, 0, 0, 0,
    }, 4, GL_TRIANGLE_STRIP);

    // Pass 2: horizontal blur, bright_tex -> blur_tex_a.
    p_glBindFramebuffer(GL_FRAMEBUFFER, g_blur_fbo_a);
    gl_use_program(g_blur_program);
    glBindTexture(GL_TEXTURE_2D, g_bright_tex);
    gl_uniform1f(gl_get_uniform_location(g_blur_program, "u_texel_x"), 1.0f / (float)g_small_w);
    gl_uniform1f(gl_get_uniform_location(g_blur_program, "u_texel_y"), 0.0f);
    gl_dynamic_vbo_draw(&g_quad_vbo, (const float[]){
        -1.0f, -1.0f, 0.0f, 0, 0, 0,  1.0f, -1.0f, 0.0f, 0, 0, 0,
        -1.0f,  1.0f, 0.0f, 0, 0, 0,  1.0f,  1.0f, 0.0f, 0, 0, 0,
    }, 4, GL_TRIANGLE_STRIP);

    // Pass 3: vertical blur, blur_tex_a -> blur_tex_b.
    p_glBindFramebuffer(GL_FRAMEBUFFER, g_blur_fbo_b);
    gl_use_program(g_blur_program);
    glBindTexture(GL_TEXTURE_2D, g_blur_tex_a);
    gl_uniform1f(gl_get_uniform_location(g_blur_program, "u_texel_x"), 0.0f);
    gl_uniform1f(gl_get_uniform_location(g_blur_program, "u_texel_y"), 1.0f / (float)g_small_h);
    gl_dynamic_vbo_draw(&g_quad_vbo, (const float[]){
        -1.0f, -1.0f, 0.0f, 0, 0, 0,  1.0f, -1.0f, 0.0f, 0, 0, 0,
        -1.0f,  1.0f, 0.0f, 0, 0, 0,  1.0f,  1.0f, 0.0f, 0, 0, 0,
    }, 4, GL_TRIANGLE_STRIP);

    // Composite onto the REAL default framebuffer: base scene (plain fixed-function textured
    // quad, no shader -- a straight copy), then the blurred bright-pass on top with additive
    // blending so bright areas genuinely bleed past their own edges.
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    gl_use_program(0); // back to fixed-function for both quads below
    glEnable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();

    glBindTexture(GL_TEXTURE_2D, g_scene_tex);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(-1, -1);
    glTexCoord2f(1, 0); glVertex2f(1, -1);
    glTexCoord2f(1, 1); glVertex2f(1, 1);
    glTexCoord2f(0, 1); glVertex2f(-1, 1);
    glEnd();

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE); // additive
    glBindTexture(GL_TEXTURE_2D, g_blur_tex_b);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(-1, -1);
    glTexCoord2f(1, 0); glVertex2f(1, -1);
    glTexCoord2f(1, 1); glVertex2f(1, 1);
    glTexCoord2f(0, 1); glVertex2f(-1, 1);
    glEnd();
    glDisable(GL_BLEND);

    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
    glEnable(GL_DEPTH_TEST);
    // This function's own composite quads turn texturing on (line ~254) and leave it bound to
    // g_blur_tex_b (the bloom blur target -- near-black for most scenes) -- unlike depth test,
    // that never got turned back off. Anything drawn next with GL_MODULATE and no texcoords of
    // its own (the lobby's 2D menu quads/text, once the player returns from any 3D mode) samples
    // that leftover dark texture and renders black regardless of glColor. Found live, 2026-09-25.
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}
