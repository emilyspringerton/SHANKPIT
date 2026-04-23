#ifndef SHANKPIT_GRASS_RENDERER_H
#define SHANKPIT_GRASS_RENDERER_H

#include <SDL2/SDL_opengl.h>

typedef struct GrassInstance {
    float x;
    float y;
    float z;
    float half_width;
    float height;
    float yaw;
    float mask;
    float fade;
    float tint;
} GrassInstance;

typedef struct GrassRenderer {
    int initialized;
    int available;
} GrassRenderer;

int grass_renderer_init(GrassRenderer *renderer);
void grass_renderer_shutdown(GrassRenderer *renderer);
int grass_renderer_can_render(const GrassRenderer *renderer);
void grass_renderer_render(const GrassRenderer *renderer, const GrassInstance *instances, int count);

#endif
