#ifndef AI_NAV_H
#define AI_NAV_H

/* S461-01 -- real, hand-authored waypoint graph (grid/waypoint style per the founder's own
   real-time direction, 2026-09-17: "we are going to need a waypoint system in the levels and
   maps northstar it" -- explicitly not a polygon navmesh, since SHANKPIT has no wall-collision
   or navmesh-bake pipeline to derive one from, only a ground heightfield in terrain.c). Same
   authoring convention as story_ai.c's own AIPatrolPoint (see story_ai_seed_voxworld_encounter):
   hand-placed nodes per scene, not procedurally generated. Full design + per-level authoring
   plan: docs2/specs/AI_WAYPOINT_NAV_NORTHSTAR.md. */

#define AI_NAV_MAX_NODES 32
#define AI_NAV_MAX_NEIGHBORS 4
#define AI_NAV_MAX_PATH 8

/* is_cover-tagged nodes carry a cover_dir unit vector: the direction FROM the node AWAY FROM the
   obstacle that provides the cover. A threat is actually blocked by this node only when the
   threat lies roughly opposite cover_dir (see ai_nav_find_cover) -- storing "which way is safe"
   rather than "which way is the wall" keeps the dot-product test a single sign check. */
typedef struct {
    float x, y, z;
    int is_cover;
    float cover_dir_x, cover_dir_z;
    int neighbor_count;
    int neighbors[AI_NAV_MAX_NEIGHBORS];
} AINavNode;

typedef struct {
    AINavNode nodes[AI_NAV_MAX_NODES];
    int node_count;
} AINavGraph;

void ai_nav_reset(AINavGraph *g);

/* Adds a node, normalizing (cover_dir_x, cover_dir_z) internally. Returns the new node's index,
   or -1 if the graph is full. cover_dir is ignored (zeroed) when is_cover is 0. */
int ai_nav_add_node(AINavGraph *g, float x, float y, float z, int is_cover, float cover_dir_x, float cover_dir_z);

/* Adds a bidirectional edge between two already-added nodes. No-op on out-of-range indices, a
   duplicate edge, or a full neighbor list (AI_NAV_MAX_NEIGHBORS) on either side. */
void ai_nav_link(AINavGraph *g, int a, int b);

/* Real A* over the graph's authored edges (not a straight line -- respects actual graph
   topology). Finds the node nearest (sx,sz) as the start, A*s to goal_node, and fills path_out
   with up to max_path node indices in travel order (excluding the start node itself). Returns
   the path length (0 if goal_node IS the nearest-start node), or -1 if unreachable or the inputs
   are invalid. */
int ai_nav_find_path(const AINavGraph *g, float sx, float sz, int goal_node, int *path_out, int max_path);

/* Finds the nearest is_cover node whose cover_dir faces away from (threat_x,threat_z) -- i.e.
   real protection from THIS threat position, not just "the closest cover tile." Returns -1 if no
   node in the graph qualifies, including an empty/unauthored graph -- callers must treat -1 as a
   real, expected case (fall back to a plain repulsion vector), not an error. */
int ai_nav_find_cover(const AINavGraph *g, float sx, float sz, float threat_x, float threat_z);

#endif
