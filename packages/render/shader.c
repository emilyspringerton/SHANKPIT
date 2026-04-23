#include "shader.h"

#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>

typedef GLuint (*PFNGLCREATESHADERPROC)(GLenum type);
typedef void (*PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length);
typedef void (*PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void (*PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint *params);
typedef void (*PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef GLuint (*PFNGLCREATEPROGRAMPROC)(void);
typedef void (*PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void (*PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void (*PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint *params);
typedef void (*PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef void (*PFNGLDELETESHADERPROC)(GLuint shader);
typedef void (*PFNGLDELETEPROGRAMPROC)(GLuint program);
typedef void (*PFNGLUSEPROGRAMPROC)(GLuint program);
typedef GLint (*PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar *name);
typedef void (*PFNGLUNIFORM1IPROC)(GLint location, GLint v0);

static PFNGLCREATESHADERPROC p_glCreateShader = NULL;
static PFNGLSHADERSOURCEPROC p_glShaderSource = NULL;
static PFNGLCOMPILESHADERPROC p_glCompileShader = NULL;
static PFNGLGETSHADERIVPROC p_glGetShaderiv = NULL;
static PFNGLGETSHADERINFOLOGPROC p_glGetShaderInfoLog = NULL;
static PFNGLCREATEPROGRAMPROC p_glCreateProgram = NULL;
static PFNGLATTACHSHADERPROC p_glAttachShader = NULL;
static PFNGLLINKPROGRAMPROC p_glLinkProgram = NULL;
static PFNGLGETPROGRAMIVPROC p_glGetProgramiv = NULL;
static PFNGLGETPROGRAMINFOLOGPROC p_glGetProgramInfoLog = NULL;
static PFNGLDELETESHADERPROC p_glDeleteShader = NULL;
static PFNGLDELETEPROGRAMPROC p_glDeleteProgram = NULL;
static PFNGLUSEPROGRAMPROC p_glUseProgram = NULL;
static PFNGLGETUNIFORMLOCATIONPROC p_glGetUniformLocation = NULL;
static PFNGLUNIFORM1IPROC p_glUniform1i = NULL;

static int shader_api_ready = 0;

static int shader_resolve_api(void) {
    if (shader_api_ready) return 1;
    p_glCreateShader = (PFNGLCREATESHADERPROC)SDL_GL_GetProcAddress("glCreateShader");
    p_glShaderSource = (PFNGLSHADERSOURCEPROC)SDL_GL_GetProcAddress("glShaderSource");
    p_glCompileShader = (PFNGLCOMPILESHADERPROC)SDL_GL_GetProcAddress("glCompileShader");
    p_glGetShaderiv = (PFNGLGETSHADERIVPROC)SDL_GL_GetProcAddress("glGetShaderiv");
    p_glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)SDL_GL_GetProcAddress("glGetShaderInfoLog");
    p_glCreateProgram = (PFNGLCREATEPROGRAMPROC)SDL_GL_GetProcAddress("glCreateProgram");
    p_glAttachShader = (PFNGLATTACHSHADERPROC)SDL_GL_GetProcAddress("glAttachShader");
    p_glLinkProgram = (PFNGLLINKPROGRAMPROC)SDL_GL_GetProcAddress("glLinkProgram");
    p_glGetProgramiv = (PFNGLGETPROGRAMIVPROC)SDL_GL_GetProcAddress("glGetProgramiv");
    p_glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)SDL_GL_GetProcAddress("glGetProgramInfoLog");
    p_glDeleteShader = (PFNGLDELETESHADERPROC)SDL_GL_GetProcAddress("glDeleteShader");
    p_glDeleteProgram = (PFNGLDELETEPROGRAMPROC)SDL_GL_GetProcAddress("glDeleteProgram");
    p_glUseProgram = (PFNGLUSEPROGRAMPROC)SDL_GL_GetProcAddress("glUseProgram");
    p_glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)SDL_GL_GetProcAddress("glGetUniformLocation");
    p_glUniform1i = (PFNGLUNIFORM1IPROC)SDL_GL_GetProcAddress("glUniform1i");

    if (!p_glCreateShader || !p_glShaderSource || !p_glCompileShader || !p_glGetShaderiv || !p_glGetShaderInfoLog ||
        !p_glCreateProgram || !p_glAttachShader || !p_glLinkProgram || !p_glGetProgramiv || !p_glGetProgramInfoLog ||
        !p_glDeleteShader || !p_glDeleteProgram || !p_glUseProgram || !p_glGetUniformLocation || !p_glUniform1i) {
        printf("[GRASS_SHADER] GLSL API unavailable; falling back to legacy grass path\n");
        return 0;
    }

    shader_api_ready = 1;
    return 1;
}

static GLuint shader_compile(GLenum type, const char *source, const char *name) {
    GLint ok = 0;
    GLchar log_buf[512];
    GLuint shader = p_glCreateShader(type);
    p_glShaderSource(shader, 1, &source, NULL);
    p_glCompileShader(shader);
    p_glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        p_glGetShaderInfoLog(shader, (GLsizei)sizeof(log_buf), NULL, log_buf);
        printf("[GRASS_SHADER] %s compile failed: %s\n", name, log_buf);
        p_glDeleteShader(shader);
        return 0;
    }
    return shader;
}

int shader_build_program(ShaderProgram *out_program,
                         const char *debug_name,
                         const char *vertex_src,
                         const char *fragment_src) {
    if (!out_program) return 0;
    memset(out_program, 0, sizeof(*out_program));
    if (!shader_resolve_api()) return 0;

    GLuint vs = shader_compile(GL_VERTEX_SHADER, vertex_src, "vertex shader");
    if (!vs) return 0;
    GLuint fs = shader_compile(GL_FRAGMENT_SHADER, fragment_src, "fragment shader");
    if (!fs) {
        p_glDeleteShader(vs);
        return 0;
    }

    GLint ok = 0;
    GLchar log_buf[512];
    GLuint program = p_glCreateProgram();
    p_glAttachShader(program, vs);
    p_glAttachShader(program, fs);
    p_glLinkProgram(program);
    p_glGetProgramiv(program, GL_LINK_STATUS, &ok);
    p_glDeleteShader(vs);
    p_glDeleteShader(fs);
    if (!ok) {
        p_glGetProgramInfoLog(program, (GLsizei)sizeof(log_buf), NULL, log_buf);
        printf("[GRASS_SHADER] program link failed (%s): %s\n", debug_name ? debug_name : "unnamed", log_buf);
        p_glDeleteProgram(program);
        return 0;
    }

    out_program->program = program;
    out_program->ready = 1;
    return 1;
}

void shader_destroy_program(ShaderProgram *program) {
    if (!program || !program->ready || !shader_resolve_api()) return;
    p_glDeleteProgram(program->program);
    program->program = 0;
    program->ready = 0;
}

void shader_use_program(const ShaderProgram *program) {
    if (!program || !program->ready || !shader_resolve_api()) return;
    p_glUseProgram(program->program);
}

void shader_use_fixed_pipeline(void) {
    if (!shader_resolve_api()) return;
    p_glUseProgram(0);
}

GLint shader_get_uniform(GLuint program, const char *name) {
    if (!shader_resolve_api()) return -1;
    return p_glGetUniformLocation(program, name);
}

void shader_set_uniform_1i(GLint location, GLint value) {
    if (!shader_resolve_api()) return;
    if (location >= 0) p_glUniform1i(location, value);
}
