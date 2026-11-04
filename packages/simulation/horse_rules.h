/* horse_rules.h -- the tölt mount's handling numbers, as a function table.
 *
 * The real numbers live in PARENA/stdlib/shankpit/horse_rules.prn, generated into horse_rules.c
 * (`make regen-horse-rules`) and installed by horse_rules_host.h. local_game.h includes THIS header
 * only for the table type and the accessor, so a translation unit that never installs the mod (most
 * tests) rides on the built-in defaults below -- exactly the shape physics.h's own PhysBuggyRules
 * already uses for the buggy. */
#ifndef HORSE_RULES_H
#define HORSE_RULES_H

typedef struct {
    int (*max_speed_milli)(void);          /* m/s x 1000 */
    int (*tolt_drain_milli)(int speed_milli); /* stamina milli lost per second at that speed */
    int (*stamina_regen_milli)(void);      /* stamina milli regained per second standing still */
    int (*jump_cost_milli)(void);          /* stamina milli a jump costs */
    int (*jump_speed_milli)(void);         /* launch velocity, m/s x 1000 */
    int (*carrot_restore_milli)(void);     /* stamina milli one carrot gives back */
    int (*fatigue_permille)(int stamina_milli); /* top-speed multiplier, 1000 = 1.0 */
    int (*can_jump)(int stamina_milli, int grounded);
} HorseRules;

/* Built-in defaults -- the pre-mod constants, kept byte-identical to horse_rules.prn's own numbers
   so a test that never installs the mod exercises the same horse the game ships. */
static int horse_rules_builtin_max_speed_milli(void) { return 8000; }
static int horse_rules_builtin_tolt_drain_milli(int speed_milli) {
    if (speed_milli < 0) return 0;
    if (speed_milli > 8000) speed_milli = 8000;
    return (90 * speed_milli) / 8000;
}
static int horse_rules_builtin_stamina_regen_milli(void) { return 55; }
static int horse_rules_builtin_jump_cost_milli(void) { return 180; }
static int horse_rules_builtin_jump_speed_milli(void) { return 5600; }
static int horse_rules_builtin_carrot_restore_milli(void) { return 400; }
static int horse_rules_builtin_fatigue_permille(int stamina_milli) {
    return stamina_milli < 250 ? 620 : 1000;
}
static int horse_rules_builtin_can_jump(int stamina_milli, int grounded) {
    return (grounded > 0 && stamina_milli > 199) ? 1 : 0;
}

static const HorseRules g_horse_rules_builtin = {
    horse_rules_builtin_max_speed_milli,
    horse_rules_builtin_tolt_drain_milli,
    horse_rules_builtin_stamina_regen_milli,
    horse_rules_builtin_jump_cost_milli,
    horse_rules_builtin_jump_speed_milli,
    horse_rules_builtin_carrot_restore_milli,
    horse_rules_builtin_fatigue_permille,
    horse_rules_builtin_can_jump,
};

static const HorseRules *g_horse_rules_active = 0;

static inline void phys_set_horse_rules(const HorseRules *r) { g_horse_rules_active = r; }

/* Never returns NULL: an uninstalled host rides the built-in defaults, never a crash. */
static inline const HorseRules *horse_rules(void) {
    return g_horse_rules_active ? g_horse_rules_active : &g_horse_rules_builtin;
}

#endif /* HORSE_RULES_H */
