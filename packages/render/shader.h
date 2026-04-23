#ifndef SHANK_SHADER_H
#define SHANK_SHADER_H

#include <SDL2/SDL_opengl.h>

int shank_shader_init(void);
int shank_shader_is_available(void);

GLuint shank_shader_build(const char *label, const char *vertex_src, const char *fragment_src);
void shank_shader_destroy(GLuint *program);
void shank_shader_use(GLuint program);
void shank_shader_stop(void);
GLint shank_shader_uniform(GLuint program, const char *name);
void shank_shader_uniform1i(GLint location, GLint v0);

#endif
