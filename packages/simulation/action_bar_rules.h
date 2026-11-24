#ifndef ACTION_BAR_RULES_H
#define ACTION_BAR_RULES_H

/* action_bar_rules.h -- C contract for packages/simulation/action_bar_rules.c (GENERATED from
 * ../PARENA/stdlib/actionbar/action_bar_rules.prn; regenerate with scripts/gen_action_bar_rules.sh).
 *
 * PACKED ACTION (int): kind*65536 + id.  0 = none/no-opinion.
 * Banks 1..4 (none / Shift / Ctrl / Alt; Alt > Ctrl > Shift), slots 1..10 (digit keys 1..9,0).
 * Precedence: player bind > plugin > default. Bank 1 default == classic 1-9 weapon keys.
 * Plugin provider contract: int provider(int bank, int slot) -> packed action or 0. */

#define AB_BANKS 4
#define AB_SLOTS 10
#define AB_KIND_NONE 0
#define AB_KIND_WEAPON 1
#define AB_KIND_ABILITY 2
#define AB_KIND_ITEM 3
#define AB_KIND_MOD 4
#define AB_KIND_CLEARED 15
#define AB_SRC_EMPTY 0
#define AB_SRC_DEFAULT 1
#define AB_SRC_PLUGIN 2
#define AB_SRC_BIND 3
#define AB_SRC_CLEARED 4

int ab_banks(void);
int ab_slots(void);
int ab_kind_none(void);
int ab_kind_weapon(void);
int ab_kind_ability(void);
int ab_kind_item(void);
int ab_kind_mod(void);
int ab_kind_cleared(void);
int ab_pack(int kind, int id);
int ab_kind_of(int action);
int ab_id_of(int action);
int ab_valid_slot(int slot);
int ab_valid_bank(int bank);
int ab_slot_for_digit(int digit);          /* 0..9 -> 1..10 (0 = slot 10); else 0 */
int ab_digit_for_slot(int slot);           /* 1..10 -> 1..9,0; else -1 */
int ab_bank_for_mods(int shift, int ctrl, int alt);
int ab_default(int bank, int slot);
int ab_resolve(int bank, int slot, int bind, int plugin);
int ab_source(int bank, int slot, int bind, int plugin);
int ab_first_nonzero(int a, int b);
int ab_cycle_slot(int slot, int delta);

/* example plugin (action_bar_example_plugin.c): Shift+1 = mod action 1 */
int ab_plugin_example_action(int bank, int slot);

#endif
