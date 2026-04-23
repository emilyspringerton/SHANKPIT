#include "grass_renderer.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "shader.h"

static ShaderProgram g_grass_program = {0};
static GLuint g_grass_tex = 0;
static GLint g_u_diffuse_loc = -1;

static const char *k_grass_vs =
"#version 120\n"
"varying vec2 v_uv;\n"
"varying vec4 v_color;\n"
"void main() {\n"
"    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
"    v_uv = gl_MultiTexCoord0.xy;\n"
"    v_color = gl_Color;\n"
"}\n";

static const char *k_grass_fs =
"#version 120\n"
"uniform sampler2D u_diffuse;\n"
"varying vec2 v_uv;\n"
"varying vec4 v_color;\n"
"void main() {\n"
"    vec4 t = texture2D(u_diffuse, v_uv);\n"
"    if (t.a < 0.45) discard;\n"
"    gl_FragColor = vec4(t.rgb * v_color.rgb, 1.0);\n"
"}\n";

static int grass_make_texture(void) {
    unsigned char pixels[32 * 32 * 4];
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            int idx = (y * 32 + x) * 4;
            float nx = ((float)x / 31.0f) * 2.0f - 1.0f;
            float ny = 1.0f - (float)y / 31.0f;
            float core = 1.0f - fabsf(nx);
            float blade = core * ny;
            float rag = ((x * 13 + y * 7) & 3) ? 1.0f : 0.85f;
            float alpha = blade * rag;
            if (ny < 0.1f) alpha *= ny * 10.0f;
            unsigned char a = (alpha > 0.18f) ? 255 : 0;
            pixels[idx + 0] = (unsigned char)(50 + (int)(40.0f * ny));
            pixels[idx + 1] = (unsigned char)(120 + (int)(95.0f * ny));
            pixels[idx + 2] = (unsigned char)(24 + (int)(26.0f * ny));
            pixels[idx + 3] = a;
        }
    }

    glGenTextures(1, &g_grass_tex);
    if (!g_grass_tex) return 0;
    glBindTexture(GL_TEXTURE_2D, g_grass_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    return 1;
}

int grass_renderer_init(GrassRenderer *renderer) {
    if (!renderer) return 0;
    memset(renderer, 0, sizeof(*renderer));
    renderer->initialized = 1;

    if (!shader_build_program(&g_grass_program, "voxworld_grass", k_grass_vs, k_grass_fs)) {
        printf("[GRASS_SHADER] shader program unavailable; using legacy grass renderer\n");
        renderer->available = 0;
        return 0;
    }

    g_u_diffuse_loc = shader_get_uniform(g_grass_program.program, "u_diffuse");
    if (!grass_make_texture()) {
        printf("[GRASS_SHADER] grass texture creation failed; using legacy grass renderer\n");
        shader_destroy_program(&g_grass_program);
        renderer->available = 0;
        return 0;
    }

    renderer->available = 1;
    printf("[GRASS_SHADER] initialized successfully\n");
    return 1;
}

void grass_renderer_shutdown(GrassRenderer *renderer) {
    if (!renderer || !renderer->initialized) return;
    if (g_grass_tex) {
        glDeleteTextures(1, &g_grass_tex);
        g_grass_tex = 0;
    }
    shader_destroy_program(&g_grass_program);
    renderer->initialized = 0;
    renderer->available = 0;
}

int grass_renderer_can_render(const GrassRenderer *renderer) {
    return renderer && renderer->initialized && renderer->available && g_grass_program.ready && g_grass_tex;
}

static void grass_emit_card(float x, float y, float z,
                            float half_w, float h,
                            float dx, float dz,
                            float mask, float fade, float tint) {
    float base_r = 0.17f + mask * 0.06f + tint * 0.04f;
    float base_g = 0.31f + mask * 0.17f + tint * 0.06f;
    float base_b = 0.12f + mask * 0.06f + tint * 0.03f;
    float top_r = base_r + 0.12f;
    float top_g = base_g + 0.20f;
    float top_b = base_b + 0.06f;

    glColor3f(base_r * fade, base_g * fade, base_b * fade);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(x - dx * half_w, y, z - dz * half_w);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(x + dx * half_w, y, z + dz * half_w);
    glColor3f(top_r * fade, top_g * fade, top_b * fade);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(x + dx * half_w * 0.35f, y + h, z + dz * half_w * 0.35f);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(x - dx * half_w * 0.35f, y + h, z - dz * half_w * 0.35f);
}

static void grass_emit_ground_patch(float x, float y, float z, float half_w, float mask, float fade, float tint) {
    float patch = (0.26f + mask * 0.42f) * fade;
    float r = (0.18f + tint * 0.05f) * patch;
    float g = (0.33f + tint * 0.08f) * patch;
    float b = (0.11f + tint * 0.04f) * patch;
    float r2 = r * 1.18f;
    float g2 = g * 1.24f;
    float b2 = b * 1.12f;
    float hw = half_w * (1.7f + mask * 1.3f);
    float py = y + 0.03f;

    glColor3f(r, g, b);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(x - hw, py, z + hw);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(x + hw, py, z + hw);
    glColor3f(r2, g2, b2);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(x + hw, py, z - hw);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(x - hw, py, z - hw);
}

void grass_renderer_render(const GrassRenderer *renderer, const GrassInstance *instances, int count) {
    if (!grass_renderer_can_render(renderer) || !instances || count <= 0) return;

    glEnable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glBindTexture(GL_TEXTURE_2D, g_grass_tex);
    shader_use_program(&g_grass_program);
    shader_set_uniform_1i(g_u_diffuse_loc, 0);

    glBegin(GL_QUADS);
    for (int i = 0; i < count; i++) {
        const GrassInstance *g = &instances[i];
        grass_emit_ground_patch(g->x, g->y, g->z, g->half_width, g->mask, g->fade, g->tint);
        float c = cosf(g->yaw);
        float s = sinf(g->yaw);
        grass_emit_card(g->x, g->y, g->z, g->half_width, g->height, c, s, g->mask, g->fade, g->tint);
        grass_emit_card(g->x, g->y, g->z, g->half_width, g->height * 0.94f, -s, c, g->mask, g->fade, g->tint);
        float c3 = cosf(g->yaw + 1.0471976f);
        float s3 = sinf(g->yaw + 1.0471976f);
        grass_emit_card(g->x, g->y, g->z, g->half_width * 0.85f, g->height * 0.88f, c3, s3, g->mask, g->fade, g->tint);
    }
    glEnd();

    shader_use_fixed_pipeline();
    glColor3f(1.0f, 1.0f, 1.0f);
}
