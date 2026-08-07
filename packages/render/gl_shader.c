#include "gl_shader.h"
#include <SDL2/SDL.h>
#include <stddef.h>

/* Function pointers, loaded once by gl_shader_load_extensions(). Declared
 * static here (this translation unit only) -- callers only ever go through
 * the gl_* helper functions below, never these directly. */
static PFNGLCREATESHADERPROC       p_glCreateShader;
static PFNGLSHADERSOURCEPROC       p_glShaderSource;
static PFNGLCOMPILESHADERPROC      p_glCompileShader;
static PFNGLGETSHADERIVPROC        p_glGetShaderiv;
static PFNGLGETSHADERINFOLOGPROC   p_glGetShaderInfoLog;
static PFNGLDELETESHADERPROC       p_glDeleteShader;
static PFNGLCREATEPROGRAMPROC      p_glCreateProgram;
static PFNGLATTACHSHADERPROC       p_glAttachShader;
static PFNGLLINKPROGRAMPROC        p_glLinkProgram;
static PFNGLGETPROGRAMIVPROC       p_glGetProgramiv;
static PFNGLGETPROGRAMINFOLOGPROC  p_glGetProgramInfoLog;
static PFNGLDELETEPROGRAMPROC      p_glDeleteProgram;
static PFNGLUSEPROGRAMPROC         p_glUseProgram;
static PFNGLGENBUFFERSPROC         p_glGenBuffers;
static PFNGLBINDBUFFERPROC         p_glBindBuffer;
static PFNGLBUFFERDATAPROC         p_glBufferData;
static PFNGLBUFFERSUBDATAPROC      p_glBufferSubData;
static PFNGLDELETEBUFFERSPROC      p_glDeleteBuffers;
static PFNGLGENVERTEXARRAYSPROC    p_glGenVertexArrays;
static PFNGLBINDVERTEXARRAYPROC    p_glBindVertexArray;
static PFNGLDELETEVERTEXARRAYSPROC p_glDeleteVertexArrays;
static PFNGLVERTEXATTRIBPOINTERPROC     p_glVertexAttribPointer;
static PFNGLENABLEVERTEXATTRIBARRAYPROC p_glEnableVertexAttribArray;
static PFNGLBINDATTRIBLOCATIONPROC p_glBindAttribLocation;
static PFNGLGETUNIFORMLOCATIONPROC p_glGetUniformLocation;
static PFNGLUNIFORMMATRIX4FVPROC   p_glUniformMatrix4fv;
static PFNGLUNIFORM4FVPROC         p_glUniform4fv;

static int g_extensions_loaded = 0;

#define LOAD(var, name) do { \
        (var) = (void *)SDL_GL_GetProcAddress(name); \
        if (!(var)) { SDL_Log("gl_shader: missing GL entry point %s", name); ok = 0; } \
    } while (0)

int gl_shader_load_extensions(void) {
    int ok = 1;
    LOAD(p_glCreateShader, "glCreateShader");
    LOAD(p_glShaderSource, "glShaderSource");
    LOAD(p_glCompileShader, "glCompileShader");
    LOAD(p_glGetShaderiv, "glGetShaderiv");
    LOAD(p_glGetShaderInfoLog, "glGetShaderInfoLog");
    LOAD(p_glDeleteShader, "glDeleteShader");
    LOAD(p_glCreateProgram, "glCreateProgram");
    LOAD(p_glAttachShader, "glAttachShader");
    LOAD(p_glLinkProgram, "glLinkProgram");
    LOAD(p_glGetProgramiv, "glGetProgramiv");
    LOAD(p_glGetProgramInfoLog, "glGetProgramInfoLog");
    LOAD(p_glDeleteProgram, "glDeleteProgram");
    LOAD(p_glUseProgram, "glUseProgram");
    LOAD(p_glGenBuffers, "glGenBuffers");
    LOAD(p_glBindBuffer, "glBindBuffer");
    LOAD(p_glBufferData, "glBufferData");
    LOAD(p_glBufferSubData, "glBufferSubData");
    LOAD(p_glDeleteBuffers, "glDeleteBuffers");
    LOAD(p_glGenVertexArrays, "glGenVertexArrays");
    LOAD(p_glBindVertexArray, "glBindVertexArray");
    LOAD(p_glDeleteVertexArrays, "glDeleteVertexArrays");
    LOAD(p_glVertexAttribPointer, "glVertexAttribPointer");
    LOAD(p_glEnableVertexAttribArray, "glEnableVertexAttribArray");
    LOAD(p_glBindAttribLocation, "glBindAttribLocation");
    LOAD(p_glGetUniformLocation, "glGetUniformLocation");
    LOAD(p_glUniformMatrix4fv, "glUniformMatrix4fv");
    LOAD(p_glUniform4fv, "glUniform4fv");
    g_extensions_loaded = ok;
    return ok;
}

#undef LOAD

