/* packages/simulation/level_manager_rules.h -- the level manager's decisions, generated from
 * PARENA/stdlib/shankpit/level_manager_rules.prn (scripts/gen_level_manager_rules.sh). T0 step 4,
 * docs/T0_LEVEL_WASM_CONTRACT.md section 2, "Level manager mod". apps/lobby/src/level_shell.c holds the state
 * and calls these; it decides nothing itself (INV-1.2). All total: every input has an answer. */
#ifndef SHANKPIT_LEVEL_MANAGER_RULES_H
#define SHANKPIT_LEVEL_MANAGER_RULES_H

/* states */
int lm_st_none(void);          /* 0: nothing loaded */
int lm_st_loading(void);       /* 1: the host instantiates this frame (boot starts here) */
int lm_st_fg(void);            /* 2: the level in the foreground, running */
int lm_st_bg(void);            /* 3: the world in the foreground, the level suspended */
int lm_st_source(void);        /* 4: the view-source overlay over a suspended level */
int lm_st_source_none(void);   /* 5: the overlay, no level */

/* commands */
int lm_cmd_toggle(void);         /* 1: Shift+M */
int lm_cmd_source(void);         /* 2: Ctrl+Shift+J, Cmd+Option+J on macOS */
int lm_cmd_close(void);          /* 3: Shift+X */
int lm_cmd_load_ok(void);        /* 4 */
int lm_cmd_load_fail(void);      /* 5 */
int lm_cmd_trap(void);           /* 6 */
int lm_cmd_overlay_closed(void); /* 7 */
int lm_cmd_suspend(void);        /* 8: the game leaves the world for its menu: FG/SOURCE -> BG, SOURCE_NONE -> NONE */

/* the command a key-down is, 0 none. sym = SDL keycode; the flags are 0/1; mac = running on macOS;
 * text_focus = chat, orb, a phone text app, a lobby text field or the editor overlay has the keyboard;
 * repeat = SDL_KeyboardEvent.repeat. */
int lm_chord(int sym, int shift, int ctrl, int alt, int gui, int mac, int text_focus, int repeat);

int lm_next(int state, int cmd);        /* the state after cmd; an inapplicable command returns state */
int lm_announce(int old_state, int new_state); /* the lifecycle action type to dispatch, 0 none */
int lm_close_reason(int cmd);           /* LEVEL_CLOSED payload b: 1 trap, 0 the player */
int lm_loaded(int state);               /* 1: an instance exists (FG, BG, SOURCE) */
int lm_ticks(int state);                /* 1: step the level this frame */
int lm_owns_input(int state);           /* 1: keyboard/mouse go to the level, the world gets none */
int lm_world_input(int state);          /* 1: the world reads its keys/mouse/pad and may grab the mouse (NONE, BG) */
int lm_banner(int state);               /* 0 none, 1 "Shift+M resume / Shift+X close", 2 "Shift+M play" */

/* the fixed 60 Hz step, in microseconds */
int lm_max_steps(void);                 /* catch-up cap per display frame (2) */
int lm_tick_us(void);                   /* 16667 */
int lm_dt_cap_us(void);                 /* one frame's dt is capped at this */
int lm_acc_add(int acc_us, int dt_us);  /* the accumulator after a frame of dt */
int lm_steps(int acc_us);               /* steps to run now */
int lm_acc_left(int acc_us);            /* the accumulator after running them */

/* SDL keycode -> the DOM keyCode a level reads (+256 = right-hand Shift/Ctrl/Alt/Meta), 0 = not given */
int lm_dom_key(int sym);

/* save -> compile -> swap (T0 step 6, dev boxes): build now? / does this transition swap in a new build? */
int lm_compile_now(int changed, int compiling, int can_build);
int lm_swap_on(int old_state, int new_state);

#endif /* SHANKPIT_LEVEL_MANAGER_RULES_H */
