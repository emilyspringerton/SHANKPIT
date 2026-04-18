#define SDL_MAIN_HANDLED
#include "editor_app.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <GL/glu.h>

#include "editor_camera.h"
#include "editor_move_tool.h"
#include "editor_scene_asset.h"
#include "editor_scene_json.h"
#include "editor_selection.h"
#include "editor_ui.h"

typedef struct {
    SDL_Window *window;
    SDL_GLContext gl;
    int running;
    int width;
    int height;

    char active_scene_path[256];
    EditorSceneAsset scene;
    EditorViewportCamera camera;
    EditorMoveTool move_tool;

    int selected_box;
    int selected_stage_camera;
    int stage_camera_drives_viewport;

    int mouse_x;
    int mouse_y;
} EditorApp;

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void vec3_sub(const float a[3], const float b[3], float out[3]) {
    out[0] = a[0] - b[0]; out[1] = a[1] - b[1]; out[2] = a[2] - b[2];
}

static float vec3_dot(const float a[3], const float b[3]) {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

static void vec3_cross(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1]*b[2] - a[2]*b[1];
    out[1] = a[2]*b[0] - a[0]*b[2];
    out[2] = a[0]*b[1] - a[1]*b[0];
}

static void vec3_norm(float v[3]) {
    float len = sqrtf(vec3_dot(v, v));
    if (len > 1e-6f) {
        v[0] /= len; v[1] /= len; v[2] /= len;
    }
}

static int ray_from_mouse(const EditorApp *app, int x, int y, float out_origin[3], float out_dir[3]) {
    GLfloat model[16], proj[16];
    GLint view[4];
    glGetDoublev(GL_MODELVIEW_MATRIX, (GLdouble*)model);
    glGetDoublev(GL_PROJECTION_MATRIX, (GLdouble*)proj);
    glGetIntegerv(GL_VIEWPORT, view);

    double near_x, near_y, near_z;
    double far_x, far_y, far_z;
    if (!gluUnProject((double)x, (double)(app->height - y), 0.0, (GLdouble*)model, (GLdouble*)proj, view, &near_x, &near_y, &near_z)) return 0;
    if (!gluUnProject((double)x, (double)(app->height - y), 1.0, (GLdouble*)model, (GLdouble*)proj, view, &far_x, &far_y, &far_z)) return 0;

    out_origin[0] = (float)near_x;
    out_origin[1] = (float)near_y;
    out_origin[2] = (float)near_z;
    out_dir[0] = (float)(far_x - near_x);
    out_dir[1] = (float)(far_y - near_y);
    out_dir[2] = (float)(far_z - near_z);
    vec3_norm(out_dir);
    return 1;
}

static int ray_plane_hit(const float ro[3], const float rd[3], float plane_y, float hit[3]) {
    if (fabsf(rd[1]) < 1e-6f) return 0;
    float t = (plane_y - ro[1]) / rd[1];
    if (t < 0.0f) return 0;
    hit[0] = ro[0] + rd[0] * t;
    hit[1] = ro[1] + rd[1] * t;
    hit[2] = ro[2] + rd[2] * t;
    return 1;
}

