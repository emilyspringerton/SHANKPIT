/* apps/lobby/src/arcade_cabinet.h -- the arcade cabinet in the CONSTRUCT hub (BACKLOG SECTION 638, ARCADE-0..2).
 * Founder real-time: "ensure we have an arcade machine affordance in SHANKPIT ... a menu option outside of survival
 * for CONSTRUCT that has a machine and it launches the WOTAN BRAWLPIT via WASM."
 *
 * Osaka Garage doctrine: the affordance is an object in the world. The machine has a lit screen (attract mode), a
 * marquee reading BRAWLPIT / WOTAN and a status light (green ready, amber loading or paused, red failed); F next
 * to it plays BRAWLPIT, the T0 WASM level (level_shell.c / packages/wasm_host), and Shift+M comes back to the hub.
 *
 * Every decision (at the machine?, which prompt, what F does, screen, light, attract phase) is PARENA:
 * stdlib/shankpit/level_manager_rules.prn, lm-arcade-*, generated into packages/simulation/level_manager_rules.c.
 * This file is the host: it reads positions, draws the object, and forwards the command to the level shell.
 * main.c's three calls: arcade_cabinet_hub_entered() once when the hub is started from the menu,
 * arcade_cabinet_draw_world() every game frame (in_hub = 0 outside the hub: the machine does not exist there),
 * arcade_cabinet_use() on an F key-down. */
#ifndef SHANKPIT_ARCADE_CABINET_H
#define SHANKPIT_ARCADE_CABINET_H

/* the hub was entered from the menu, world first: the boot-pending BRAWLPIT load is cancelled / a level in front
 * steps back (PARENA lm-cmd-arcade-enter), and the machine re-anchors to the hero's floor on its next frame */
void arcade_cabinet_hub_entered(void);

/* draw the machine and refresh the proximity state. hx/hy/hz = the hero's position (hy is the hero's feet) */
void arcade_cabinet_draw_world(unsigned int now_ms, int in_hub, float hx, float hy, float hz);

/* an F key-down: 1 = at the machine and the command was issued (the key is consumed) */
int arcade_cabinet_use(int in_hub, float hx, float hy, float hz);

/* the HUD line for the player's position as of the last draw (PARENA lm-arcade-prompt): 0 none, 1 play, 2 loading,
 * 3 failed (F retries), 4 resume. arcade_cabinet_hud() draws it in the 1280x720 virtual screen (level_shell.c
 * calls it inside its own 2D pass) */
int arcade_cabinet_prompt(void);
void arcade_cabinet_hud(void);

/* the pure geometry host-side, exposed for the tests: the cabinet's floor position in the hub (world x, z) */
void arcade_cabinet_position(float *x, float *z);

#endif /* SHANKPIT_ARCADE_CABINET_H */
