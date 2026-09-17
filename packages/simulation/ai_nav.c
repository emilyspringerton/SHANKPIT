#include "ai_nav.h"
#include <math.h>
#include <string.h>

static float nav_dist(float x1, float z1, float x2, float z2) {
    float dx = x2 - x1, dz = z2 - z1;
    return sqrtf(dx * dx + dz * dz);
}

void ai_nav_reset(AINavGraph *g) {
    memset(g, 0, sizeof(*g));
}

int ai_nav_add_node(AINavGraph *g, float x, float y, float z, int is_cover, float cover_dir_x, float cover_dir_z) {
    int idx;
    float mag;
    AINavNode *n;
    if (!g || g->node_count >= AI_NAV_MAX_NODES) return -1;
    idx = g->node_count++;
    n = &g->nodes[idx];
    n->x = x;
    n->y = y;
    n->z = z;
    n->is_cover = is_cover;
    n->neighbor_count = 0;
    if (is_cover) {
        mag = sqrtf(cover_dir_x * cover_dir_x + cover_dir_z * cover_dir_z);
        if (mag > 0.0001f) {
            n->cover_dir_x = cover_dir_x / mag;
            n->cover_dir_z = cover_dir_z / mag;
        } else {
            n->cover_dir_x = 0.0f;
            n->cover_dir_z = 0.0f;
        }
    } else {
        n->cover_dir_x = 0.0f;
        n->cover_dir_z = 0.0f;
    }
    return idx;
}

static void nav_link_one_way(AINavGraph *g, int a, int b) {
    AINavNode *n;
    int i;
    if (!g || a < 0 || a >= g->node_count || b < 0 || b >= g->node_count) return;
    n = &g->nodes[a];
    for (i = 0; i < n->neighbor_count; i++) {
        if (n->neighbors[i] == b) return;
    }
    if (n->neighbor_count >= AI_NAV_MAX_NEIGHBORS) return;
    n->neighbors[n->neighbor_count++] = b;
}

void ai_nav_link(AINavGraph *g, int a, int b) {
    nav_link_one_way(g, a, b);
    nav_link_one_way(g, b, a);
}

static int nav_nearest_node(const AINavGraph *g, float x, float z) {
    int best = -1;
    float best_dist = 0.0f;
    int i;
    for (i = 0; i < g->node_count; i++) {
        float d = nav_dist(x, z, g->nodes[i].x, g->nodes[i].z);
        if (best < 0 || d < best_dist) {
            best = i;
            best_dist = d;
        }
    }
    return best;
}

/* Small-graph A* (<=AI_NAV_MAX_NODES=32 nodes) -- an O(n^2) open-list scan is deliberately fine
   at this scale, no priority queue needed. */
int ai_nav_find_path(const AINavGraph *g, float sx, float sz, int goal_node, int *path_out, int max_path) {
    int start;
    float g_score[AI_NAV_MAX_NODES];
    float f_score[AI_NAV_MAX_NODES];
    int came_from[AI_NAV_MAX_NODES];
    int visited[AI_NAV_MAX_NODES];
    int i, current, len;
    int rev[AI_NAV_MAX_NODES];
    int rlen;
    int node;

    if (!g || !path_out || g->node_count <= 0 || goal_node < 0 || goal_node >= g->node_count) return -1;
    start = nav_nearest_node(g, sx, sz);
    if (start < 0) return -1;
    if (start == goal_node) return 0;

    for (i = 0; i < g->node_count; i++) {
        g_score[i] = 1e9f;
        f_score[i] = 1e9f;
        came_from[i] = -1;
        visited[i] = 0;
    }
    g_score[start] = 0.0f;
    f_score[start] = nav_dist(g->nodes[start].x, g->nodes[start].z, g->nodes[goal_node].x, g->nodes[goal_node].z);

    for (;;) {
        current = -1;
        {
            float best = 1e9f;
            for (i = 0; i < g->node_count; i++) {
                if (!visited[i] && f_score[i] < best) {
                    best = f_score[i];
                    current = i;
                }
            }
        }
        if (current < 0) break;
        if (current == goal_node) break;
        visited[current] = 1;

        for (i = 0; i < g->nodes[current].neighbor_count; i++) {
            int nb = g->nodes[current].neighbors[i];
            float tentative;
            if (visited[nb]) continue;
            tentative = g_score[current] + nav_dist(g->nodes[current].x, g->nodes[current].z, g->nodes[nb].x, g->nodes[nb].z);
            if (tentative < g_score[nb]) {
                came_from[nb] = current;
                g_score[nb] = tentative;
                f_score[nb] = tentative + nav_dist(g->nodes[nb].x, g->nodes[nb].z, g->nodes[goal_node].x, g->nodes[goal_node].z);
            }
        }
    }

    if (came_from[goal_node] < 0) return -1;

    rlen = 0;
    node = goal_node;
    while (node != start && node >= 0 && rlen < AI_NAV_MAX_NODES) {
        rev[rlen++] = node;
        node = came_from[node];
    }
    len = (rlen < max_path) ? rlen : max_path;
    for (i = 0; i < len; i++) path_out[i] = rev[rlen - 1 - i];
    return len;
}

int ai_nav_find_cover(const AINavGraph *g, float sx, float sz, float threat_x, float threat_z) {
    int best = -1;
    float best_dist = 0.0f;
    int i;
    if (!g) return -1;
    for (i = 0; i < g->node_count; i++) {
        const AINavNode *n = &g->nodes[i];
        float to_threat_x, to_threat_z, mag, dot, d;
        if (!n->is_cover) continue;
        to_threat_x = threat_x - n->x;
        to_threat_z = threat_z - n->z;
        mag = sqrtf(to_threat_x * to_threat_x + to_threat_z * to_threat_z);
        if (mag <= 0.0001f) continue; /* threat sits on the node itself -- no meaningful direction to test */
        dot = (to_threat_x / mag) * n->cover_dir_x + (to_threat_z / mag) * n->cover_dir_z;
        /* cover_dir must point AWAY from the threat, so the to-threat direction should be
           roughly opposite it -- dot should be strongly negative. */
        if (dot >= -0.3f) continue;
        d = nav_dist(sx, sz, n->x, n->z);
        if (best < 0 || d < best_dist) {
            best = i;
            best_dist = d;
        }
    }
    return best;
}