static void apply_view(const EditorApp *app) {
    float eye[3];
    editor_camera_eye_position(&app->camera, eye);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(app->camera.fov_degrees, (double)app->width / (double)app->height, 0.1, 5000.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(eye[0], eye[1], eye[2],
              app->camera.target[0], app->camera.target[1], app->camera.target[2],
              0.0, 1.0, 0.0);
}

static void draw_grid(void) {
    glColor3f(0.15f, 0.25f, 0.35f);
    glBegin(GL_LINES);
    for (int i = -100; i <= 100; i++) {
        glVertex3f((float)i * 2.0f, 0.0f, -200.0f);
        glVertex3f((float)i * 2.0f, 0.0f, 200.0f);
        glVertex3f(-200.0f, 0.0f, (float)i * 2.0f);
        glVertex3f(200.0f, 0.0f, (float)i * 2.0f);
    }
    glEnd();
}

static void draw_box(const EditorSceneBox *box, int selected) {
    glPushMatrix();
    glTranslatef(box->position[0], box->position[1], box->position[2]);
    glRotatef(box->rotation_y_degrees, 0.0f, 1.0f, 0.0f);
    glScalef(box->size[0], box->size[1], box->size[2]);

    glColor4f(box->color[0], box->color[1], box->color[2], box->color[3]);
    glBegin(GL_QUADS);
    glVertex3f(-0.5f,-0.5f, 0.5f); glVertex3f( 0.5f,-0.5f, 0.5f); glVertex3f( 0.5f, 0.5f, 0.5f); glVertex3f(-0.5f, 0.5f, 0.5f);
    glVertex3f(-0.5f,-0.5f,-0.5f); glVertex3f(-0.5f, 0.5f,-0.5f); glVertex3f( 0.5f, 0.5f,-0.5f); glVertex3f( 0.5f,-0.5f,-0.5f);
    glVertex3f(-0.5f, 0.5f,-0.5f); glVertex3f(-0.5f, 0.5f, 0.5f); glVertex3f( 0.5f, 0.5f, 0.5f); glVertex3f( 0.5f, 0.5f,-0.5f);
    glVertex3f(-0.5f,-0.5f,-0.5f); glVertex3f( 0.5f,-0.5f,-0.5f); glVertex3f( 0.5f,-0.5f, 0.5f); glVertex3f(-0.5f,-0.5f, 0.5f);
    glVertex3f( 0.5f,-0.5f,-0.5f); glVertex3f( 0.5f, 0.5f,-0.5f); glVertex3f( 0.5f, 0.5f, 0.5f); glVertex3f( 0.5f,-0.5f, 0.5f);
    glVertex3f(-0.5f,-0.5f,-0.5f); glVertex3f(-0.5f,-0.5f, 0.5f); glVertex3f(-0.5f, 0.5f, 0.5f); glVertex3f(-0.5f, 0.5f,-0.5f);
    glEnd();

    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glColor3f(selected ? 1.0f : 0.0f, selected ? 1.0f : 0.8f, selected ? 0.2f : 0.9f);
    glBegin(GL_QUADS);
    glVertex3f(-0.5f,-0.5f, 0.5f); glVertex3f( 0.5f,-0.5f, 0.5f); glVertex3f( 0.5f, 0.5f, 0.5f); glVertex3f(-0.5f, 0.5f, 0.5f);
    glVertex3f(-0.5f,-0.5f,-0.5f); glVertex3f(-0.5f, 0.5f,-0.5f); glVertex3f( 0.5f, 0.5f,-0.5f); glVertex3f( 0.5f,-0.5f,-0.5f);
    glVertex3f(-0.5f, 0.5f,-0.5f); glVertex3f(-0.5f, 0.5f, 0.5f); glVertex3f( 0.5f, 0.5f, 0.5f); glVertex3f( 0.5f, 0.5f,-0.5f);
    glVertex3f(-0.5f,-0.5f,-0.5f); glVertex3f( 0.5f,-0.5f,-0.5f); glVertex3f( 0.5f,-0.5f, 0.5f); glVertex3f(-0.5f,-0.5f, 0.5f);
    glVertex3f( 0.5f,-0.5f,-0.5f); glVertex3f( 0.5f, 0.5f,-0.5f); glVertex3f( 0.5f, 0.5f, 0.5f); glVertex3f( 0.5f,-0.5f, 0.5f);
    glVertex3f(-0.5f,-0.5f,-0.5f); glVertex3f(-0.5f,-0.5f, 0.5f); glVertex3f(-0.5f, 0.5f, 0.5f); glVertex3f(-0.5f, 0.5f,-0.5f);
    glEnd();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glPopMatrix();
}

static void draw_scene(const EditorApp *app) {
    draw_grid();
    for (size_t i = 0; i < app->scene.box_count; i++) {
        const EditorSceneBox *box = &app->scene.boxes[i];
        if (!box->flags.visible) continue;
        draw_box(box, (int)i == app->selected_box);
    }
}

static void frame_selected(EditorApp *app) {
    if (app->selected_box < 0 || app->selected_box >= (int)app->scene.box_count) return;
    const EditorSceneBox *box = &app->scene.boxes[app->selected_box];
    app->camera.target[0] = box->position[0];
    app->camera.target[1] = box->position[1];
    app->camera.target[2] = box->position[2];
    float max_dim = box->size[0];
    if (box->size[1] > max_dim) max_dim = box->size[1];
    if (box->size[2] > max_dim) max_dim = box->size[2];
    app->camera.distance = clampf(max_dim * 4.0f, 6.0f, 120.0f);
}

static void apply_stage_camera_to_view(EditorApp *app, int idx) {
    if (idx < 0 || idx >= (int)app->scene.stage_camera_count) return;
    const EditorStageCamera *c = &app->scene.stage_cameras[idx];
    app->camera.target[0] = c->target[0];
    app->camera.target[1] = c->target[1];
    app->camera.target[2] = c->target[2];
    app->camera.distance = c->distance;
    app->camera.yaw_degrees = c->yaw_degrees;
    app->camera.pitch_degrees = c->pitch_degrees;
    app->camera.fov_degrees = c->fov_degrees;
}

static void sync_view_to_stage_camera(EditorApp *app) {
    if (!app->stage_camera_drives_viewport) return;
    if (app->selected_stage_camera < 0 || app->selected_stage_camera >= (int)app->scene.stage_camera_count) return;
    EditorStageCamera *c = &app->scene.stage_cameras[app->selected_stage_camera];
    c->target[0] = app->camera.target[0];
    c->target[1] = app->camera.target[1];
    c->target[2] = app->camera.target[2];
    c->distance = app->camera.distance;
    c->yaw_degrees = app->camera.yaw_degrees;
    c->pitch_degrees = app->camera.pitch_degrees;
    c->fov_degrees = app->camera.fov_degrees;
}

static void create_default_scene(EditorApp *app) {
    editor_scene_asset_init(&app->scene);
    strncpy(app->scene.scene_name, "test_room", sizeof(app->scene.scene_name) - 1);
    editor_scene_asset_add_default_box(&app->scene, "floor", 0.0f, -1.0f, 0.0f);
    app->scene.boxes[0].size[0] = 30.0f;
    app->scene.boxes[0].size[1] = 1.0f;
    app->scene.boxes[0].size[2] = 30.0f;
    app->scene.boxes[0].color[0] = 0.25f;
    app->scene.boxes[0].color[1] = 0.28f;
    app->scene.boxes[0].color[2] = 0.3f;

    editor_scene_asset_add_default_box(&app->scene, "center_block", 0.0f, 2.0f, 0.0f);
    app->scene.boxes[1].size[0] = 8.0f;
    app->scene.boxes[1].size[1] = 4.0f;
    app->scene.boxes[1].size[2] = 8.0f;
    app->scene.boxes[1].color[0] = 0.7f;
    app->scene.boxes[1].color[1] = 0.2f;
    app->scene.boxes[1].color[2] = 0.2f;

    editor_scene_asset_add_default_box(&app->scene, "catwalk", 10.0f, 4.0f, -8.0f);
    app->scene.boxes[2].size[0] = 12.0f;
    app->scene.boxes[2].size[1] = 1.0f;
    app->scene.boxes[2].size[2] = 3.0f;
    app->scene.boxes[2].rotation_y_degrees = 18.0f;

    editor_scene_asset_add_default_stage_camera(&app->scene, "overview", 0.0f, 1.0f, 0.0f);
    editor_scene_asset_add_default_stage_camera(&app->scene, "arena_focus", 8.0f, 2.0f, -3.0f);
    app->scene.stage_cameras[1].yaw_degrees = -30.0f;
    app->scene.stage_cameras[1].distance = 18.0f;

    app->selected_box = 0;
    app->selected_stage_camera = 0;
}

static void handle_camera_hotkeys(EditorApp *app, SDL_Keycode key) {
    if (key == SDLK_LEFTBRACKET && app->scene.stage_camera_count > 0) {
        app->selected_stage_camera = (app->selected_stage_camera - 1 + (int)app->scene.stage_camera_count) % (int)app->scene.stage_camera_count;
        if (app->stage_camera_drives_viewport) apply_stage_camera_to_view(app, app->selected_stage_camera);
    } else if (key == SDLK_RIGHTBRACKET && app->scene.stage_camera_count > 0) {
        app->selected_stage_camera = (app->selected_stage_camera + 1) % (int)app->scene.stage_camera_count;
        if (app->stage_camera_drives_viewport) apply_stage_camera_to_view(app, app->selected_stage_camera);
    } else if (key == SDLK_c) {
        app->stage_camera_drives_viewport = !app->stage_camera_drives_viewport;
        if (app->stage_camera_drives_viewport) apply_stage_camera_to_view(app, app->selected_stage_camera);
    } else if (key == SDLK_n) {
        char name[64];
        snprintf(name, sizeof(name), "camera_%d", (int)app->scene.stage_camera_count + 1);
        editor_scene_asset_add_default_stage_camera(&app->scene, name, app->camera.target[0], app->camera.target[1], app->camera.target[2]);
        if (app->scene.stage_camera_count > 0) {
            app->selected_stage_camera = (int)app->scene.stage_camera_count - 1;
            EditorStageCamera *c = &app->scene.stage_cameras[app->selected_stage_camera];
            c->distance = app->camera.distance;
            c->yaw_degrees = app->camera.yaw_degrees;
            c->pitch_degrees = app->camera.pitch_degrees;
            c->fov_degrees = app->camera.fov_degrees;
        }
    }
}

static void adjust_selected_stage_camera(EditorApp *app, SDL_Keycode key) {
    if (app->selected_stage_camera < 0 || app->selected_stage_camera >= (int)app->scene.stage_camera_count) return;
    EditorStageCamera *c = &app->scene.stage_cameras[app->selected_stage_camera];
    if (key == SDLK_KP_4) c->yaw_degrees -= 1.0f;
    else if (key == SDLK_KP_6) c->yaw_degrees += 1.0f;
    else if (key == SDLK_KP_8) c->pitch_degrees = clampf(c->pitch_degrees + 1.0f, -89.0f, 89.0f);
    else if (key == SDLK_KP_2) c->pitch_degrees = clampf(c->pitch_degrees - 1.0f, -89.0f, 89.0f);
    else if (key == SDLK_KP_PLUS || key == SDLK_EQUALS) c->distance = clampf(c->distance - 0.5f, 2.0f, 500.0f);
    else if (key == SDLK_KP_MINUS || key == SDLK_MINUS) c->distance = clampf(c->distance + 0.5f, 2.0f, 500.0f);
    else if (key == SDLK_PAGEUP) c->fov_degrees = clampf(c->fov_degrees + 1.0f, 20.0f, 120.0f);
    else if (key == SDLK_PAGEDOWN) c->fov_degrees = clampf(c->fov_degrees - 1.0f, 20.0f, 120.0f);
    else return;

    if (app->stage_camera_drives_viewport) apply_stage_camera_to_view(app, app->selected_stage_camera);
}

static void handle_event(EditorApp *app, SDL_Event *e) {
    if (e->type == SDL_QUIT) {
        app->running = 0;
    } else if (e->type == SDL_WINDOWEVENT && e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        app->width = e->window.data1;
        app->height = e->window.data2;
        glViewport(0, 0, app->width, app->height);
    } else if (e->type == SDL_MOUSEMOTION) {
        app->mouse_x = e->motion.x;
        app->mouse_y = e->motion.y;
        Uint32 state = SDL_GetMouseState(NULL, NULL);
        int alt = (SDL_GetModState() & KMOD_ALT) != 0;
        if ((state & SDL_BUTTON(SDL_BUTTON_LEFT)) && alt) {
            editor_camera_orbit(&app->camera, e->motion.xrel * 0.35f, -e->motion.yrel * 0.3f);
        } else if ((state & SDL_BUTTON(SDL_BUTTON_MIDDLE)) || ((state & SDL_BUTTON(SDL_BUTTON_RIGHT)) && alt)) {
            float speed = app->camera.distance * 0.004f;
            editor_camera_pan(&app->camera, -e->motion.xrel * speed, e->motion.yrel * speed);
        } else if ((state & SDL_BUTTON(SDL_BUTTON_LEFT)) && app->move_tool.active && app->selected_box >= 0) {
            float ro[3], rd[3], hit[3];
            if (ray_from_mouse(app, app->mouse_x, app->mouse_y, ro, rd)) {
                float plane_y = app->scene.boxes[app->selected_box].position[1];
                if (app->move_tool.axis == EDITOR_MOVE_AXIS_Y) {
                    float yaw = app->camera.yaw_degrees * 0.017453292f;
                    float plane_normal[3] = { cosf(yaw), 0.0f, -sinf(yaw) };
                    float p0[3] = { app->scene.boxes[app->selected_box].position[0], plane_y, app->scene.boxes[app->selected_box].position[2] };
                    float denom = vec3_dot(plane_normal, rd);
                    if (fabsf(denom) > 1e-6f) {
                        float w[3];
                        vec3_sub(p0, ro, w);
                        float t = vec3_dot(w, plane_normal) / denom;
                        if (t > 0.0f) {
                            hit[0] = ro[0] + rd[0] * t;
                            hit[1] = ro[1] + rd[1] * t;
                            hit[2] = ro[2] + rd[2] * t;
                            editor_move_tool_apply(&app->move_tool, hit, app->scene.boxes[app->selected_box].position);
                        }
                    }
                } else if (ray_plane_hit(ro, rd, plane_y, hit)) {
                    editor_move_tool_apply(&app->move_tool, hit, app->scene.boxes[app->selected_box].position);
                }
            }
        }
        sync_view_to_stage_camera(app);
    } else if (e->type == SDL_MOUSEWHEEL) {
        editor_camera_zoom(&app->camera, -e->wheel.y * (app->camera.distance * 0.1f));
        sync_view_to_stage_camera(app);
    } else if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        int alt = (SDL_GetModState() & KMOD_ALT) != 0;
        if (!alt) {
            float ro[3], rd[3], hit[3];
            apply_view(app);
            if (ray_from_mouse(app, e->button.x, e->button.y, ro, rd)) {
                int idx = editor_selection_pick_box(&app->scene, ro, rd);
                app->selected_box = idx;
                if (idx >= 0 && ray_plane_hit(ro, rd, app->scene.boxes[idx].position[1], hit)) {
                    editor_move_tool_begin(&app->move_tool, hit, app->scene.boxes[idx].position);
                }
            }
        }
    } else if (e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT) {
        editor_move_tool_cancel(&app->move_tool);
    } else if (e->type == SDL_KEYDOWN) {
        SDL_Keycode key = e->key.keysym.sym;
        if (key == SDLK_ESCAPE) app->running = 0;
        else if (key == SDLK_f) frame_selected(app);
        else if (key == SDLK_x) editor_move_tool_set_axis(&app->move_tool, EDITOR_MOVE_AXIS_X);
        else if (key == SDLK_y) editor_move_tool_set_axis(&app->move_tool, EDITOR_MOVE_AXIS_Y);
        else if (key == SDLK_z) editor_move_tool_set_axis(&app->move_tool, EDITOR_MOVE_AXIS_Z);
        else if (key == SDLK_g) editor_move_tool_set_axis(&app->move_tool, EDITOR_MOVE_AXIS_FREE);
        else if (key == SDLK_o) {
            if (!editor_scene_json_load(app->active_scene_path, &app->scene)) {
                fprintf(stderr, "[scene_editor] failed to load '%s'\n", app->active_scene_path);
            }
        } else if (key == SDLK_p) {
            if (!editor_scene_json_save(app->active_scene_path, &app->scene)) {
                fprintf(stderr, "[scene_editor] failed to save '%s'\n", app->active_scene_path);
            }
        } else {
            handle_camera_hotkeys(app, key);
            adjust_selected_stage_camera(app, key);
        }
    }
}

