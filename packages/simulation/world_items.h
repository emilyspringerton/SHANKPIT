#ifndef WORLD_ITEMS_H
#define WORLD_ITEMS_H
/* world_items.h -- founder real-time 2026-11-24 (BACKLOG SECTION 643): basic items you can pick up that are affordances:
 * notebook, pencil, pencil sharpener, eraser, pen, ruler, scissors. Every decision (reach, pocket cap, what using A on B does)
 * is the PARENA mod PARENA/stdlib/shankpit/item_rules.prn (generated item_rules.c); this module is the pool, the carried list
 * and the plumbing. Pure data, no SDL/GL/PlayerState, headless-testable. Client-local like gun_items.h / food_pickup.h:
 * NOT yet server-authoritative, and not visible to other players. */

#define WORLD_ITEM_MAX 32
#define WORLD_CARRY_MAX 8   /* mirrors item_carry_cap() */

typedef struct { int id; int aux; int aux2; } ItemState;   /* id 1..7 (see item_rules.h); aux/aux2 are per-item state */
typedef struct { int active; ItemState s; float x, y, z; } WorldItem;

void world_items_reset(void);                       /* clears the pool AND the pockets */
int  world_items_drop(int id, int aux, int aux2, float x, float y, float z); /* aux<0: item defaults; returns slot or -1 */
int  world_items_active_count(void);
const WorldItem *world_items_get(int slot);
/* Nearest active item in reach of (px,py,pz) (per-item radius from the rules), or -1. */
int  world_items_nearest(float px, float py, float pz);
/* Prompt for the nearest item: 0 none, 1 pick up, 2 pockets full (the PARENA item-prompt). */
int  world_items_prompt(float px, float py, float pz, int *slot_out);
/* Picks up the nearest item in reach. Returns the carried index, -1 nothing in reach, -2 pockets full. The new item becomes held. */
int  world_items_pickup(float px, float py, float pz);

int  world_items_carried_count(void);
const ItemState *world_items_carried(int i);
int  world_items_held(void);                        /* carried index of the held item, -1 when empty-handed */
void world_items_cycle_held(void);                  /* next carried item (wraps); no-op when empty */
/* Puts the held item back on the ground at (x,y,z); returns its world slot, -1 if nothing held or pool full. */
int  world_items_drop_held(float x, float y, float z);
/* Uses the held item: on the nearest world item in reach, then on each other carried item, then alone. Tries both directions
 * (A on B, then B on A). Writes a one-line result into msg. Returns the effect code from the rules (>0 done, <0 refused, 0 none). */
int  world_items_use_held(float px, float py, float pz, char *msg, int msg_cap);

const char *world_item_name(int id);
/* One-line state summary of an item ("PENCIL point 100 len 190mm"). */
void world_item_describe(const ItemState *s, char *out, int cap);

/* Seeds a desk: all seven items laid in a row along +X starting at (cx+dx, desktop_y, cz). The desk top height is returned via
 * *top_out so the host can draw the desk under them. Returns the number placed. */
int  world_items_seed_desk(float cx, float floor_y, float cz, float *desk_cx, float *desk_top);
#endif
