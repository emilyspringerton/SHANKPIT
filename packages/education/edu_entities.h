#ifndef EDU_ENTITIES_H
#define EDU_ENTITIES_H

#define MAX_EDU_ENTITIES 64

typedef enum {
    EDU_ENTITY_NONE = 0,
    EDU_ENTITY_PROP_CRATE,
    EDU_ENTITY_GATE,
    EDU_ENTITY_PROP_BARREL,
    EDU_ENTITY_PROP_SWITCH
} EduEntityType;

typedef struct {
    int id;
    EduEntityType type;
    float x, y, z;
    float vx, vy, vz;
    float pitch, yaw, roll;
    int solid;
    int frozen;
    int owner_id;
    char label[32];
    int health;
    unsigned int flags;
} Entity;

#endif
