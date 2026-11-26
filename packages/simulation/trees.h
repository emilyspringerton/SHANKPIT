#ifndef TREES_H
#define TREES_H

/* trees.h -- trees and wood in the zombie modes (SURVIVAL, ZOMBIES). Founder 2026-11-26: "Add trees to zombie survival modes for
 * shankpit og engine and shankpit wasm zombies ultra low poly models like our skins add resource collection mod parena based mods".
 * What a tree IS -- its box model (at most four boxes, like the skins), where trees grow, toughness, chop damage per weapon, wood,
 * regrowth -- is the PARENA mod PARENA/stdlib/shankpit/tree_rules.prn (generated into tree_rules.c; the wasm survival mode links
 * the same file). This module is the pool, the ray test and the wood count. Pure data + math (no SDL/GL/PlayerState), headless-
 * testable, client-local like chests.h. */

#define TREE_MAX 64

typedef struct {
    int active;            /* a slot in use (standing or felled) */
    int alive;             /* 1 standing, 0 felled (a stump until it grows back) */
    int kind;              /* 1 pine, 2 oak, 3 birch */
    int hp;
    float x, y, z;         /* base on the ground */
    float scale;           /* 0.80 .. 1.25 */
    unsigned int regrow_at_ms;
    unsigned int hurt_ms;  /* host-set timestamp of the last chop, for a shake */
} Tree;

/* The host's say on a spot: 1 if a tree may stand at (x, z), with the ground height written to *ground_y; 0 if not. */
typedef int (*TreeSiteFn)(float x, float z, float *ground_y);

void trees_reset(void);
/* Grows trees on tree_rules' grid, ring by ring outward from (cx, cz) out to `radius`, while the pool lasts. Returns the count. */
int trees_grow(float cx, float cz, float radius, int seed, TreeSiteFn site);
int trees_count(void);
const Tree *trees_get(int slot);                       /* NULL past the pool or for an unused slot */
/* One number of one box of a tree, in world units, relative to the tree's base: axis 0 x, 1 y (centre), 2 z, 3 w, 4 h, 5 d. */
float trees_part(const Tree *t, int part, int axis);
int trees_parts(const Tree *t);
unsigned int trees_part_color(const Tree *t, int part);  /* 0xRRGGBB */
/* A chop: a unit ray of length <= range against standing trunks. The nearest takes tree_chop_damage(weapon); wood is added (a log
 * per hit, the fell bonus when it comes down). Returns the slot hit or -1; *felled is set to 1 if it came down. */
int trees_hit_ray(float ox, float oy, float oz, float dx, float dy, float dz, float range, int weapon, unsigned int now_ms, int *felled);
/* Regrowth: a felled tree stands again once its time is up (the host may delay it while somebody stands on the spot). */
void trees_tick(unsigned int now_ms);
int trees_wood(void);
void trees_wood_set(int n);

/* PARENA-generated (tree_rules.c) */
int tree_kinds(void);
int tree_parts(int kind);
int tree_part(int kind, int part, int axis);
int tree_part_color(int kind, int part);
int tree_at(int gx, int gz, int seed);
int tree_jitter_milli(int gx, int gz, int seed, int axis);
int tree_scale_permille(int gx, int gz, int seed);
int tree_max_hp(int kind);
int tree_chop_damage(int weapon);
int tree_wood_per_hit(int kind);
int tree_fell_bonus(int kind);
int tree_regrow_ms(int kind);
int tree_cell_milli(void);
int res_cap(int res);

#endif
