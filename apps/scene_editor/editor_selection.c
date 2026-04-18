#include "editor_selection.h"

#include <float.h>
#include <math.h>

static int ray_aabb(const float ro[3], const float rd[3], const float bmin[3], const float bmax[3], float *out_t) {
    float tmin = -FLT_MAX;
    float tmax = FLT_MAX;
    for (int i = 0; i < 3; i++) {
        if (fabsf(rd[i]) < 1e-6f) {
            if (ro[i] < bmin[i] || ro[i] > bmax[i]) return 0;
            continue;
        }
        float inv = 1.0f / rd[i];
        float t1 = (bmin[i] - ro[i]) * inv;
        float t2 = (bmax[i] - ro[i]) * inv;
        if (t1 > t2) {
            float tmp = t1;
            t1 = t2;
            t2 = tmp;
        }
        if (t1 > tmin) tmin = t1;
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return 0;
    }
    if (tmax < 0.0f) return 0;
    *out_t = tmin > 0.0f ? tmin : tmax;
    return 1;
}

int editor_selection_pick_box(const EditorSceneAsset *scene, const float ray_origin[3], const float ray_dir[3]) {
    int best = -1;
    float best_t = FLT_MAX;
    for (size_t i = 0; i < scene->box_count; i++) {
        const EditorSceneBox *box = &scene->boxes[i];
        if (!box->flags.visible) continue;
        float half[3] = { box->size[0] * 0.5f, box->size[1] * 0.5f, box->size[2] * 0.5f };
        float bmin[3] = { box->position[0] - half[0], box->position[1] - half[1], box->position[2] - half[2] };
        float bmax[3] = { box->position[0] + half[0], box->position[1] + half[1], box->position[2] + half[2] };
        float t;
        if (ray_aabb(ray_origin, ray_dir, bmin, bmax, &t) && t < best_t) {
            best_t = t;
            best = (int)i;
        }
    }
    return best;
}
