#ifndef SHANKPIT_SHADER_H
#define SHANKPIT_SHADER_H

#include <SDL2/SDL_opengl.h>

typedef struct ShaderProgram {
    GLuint program;
    int ready;
} ShaderProgram;

int shader_build_program(ShaderProgram *out_program,
                         const char *debug_name,
                         const char *vertex_src,
                         const char *fragment_src);
void shader_destroy_program(ShaderProgram *program);
void shader_use_program(const ShaderProgram *program);
void shader_use_fixed_pipeline(void);
GLint shader_get_uniform(GLuint program, const char *name);
void shader_set_uniform_1i(GLint location, GLint value);

#endif