static int app_init(EditorApp *app, const char *path_override) {
    memset(app, 0, sizeof(*app));
    app->running = 1;
    app->width = 1400;
    app->height = 900;
    app->selected_box = -1;
    app->selected_stage_camera = -1;
    app->active_scene_path[0] = '\0';
    editor_camera_init(&app->camera);
    app->move_tool.axis = EDITOR_MOVE_AXIS_FREE;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "[scene_editor] SDL_Init failed: %s\n", SDL_GetError());
        return 0;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    app->window = SDL_CreateWindow("SHANKPIT Scene Editor",
                                   SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED,
                                   app->width,
                                   app->height,
                                   SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!app->window) {
        fprintf(stderr, "[scene_editor] SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 0;
    }

    app->gl = SDL_GL_CreateContext(app->window);
    if (!app->gl) {
        fprintf(stderr, "[scene_editor] SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return 0;
    }

    SDL_GL_SetSwapInterval(1);

    glViewport(0, 0, app->width, app->height);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (path_override && path_override[0]) strncpy(app->active_scene_path, path_override, sizeof(app->active_scene_path)-1);
    else strncpy(app->active_scene_path, "assets/scenes/test_room.json", sizeof(app->active_scene_path)-1);

    if (!editor_scene_json_load(app->active_scene_path, &app->scene)) {
        create_default_scene(app);
        editor_scene_json_save(app->active_scene_path, &app->scene);
    }
    if (app->scene.box_count > 0) app->selected_box = 0;
    if (app->scene.stage_camera_count > 0) app->selected_stage_camera = 0;

    return 1;
}

static void app_shutdown(EditorApp *app) {
    if (app->gl) SDL_GL_DeleteContext(app->gl);
    if (app->window) SDL_DestroyWindow(app->window);
    SDL_Quit();
}

int editor_app_run(const char *scene_path_override) {
    EditorApp app;
    if (!app_init(&app, scene_path_override)) {
        app_shutdown(&app);
        return 1;
    }

    while (app.running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            handle_event(&app, &e);
        }

        glClearColor(0.07f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        apply_view(&app);
        draw_scene(&app);
        editor_ui_draw_overlay(app.width, app.height, &app.scene, app.selected_box,
                               app.selected_stage_camera, app.stage_camera_drives_viewport,
                               &app.move_tool, &app.camera, app.active_scene_path);

        SDL_GL_SwapWindow(app.window);
    }

    app_shutdown(&app);
    return 0;
}
