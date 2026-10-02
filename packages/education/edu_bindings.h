#ifndef EDU_BINDINGS_H
#define EDU_BINDINGS_H

#include "edu_entities.h"

typedef struct {
    int crate_speed;
    int crate_x;
    int gate_open;
    int bridge_raised;
    int bridge_angle;
    int portal_stability;
    int portal_open;
    int enemy_count;
    int trial_complete;
    int switch_on;
    int grounded_test_platform;
    int quest_make_it_move;
    int quest_stop_the_fall;
    int quest_open_the_gate;
    int rift_hound_active;
    int rift_hound_hp;
    int enemy_marked[4];
    int enemy_slowed[4];
    int can_open_gate;
    int can_raise_bridge;
    int can_modify_portal;
    int can_affect_enemies;
    int can_spawn_entities;
    int puppet_active;
    int puppet_type;
    float puppet_x;
    float puppet_y;
    float puppet_z;
    float puppet_anim_t;
    int last_print;
    char last_message[96];
    Entity entities[MAX_EDU_ENTITIES];
    int entity_count;
    unsigned int entity_tick_last_ms;
} EduWorldState;

typedef enum {
    EDU_BUILTIN_SET_CRATE_SPEED = 0,
    EDU_BUILTIN_MOVE_CRATE,
    EDU_BUILTIN_STOP_CRATE,
    EDU_BUILTIN_OPEN_GATE,
    EDU_BUILTIN_CLOSE_GATE,
    EDU_BUILTIN_SET_SWITCH,
    EDU_BUILTIN_IS_SWITCH_ON,
    EDU_BUILTIN_QUERY_GROUNDED,
    EDU_BUILTIN_MARK_QUEST_COMPLETE,
    EDU_BUILTIN_GET_QUEST_STATE,
    EDU_BUILTIN_PRINT,
    EDU_BUILTIN_SHOW_MESSAGE,
    EDU_BUILTIN_SCAN_GATE,
    EDU_BUILTIN_SCAN_PORTAL,
    EDU_BUILTIN_SCAN_ENEMY_COUNT,
    EDU_BUILTIN_RAISE_BRIDGE,
    EDU_BUILTIN_STABILIZE_PORTAL,
    EDU_BUILTIN_OPEN_PORTAL,
    EDU_BUILTIN_MARK_ENEMY,
    EDU_BUILTIN_SLOW_ENEMY,
    EDU_BUILTIN_SPAWN_PROP,
    EDU_BUILTIN_SET_ENTITY_POS,
    EDU_BUILTIN_SET_ENTITY_VEL,
    EDU_BUILTIN_COUNT
} EduBuiltinId;

int edu_binding_lookup(const char *name, int name_len, int *out_id, int *out_argc);
int edu_binding_call(EduWorldState *world, int builtin_id, const int *args, int argc,
                     const char *const *strings, int string_count, int *out_ret,
                     char *debug_out, int debug_cap);
void world_objects_tick(EduWorldState *w, unsigned int now_ms);

#endif
