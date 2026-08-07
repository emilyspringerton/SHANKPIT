#ifndef GL_SHADER_H
#define GL_SHADER_H

/* SHANKPIT's first shader/VBO subsystem (S144-02, founder: "build shankpit
 * shader dynamic vbo"). Every render call site in this codebase before this
 * used fixed-function immediate mode (glBegin/glVertex3f) -- there is no
 * GLEW/glad here by design (same no-external-dependency convention as
 * hmac_sha256.h/http_client.h), so GL 2.0+ entry points are loaded by hand
 * via SDL_GL_GetProcAddress. This is required for portability: on Linux,
 * libGL.so exports these symbols directly and could be linked without this
 * file, but the Windows EA cross-compile target (i686-w64-mingw32-gcc) only
 * gets GL 1.1 for free from opengl32.dll -- everything past that must be
 * loaded dynamically on that platform, so this file does it the same way
 * everywhere rather than diverging by platform. */

#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_opengl_glext.h>

/* Loads GL 2.0+ function pointers via SDL_GL_GetProcAddress. Must be called
 * once after SDL_GL_CreateContext, before any other gl_shader_* function.
 * Returns 1 on success, 0 if any required entry point is missing (very old
 * driver or a software renderer with no shader support) -- callers must
 * fall back to the existing fixed-function path rather than call into an
 * unloaded function pointer. */
int gl_shader_load_extensions(void);

/* Compiles one shader stage from source. Returns 0 on failure and prints
 * the real compiler error log via SDL_Log (never crashes on a bad shader). */
GLuint gl_compile_shader(GLenum type, const char *src);

/* Links a vertex+fragment pair into a program. The two shader objects are
 * deleted after linking (an object stays alive via the program's own
 * refcount once attached+linked, so this doesn't leak). Returns 0 on
 * failure and prints the real linker error log.
 *
 * Fixed attribute contract (bound here, before linking, since GLSL 120 has
 * no layout(location=N) qualifier): the vertex shader's position attribute
 * must be named "a_pos" (bound to location 0) and its normal attribute
 * "a_normal" (location 1) -- matching DynamicVBO's own fixed pos+normal
 * layout below, so the two always agree without a runtime location query. */
GLuint gl_link_program(GLuint vertex_shader, GLuint fragment_shader);

/* Thin wrappers over the loaded function pointers so callers never touch
 * gl_shader.c's internals directly -- same shape as gl_compile_shader/
 * gl_link_program above. */
void gl_use_program(GLuint program);
GLint gl_get_uniform_location(GLuint program, const char *name);
void gl_uniform_matrix4fv(GLint location, const float *m16);
void gl_uniform4fv(GLint location, const float *v4);

/* A single reusable GPU buffer for CPU-generated geometry that changes
 * every frame (skinned mesh output, etc). Vertex layout is fixed: location
 * 0 = position (3 floats), location 1 = normal (3 floats) -- the exact
 * shape GOLDENBAND's gband_mesh_rig already emits, so no adapter struct is
 * needed between the rig and this buffer. */
typedef struct DynamicVBO {
    GLuint vao;
    GLuint vbo;
    GLsizei capacity_verts;
    int overflow_warned; /* logs the capacity-exceeded warning once, not every frame */
} DynamicVBO;

#define GL_SHADER_VBO_FLOATS_PER_VERT 6 /* pos.xyz + normal.xyz */

/* Allocates a VAO+VBO sized for capacity_verts vertices. Returns 1 on
 * success, 0 on failure (extensions not loaded, or GL_OUT_OF_MEMORY). */
int gl_dynamic_vbo_init(DynamicVBO *vbo, GLsizei capacity_verts);
void gl_dynamic_vbo_destroy(DynamicVBO *vbo);

/* Uploads vert_count vertices (GL_SHADER_VBO_FLOATS_PER_VERT floats each)
 * via glBufferData(NULL) + glBufferSubData (buffer orphaning -- avoids a
 * GPU sync stall on a buffer still in flight from the previous frame, the
 * same real fix GFD's battlegrounds_gui needed for its own skinned-mesh
 * VBO), then draws them with mode (e.g. GL_TRIANGLES). If vert_count
 * exceeds capacity, logs a warning once and draws nothing that frame rather
 * than overrunning the buffer -- the exact bug class GFD hit for real with
 * Tyler's 2922-vert mesh against a 512-vert buffer, caught here on purpose. */
void gl_dynamic_vbo_draw(DynamicVBO *vbo, const float *verts, GLsizei vert_count, GLenum mode);

#endif
