/* litbox_shader.h -- GLSL for per-fragment dynamic lighting of map boxes (#451/#452), shared by
 * apps/lobby (draw_map) and packages/render/litbox_render_test.c so the test exercises the exact
 * shader the game runs. See the block comment above litbox_shader_init in apps/lobby/src/main.c. */
#ifndef LITBOX_SHADER_H
#define LITBOX_SHADER_H

static const char *g_litbox_vs_src =
    "#version 120\n"
    "uniform vec3 u_center;\n"
    "uniform vec3 u_size;\n"
    "varying vec3 v_world;\n"
    "varying vec3 v_normal;\n"
    "void main() {\n"
    "    v_world = u_center + gl_Vertex.xyz * u_size;\n"
    "    v_normal = normalize(gl_Normal);\n"
    "    gl_FrontColor = gl_Color;\n"
    "    gl_TexCoord[0] = gl_MultiTexCoord0;\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
    "}\n";

static const char *g_litbox_fs_src =
    "#version 120\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec3 u_albedo;\n"
    "uniform int u_nspot;\n"
    "uniform int u_npoint;\n"
    "uniform vec4 u_spot_pos[8];\n"   /* xyz = eye, w = range */
    "uniform vec4 u_spot_dir[8];\n"   /* xyz = forward */
    "uniform float u_cone_cos;\n"     /* cos of the nominal half angle */
    "uniform vec4 u_point_pos[16];\n" /* xyz = position, w = range */
    "uniform vec4 u_point_col[16];\n"
    "varying vec3 v_world;\n"
    "varying vec3 v_normal;\n"
    "void main() {\n"
    "    vec4 tex = texture2D(u_tex, gl_TexCoord[0].xy);\n"
    "    vec3 n = normalize(v_normal);\n"
    "    vec3 dyn = vec3(0.0);\n"
    "    for (int i = 0; i < 8; i++) {\n"
    "        if (i >= u_nspot) break;\n"
    "        vec3 d = v_world - u_spot_pos[i].xyz;\n"
    "        float dist = length(d);\n"
    "        if (dist < 0.001) continue;\n"
    "        vec3 l = d / dist;\n"
    "        float x = (1.0 - dot(l, u_spot_dir[i].xyz)) / (1.0 - u_cone_cos);\n"  /* 0 on axis, 1 at the nominal rim */
    "        float spot = exp(-x * x * 2.2);\n"                                    /* Gaussian: no cone edge */
    "        float r = dist / u_spot_pos[i].w;\n"
    "        float fall = exp(-r * r * 4.0);\n"
    "        float ndl = max(0.0, -dot(n, l));\n"
    "        dyn += vec3(1.0, 0.96, 0.82) * (spot * fall * ndl * 1.6);\n"
    "    }\n"
    "    for (int j = 0; j < 16; j++) {\n"
    "        if (j >= u_npoint) break;\n"
    "        vec3 d = v_world - u_point_pos[j].xyz;\n"
    "        float dist = length(d);\n"
    "        if (dist < 0.001) continue;\n"
    "        float r = dist / u_point_pos[j].w;\n"
    "        float fall = max(0.0, exp(-r * r * 3.0) - 0.0498) / 0.9502;\n"        /* windowed to exactly 0 at the range */
    "        float ndl = clamp((-dot(n, d / dist) + 0.25) / 1.25, 0.0, 1.0);\n"    /* wrapped N.L */
    "        dyn += u_point_col[j].rgb * (fall * ndl * 1.5);\n"
    "    }\n"
    "    gl_FragColor = vec4(tex.rgb * (gl_Color.rgb + dyn * u_albedo), tex.a * gl_Color.a);\n"
    "}\n";

#endif
