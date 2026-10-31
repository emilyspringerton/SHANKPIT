#ifndef SHANKPIT_ARENA_RULES_H
#define SHANKPIT_ARENA_RULES_H

/* arena_rules.h -- combat and slippery-physics rules, generated from PARENA and shared with NOOBTOPIA.
 *
 * This file is only the prototype list: the numbers live in PARENA/stdlib/noobtopia/arena_rules.prn, emitted to
 * packages/simulation/arena_rules.c by scripts/gen_noobtopia_rules.sh and to NOOBTOPIA/src/generated/arena_rules.lua
 * by NOOBTOPIA/scripts/gen_rules.sh. One source, two worlds -- see MONOREPO/docs/THREE_WAY_MERGE.md.
 *
 * Weapon ids:  0 FIST  1 BAT  2 PISTOL  3 ROCKET
 * Surface ids: 0 CONCRETE  1 ICE  2 GRASS  3 METAL  4 MUD
 */

int weapon_damage(int weapon);
int weapon_knockback_permille(int weapon);
int weapon_hit_chance_permille(int weapon, int dist);
int weapon_hit_base(int weapon);
int weapon_cooldown_ticks(int weapon);

int surface_friction_permille(int surface);
int surface_accel(int surface);
int surface_jump_impulse(int surface);
int step_speed(int speed, int wish, int surface);
int slide_decay(int speed, int surface);

#endif
