#ifndef ITEM_RULES_H
#define ITEM_RULES_H
/* item_rules.h -- prototypes for item_rules.c, generated from PARENA/stdlib/shankpit/item_rules.prn
 * (scripts/gen_item_rules.sh). Item ids: 1 notebook 2 pencil 3 pencil sharpener 4 eraser 5 pen 6 ruler 7 scissors. */
int item_valid(int id);
int item_count(void);
int item_pickup_radius_mm(int id);
int item_pickup_height_mm(int id);
int item_carry_cap(void);
int item_can_pickup(int id, int carried);
int item_prompt(int id, int carried);
int item_color(int id);
int item_dim_mm(int id, int axis);
int item_init_aux(int id);
int item_init_aux2(int id);
int item_notebook_pages(void);
int item_use_effect(int actor, int target, int ha, int hb, int ta, int tb);
int item_use_apply(int effect, int field, int ha, int hb, int ta, int tb);
#endif
