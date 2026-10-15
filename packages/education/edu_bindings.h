#ifndef EDU_BINDINGS_H
#define EDU_BINDINGS_H

#include "edu_entities.h"

/* Level-authored programmable widgets (NOCK `edu_widgets` -> EduVM). A widget is a named machine with one
 * int state register; scripts address it by string id (set_state/read_state) or by its register address
 * (set_reg/read_sensor, which is what makes `while` loops over consecutive gates possible). PLATE is a
 * sensor: only the host writes it (edu_widget_host_set), a script can never forge one. */
#define EDU_MAX_WIDGETS 16
#define EDU_WIDGET_ID_MAX 32
typedef enum {
    EDU_WIDGET_NONE = 0,
    EDU_WIDGET_GATE,
    EDU_WIDGET_BRIDGE,
    EDU_WIDGET_PORTAL,
    EDU_WIDGET_CRATE,
    EDU_WIDGET_PLATE,
    EDU_WIDGET_TYPE_COUNT
} EduWidgetType;

typedef struct {
    int active;
    char id[EDU_WIDGET_ID_MAX];
    int machine_type;
    int initial_state;
    int state;
    int reg;                 /* memory_address: the "hardware register" scripts read/write by number */
    float x, y, z, radius;   /* world position + sensor reach (plates), for the host to draw/trigger */
} EduWidget;

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
    EduWidget widgets[EDU_MAX_WIDGETS];
    int widget_count;
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
    EDU_BUILTIN_SET_STATE,
    EDU_BUILTIN_SET_REG,
    EDU_BUILTIN_READ_SENSOR,
    EDU_BUILTIN_READ_STATE,
    EDU_BUILTIN_COUNT
} EduBuiltinId;

int edu_binding_lookup(const char *name, int name_len, int *out_id, int *out_argc);
int edu_binding_call(EduWorldState *world, int builtin_id, const int *args, int argc,
                     const char *const *strings, int string_count, int *out_ret,
                     char *debug_out, int debug_cap);
/* widget host API. bind: re-binding an existing id updates it in place; returns slot index or -1
 * (full / bad type / empty id / duplicate register on a different id). */
int edu_widget_bind(EduWorldState *w, const char *id, int machine_type, int initial_state, int reg,
                    float x, float y, float z, float radius);
void edu_widgets_clear(EduWorldState *w);
void edu_widgets_reset(EduWorldState *w);                       /* every state back to its initial_state */
EduWidget *edu_widget_find_id(EduWorldState *w, const char *id);
EduWidget *edu_widget_find_reg(EduWorldState *w, int reg);
int edu_widget_host_set(EduWorldState *w, const char *id, int state); /* host write (sensors allowed) */
void world_objects_tick(EduWorldState *w, unsigned int now_ms);

#endif