GLuint gl_compile_shader(GLenum type, const char *src) {
    if (!g_extensions_loaded) return 0;
    GLuint shader = p_glCreateShader(type);
    if (!shader) return 0;
    p_glShaderSource(shader, 1, &src, NULL);
    p_glCompileShader(shader);

    GLint status = 0;
    p_glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[1024];
        GLsizei len = 0;
        p_glGetShaderInfoLog(shader, sizeof(log), &len, log);
        SDL_Log("gl_shader: shader compile failed: %.*s", (int)len, log);
        p_glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint gl_link_program(GLuint vertex_shader, GLuint fragment_shader) {
    if (!g_extensions_loaded || !vertex_shader || !fragment_shader) return 0;
    GLuint prog = p_glCreateProgram();
    if (!prog) return 0;
    p_glAttachShader(prog, vertex_shader);
    p_glAttachShader(prog, fragment_shader);
    /* Fixed attribute layout contract, matching DynamicVBO's own fixed
     * pos+normal layout: vertex shaders used with this module must name
     * their position attribute "a_pos" and normal attribute "a_normal".
     * Bound here (must happen before linking; GLSL 120 has no
     * layout(location=N) qualifier to do this from shader source). */
    p_glBindAttribLocation(prog, 0, "a_pos");
    p_glBindAttribLocation(prog, 1, "a_normal");
    p_glLinkProgram(prog);

    GLint status = 0;
    p_glGetProgramiv(prog, GL_LINK_STATUS, &status);
    /* Shader objects are refcounted once attached; safe to delete our
     * references regardless of link success -- they stay alive only if
     * still attached to a live program, and are freed here either way. */
    p_glDeleteShader(vertex_shader);
    p_glDeleteShader(fragment_shader);
    if (!status) {
        char log[1024];
        GLsizei len = 0;
        p_glGetProgramInfoLog(prog, sizeof(log), &len, log);
        SDL_Log("gl_shader: program link failed: %.*s", (int)len, log);
        p_glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

void gl_use_program(GLuint program) {
    if (!g_extensions_loaded) return;
    p_glUseProgram(program);
}

GLint gl_get_uniform_location(GLuint program, const char *name) {
    if (!g_extensions_loaded) return -1;
    return p_glGetUniformLocation(program, name);
}

void gl_uniform_matrix4fv(GLint location, const float *m16) {
    if (!g_extensions_loaded || location < 0) return;
    p_glUniformMatrix4fv(location, 1, GL_FALSE, m16);
}

void gl_uniform4fv(GLint location, const float *v4) {
    if (!g_extensions_loaded || location < 0) return;
    p_glUniform4fv(location, 1, v4);
}

int gl_dynamic_vbo_init(DynamicVBO *vbo, GLsizei capacity_verts) {
    if (!g_extensions_loaded || !vbo || capacity_verts <= 0) return 0;
    vbo->capacity_verts = capacity_verts;
    vbo->overflow_warned = 0;

    p_glGenVertexArrays(1, &vbo->vao);
    p_glGenBuffers(1, &vbo->vbo);
    p_glBindVertexArray(vbo->vao);
    p_glBindBuffer(GL_ARRAY_BUFFER, vbo->vbo);

    GLsizeiptr bytes = (GLsizeiptr)capacity_verts * GL_SHADER_VBO_FLOATS_PER_VERT * sizeof(GLfloat);
    p_glBufferData(GL_ARRAY_BUFFER, bytes, NULL, GL_STREAM_DRAW);

    GLsizei stride = GL_SHADER_VBO_FLOATS_PER_VERT * sizeof(GLfloat);
    p_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
    p_glEnableVertexAttribArray(0);
    p_glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void *)(3 * sizeof(GLfloat)));
    p_glEnableVertexAttribArray(1);

    p_glBindVertexArray(0);
    p_glBindBuffer(GL_ARRAY_BUFFER, 0);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        SDL_Log("gl_shader: gl_dynamic_vbo_init GL error 0x%x", err);
        return 0;
    }
    return 1;
}

void gl_dynamic_vbo_destroy(DynamicVBO *vbo) {
    if (!g_extensions_loaded || !vbo) return;
    if (vbo->vbo) p_glDeleteBuffers(1, &vbo->vbo);
    if (vbo->vao) p_glDeleteVertexArrays(1, &vbo->vao);
    vbo->vbo = 0;
    vbo->vao = 0;
}

void gl_dynamic_vbo_draw(DynamicVBO *vbo, const float *verts, GLsizei vert_count, GLenum mode) {
    if (!g_extensions_loaded || !vbo || !vbo->vbo || !verts || vert_count <= 0) return;
    if (vert_count > vbo->capacity_verts) {
        if (!vbo->overflow_warned) {
            SDL_Log("gl_shader: vert_count %d exceeds VBO capacity %d, dropping draw call (logged once)",
                    (int)vert_count, (int)vbo->capacity_verts);
            vbo->overflow_warned = 1;
        }
        return;
    }

    p_glBindVertexArray(vbo->vao);
    p_glBindBuffer(GL_ARRAY_BUFFER, vbo->vbo);

    /* Orphan the buffer (glBufferData(NULL)) before glBufferSubData so the
     * driver can hand back a fresh allocation instead of blocking on the
     * previous frame's draw still reading the old one -- the real fix
     * GFD's battlegrounds_gui needed for its own per-frame skinned-mesh
     * upload (see GOLDENBAND_INTEGRATION_NORTHSTAR.md). */
    GLsizeiptr full_bytes = (GLsizeiptr)vbo->capacity_verts * GL_SHADER_VBO_FLOATS_PER_VERT * sizeof(GLfloat);
    p_glBufferData(GL_ARRAY_BUFFER, full_bytes, NULL, GL_STREAM_DRAW);
    GLsizeiptr used_bytes = (GLsizeiptr)vert_count * GL_SHADER_VBO_FLOATS_PER_VERT * sizeof(GLfloat);
    p_glBufferSubData(GL_ARRAY_BUFFER, 0, used_bytes, verts);

    glDrawArrays(mode, 0, vert_count);

    p_glBindVertexArray(0);
    p_glBindBuffer(GL_ARRAY_BUFFER, 0);
}
