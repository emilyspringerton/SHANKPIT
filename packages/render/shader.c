#include "shader.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_opengl_glext.h>
#include <stdio.h>

static PFNGLCREATESHADERPROC p_glCreateShader = NULL;
static PFNGLSHADERSOURCEPROC p_glShaderSource = NULL;
static PFNGLCOMPILESHADERPROC p_glCompileShader = NULL;
static PFNGLGETSHADERIVPROC p_glGetShaderiv = NULL;
static PFNGLGETSHADERINFOLOGPROC p_glGetShaderInfoLog = NULL;
static PFNGLDELETESHADERPROC p_glDeleteShader = NULL;
static PFNGLCREATEPROGRAMPROC p_glCreateProgram = NULL;
static PFNGLATTACHSHADERPROC p_glAttachShader = NULL;
static PFNGLLINKPROGRAMPROC p_glLinkProgram = NULL;
static PFNGLGETPROGRAMIVPROC p_glGetProgramiv = NULL;
static PFNGLGETPROGRAMINFOLOGPROC p_glGetProgramInfoLog = NULL;
static PFNGLDELETEPROGRAMPROC p_glDeleteProgram = NULL;
static PFNGLUSEPROGRAMPROC p_glUseProgram = NULL;
static PFNGLGETUNIFORMLOCATIONPROC p_glGetUniformLocation = NULL;
static PFNGLUNIFORM1IPROC p_glUniform1i = NULL;

static int g_shader_init = 0;
static int g_shader_available = 0;

static void *load_gl_sym(const char *name) {
    void *p = SDL_GL_GetProcAddress(name);
    if (!p) printf("[SHADER] missing GL symbol: %s\n", name);
    return p;
}

int shank_shader_init(void) {
    if (g_shader_init) return g_shader_available;
    g_shader_init = 1;

    p_glCreateShader = (PFNGLCREATESHADERPROC)load_gl_sym("glCreateShader");
    p_glShaderSource = (PFNGLSHADERSOURCEPROC)load_gl_sym("glShaderSource");
    p_glCompileShader = (PFNGLCOMPILESHADERPROC)load_gl_sym("glCompileShader");
    p_glGetShaderiv = (PFNGLGETSHADERIVPROC)load_gl_sym("glGetShaderiv");
    p_glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)load_gl_sym("glGetShaderInfoLog");
    p_glDeleteShader = (PFNGLDELETESHADERPROC)load_gl_sym("glDeleteShader");
    p_glCreateProgram = (PFNGLCREATEPROGRAMPROC)load_gl_sym("glCreateProgram");
    p_glAttachShader = (PFNGLATTACHSHADERPROC)load_gl_sym("glAttachShader");
    p_glLinkProgram = (PFNGLLINKPROGRAMPROC)load_gl_sym("glLinkProgram");
    p_glGetProgramiv = (PFNGLGETPROGRAMIVPROC)load_gl_sym("glGetProgramiv");
    p_glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)load_gl_sym("glGetProgramInfoLog");
    p_glDeleteProgram = (PFNGLDELETEPROGRAMPROC)load_gl_sym("glDeleteProgram");
    p_glUseProgram = (PFNGLUSEPROGRAMPROC)load_gl_sym("glUseProgram");
    p_glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)load_gl_sym("glGetUniformLocation");
    p_glUniform1i = (PFNGLUNIFORM1IPROC)load_gl_sym("glUniform1i");

    g_shader_available = p_glCreateShader && p_glShaderSource && p_glCompileShader &&
                         p_glGetShaderiv && p_glGetShaderInfoLog && p_glDeleteShader &&
                         p_glCreateProgram && p_glAttachShader && p_glLinkProgram &&
                         p_glGetProgramiv && p_glGetProgramInfoLog && p_glDeleteProgram &&
                         p_glUseProgram && p_glGetUniformLocation && p_glUniform1i;

    if (!g_shader_available) {
        printf("[SHADER] OpenGL shader path unavailable; grass will use legacy rendering.\n");
    }
    return g_shader_available;
}

int shank_shader_is_available(void) {
    return g_shader_available;
}

static GLuint compile_shader_stage(const char *label, GLenum type, const char *src) {
    GLint ok = 0;
    GLuint s = p_glCreateShader(type);
    if (!s) return 0;

    p_glShaderSource(s, 1, &src, NULL);
    p_glCompileShader(s);
    p_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log_buf[2048];
        GLsizei out_len = 0;
        log_buf[0] = '\0';
        p_glGetShaderInfoLog(s, (GLsizei)sizeof(log_buf) - 1, &out_len, log_buf);
        log_buf[(out_len >= 0 && out_len < (GLsizei)sizeof(log_buf)) ? out_len : ((GLsizei)sizeof(log_buf) - 1)] = '\0';
        printf("[SHADER] %s %s compile failed: %s\n", label, type == GL_VERTEX_SHADER ? "vertex" : "fragment", log_buf);
        p_glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint shank_shader_build(const char *label, const char *vertex_src, const char *fragment_src) {
    if (!shank_shader_init()) return 0;

    GLuint vs = compile_shader_stage(label, GL_VERTEX_SHADER, vertex_src);
    if (!vs) return 0;
    GLuint fs = compile_shader_stage(label, GL_FRAGMENT_SHADER, fragment_src);
    if (!fs) {
        p_glDeleteShader(vs);
        return 0;
    }

    GLuint prog = p_glCreateProgram();
    if (!prog) {
        p_glDeleteShader(vs);
        p_glDeleteShader(fs);
        return 0;
    }

    p_glAttachShader(prog, vs);
    p_glAttachShader(prog, fs);
    p_glLinkProgram(prog);

    GLint link_ok = 0;
    p_glGetProgramiv(prog, GL_LINK_STATUS, &link_ok);
    p_glDeleteShader(vs);
    p_glDeleteShader(fs);
    if (!link_ok) {
        char log_buf[2048];
        GLsizei out_len = 0;
        log_buf[0] = '\0';
        p_glGetProgramInfoLog(prog, (GLsizei)sizeof(log_buf) - 1, &out_len, log_buf);
        log_buf[(out_len >= 0 && out_len < (GLsizei)sizeof(log_buf)) ? out_len : ((GLsizei)sizeof(log_buf) - 1)] = '\0';
        printf("[SHADER] %s link failed: %s\n", label, log_buf);
        p_glDeleteProgram(prog);
        return 0;
    }

    return prog;
}

void shank_shader_destroy(GLuint *program) {
    if (!program || !*program || !shank_shader_is_available()) return;
    p_glDeleteProgram(*program);
    *program = 0;
}

void shank_shader_use(GLuint program) {
    if (!shank_shader_is_available()) return;
    p_glUseProgram(program);
}

void shank_shader_stop(void) {
    if (!shank_shader_is_available()) return;
    p_glUseProgram(0);
}

GLint shank_shader_uniform(GLuint program, const char *name) {
    if (!program || !name || !shank_shader_is_available()) return -1;
    return p_glGetUniformLocation(program, name);
}

void shank_shader_uniform1i(GLint location, GLint v0) {
    if (location < 0 || !shank_shader_is_available()) return;
    p_glUniform1i(location, v0);
}
