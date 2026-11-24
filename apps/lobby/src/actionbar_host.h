#ifndef ACTIONBAR_HOST_H
#define ACTIONBAR_HOST_H

/* actionbar_host.h -- thin SHANKPIT host glue for the PARENA action-bar rules mod
 * (packages/simulation/action_bar_rules.c, from PARENA/stdlib/actionbar/ (.prn files)).
 *
 * The MOD owns the decisions (key+modifier -> bank/slot -> action, precedence bind > plugin > default).
 * This file owns only: reading the digit keys, the plugin registry, the optional bar's drawing and hit
 * test, and the one persisted setting. Packed action = kind*65536 + id (see action_bar_rules.h). */

#define ABH_MAX_PLUGINS 8

/* A plugin (any other PARENA mod) contributes actions. provider is required; label/activate are optional.
 *   provider(bank,slot) -> packed action or 0 (no opinion)
 *   label(id)           -> short upper-case label for a kind-4 action (<= 5 chars show on the bar), or NULL
 *   activate(id)        -> run a kind-4 action when its slot is pressed */
typedef int (*abh_provider_fn)(int bank, int slot);
typedef const char *(*abh_label_fn)(int id);
typedef void (*abh_activate_fn)(int id);
/* The player-bind layer (the lobby's macro binds): non-zero packed action if a bind claims (bank,slot), else 0. */
typedef int (*abh_bind_fn)(int bank, int slot);

int abh_register_plugin(abh_provider_fn provider, abh_label_fn label, abh_activate_fn activate); /* 1 ok, 0 full/null */
void abh_reset_plugins(void);
void abh_set_bind_fn(abh_bind_fn fn);

/* The setting (persisted by the host's display config). Hotkeys ALWAYS work; this only shows the bar. */
void abh_set_visible(int on);
int abh_visible(void);

/* Final action for (bank,slot): player bind > plugin > default (the mod's ab_resolve). */
int abh_resolve(int bank, int slot);
/* Label for a packed action ("" if empty). Never NULL. */
const char *abh_label(int action);

/* One poll per frame: keys is SDL's keyboard state array indexed by scancode, the three flags are held
 * modifiers. Digit keys 1..9,0 are level-triggered like the old hard-coded block. If a key resolves to a
 * weapon action, *wpn_req is set and 1 returned. A kind-4 action runs its plugin's activate. */
int abh_poll(const unsigned char *keys, int shift, int ctrl, int alt, int *wpn_req);

/* Minecraft-style cycling over bank 1 (skips empty slots): delta +-1. current_weapon locates the start slot.
 * Returns 1 and sets *wpn_req when it landed on a weapon. */
int abh_scroll(int delta, int current_weapon, int *wpn_req);

/* Mouse: slot index 1..10 under (mx,my) in the 1280x720 y-up virtual space, or 0. Click activates it in
 * the bank currently shown. Returns 1 and sets *wpn_req on a weapon. */
int abh_hit(float mx, float my);
int abh_click(float mx, float my, int shift, int ctrl, int alt, int *wpn_req);

/* Draw (GL immediate, caller has the 1280x720 ortho set). ammo[10] per weapon id, -1 = no ammo shown. */
void abh_draw(int shift, int ctrl, int alt, int current_weapon, const int *ammo, int reloading);

#endif
