#include "editor_ui.h"

#include <stdio.h>

#include "../../packages/ui/turtle_text.h"

static void ui_text(float x, float y, float scale, const char *text) {
    TurtlePen pen = turtle_pen_create(x, y, scale);
    turtle_draw_text(&pen, text);
}

void editor_ui_draw_overlay(int width, int height,
                            const EditorSceneAsset *scene,
                            int selected_box,
                            int selected_stage_camera,
                            int stage_camera_drives_viewport,
                            const EditorMoveTool *move_tool,
                            const EditorViewportCamera *camera,
                            const char *active_path) {
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glColor4f(0.0f, 0.0f, 0.0f, 0.55f);
    turtle_draw_rect(0, 0, 310, (float)height);
    turtle_draw_rect((float)width - 340, 0, 340, (float)height);
    glColor3f(0.94f, 0.94f, 0.94f);

    char line[256];
    int y = 18;
    ui_text(10, (float)y, 2, "Scene Editor V1"); y += 16;
    snprintf(line, sizeof(line), "scene: %s", scene->scene_name);
    ui_text(10, (float)y, 2, line); y += 14;
    snprintf(line, sizeof(line), "path: %s", active_path ? active_path : "(none)");
    ui_text(10, (float)y, 2, line); y += 18;
    snprintf(line, sizeof(line), "boxes: %d  cameras: %d", (int)scene->box_count, (int)scene->stage_camera_count);
    ui_text(10, (float)y, 2, line); y += 14;
    snprintf(line, sizeof(line), "selected box: %d", selected_box >= 0 ? selected_box : -1);
    ui_text(10, (float)y, 2, line); y += 14;
    snprintf(line, sizeof(line), "selected camera: %d %s", selected_stage_camera >= 0 ? selected_stage_camera : -1,
             stage_camera_drives_viewport ? "(driving view)" : "");
    ui_text(10, (float)y, 2, line); y += 22;

    ui_text(10, (float)y, 2, "Controls"); y += 14;
    ui_text(10, (float)y, 2, "Alt+LMB orbit"); y += 12;
    ui_text(10, (float)y, 2, "MMB / Alt+RMB pan"); y += 12;
    ui_text(10, (float)y, 2, "Wheel zoom"); y += 12;
    ui_text(10, (float)y, 2, "LMB select / drag move"); y += 12;
    ui_text(10, (float)y, 2, "F frame selected"); y += 12;
    ui_text(10, (float)y, 2, "O load  P save"); y += 12;
    ui_text(10, (float)y, 2, "N new camera from view"); y += 12;
    ui_text(10, (float)y, 2, "[ ] select camera"); y += 12;
    ui_text(10, (float)y, 2, "C toggle camera/view link"); y += 12;
    ui_text(10, (float)y, 2, "X/Y/Z axis lock, G free"); y += 12;

    int ry = 18;
    ui_text((float)width - 330, (float)ry, 2, "Inspector"); ry += 16;
    snprintf(line, sizeof(line), "view target: %.2f %.2f %.2f", camera->target[0], camera->target[1], camera->target[2]);
    ui_text((float)width - 330, (float)ry, 2, line); ry += 14;
    snprintf(line, sizeof(line), "distance %.2f yaw %.2f", camera->distance, camera->yaw_degrees);
    ui_text((float)width - 330, (float)ry, 2, line); ry += 14;
    snprintf(line, sizeof(line), "pitch %.2f fov %.2f", camera->pitch_degrees, camera->fov_degrees);
    ui_text((float)width - 330, (float)ry, 2, line); ry += 20;

    if (selected_box >= 0 && selected_box < (int)scene->box_count) {
        const EditorSceneBox *box = &scene->boxes[selected_box];
        snprintf(line, sizeof(line), "box %d: %s", box->id, box->name);
        ui_text((float)width - 330, (float)ry, 2, line); ry += 14;
        snprintf(line, sizeof(line), "pos: %.2f %.2f %.2f", box->position[0], box->position[1], box->position[2]);
        ui_text((float)width - 330, (float)ry, 2, line); ry += 14;
        snprintf(line, sizeof(line), "size: %.2f %.2f %.2f", box->size[0], box->size[1], box->size[2]);
        ui_text((float)width - 330, (float)ry, 2, line); ry += 14;
        snprintf(line, sizeof(line), "rotY: %.2f", box->rotation_y_degrees);
        ui_text((float)width - 330, (float)ry, 2, line); ry += 14;
    }

    if (selected_stage_camera >= 0 && selected_stage_camera < (int)scene->stage_camera_count) {
        const EditorStageCamera *cam = &scene->stage_cameras[selected_stage_camera];
        ry += 6;
        snprintf(line, sizeof(line), "stage cam %d: %s", cam->id, cam->name);
        ui_text((float)width - 330, (float)ry, 2, line); ry += 14;
        snprintf(line, sizeof(line), "target: %.2f %.2f %.2f", cam->target[0], cam->target[1], cam->target[2]);
        ui_text((float)width - 330, (float)ry, 2, line); ry += 14;
        snprintf(line, sizeof(line), "dist %.2f yaw %.2f", cam->distance, cam->yaw_degrees);
        ui_text((float)width - 330, (float)ry, 2, line); ry += 14;
        snprintf(line, sizeof(line), "pitch %.2f fov %.2f", cam->pitch_degrees, cam->fov_degrees);
        ui_text((float)width - 330, (float)ry, 2, line); ry += 14;
    }

    snprintf(line, sizeof(line), "move axis: %d %s", (int)move_tool->axis, move_tool->active ? "(dragging)" : "");
    ui_text((float)width - 330, (float)height - 20, 2, line);

    glEnable(GL_DEPTH_TEST);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}
