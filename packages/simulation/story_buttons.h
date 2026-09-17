// story_buttons.h -- S485, real, reusable interact-triggered buttons. Founder real-time: "how am
// i gonna put a button on a wall next to a door to open a door?" -> "we need a reusable button
// that can be put in different places" -> "i want to follow the pub sub model ... the bridge
// listens for a button the button has no idea the bridge exists."
//
// A button knows nothing about what it controls. Pressing it dispatches
// REFLUX_ACTION_BUTTON_PRESSED(button_id, player_id, 0) into the shared REFLUX log
// (packages/reflux/reflux_runtime.h) and stops -- any number of subscribers (today: doors with a
// matching LevelDoor.subscribe_button_id, see story_doors.h) react independently, with zero
// reference back to this file. Deliberately real, edge-triggered player INTERACT input (BTN_USE,
// already wired end to end: apps/lobby's own F-key binding -> UserCmd.buttons -> p->in_use,
// packages/common/net_sim.h), not walk-up proximity -- founder's own explicit reasoning: "a door
// that is open and closable gives you more agency... in a situation with zombies... the zombies
// cant hit the button." Story AI characters (packages/simulation/story_ai.c) are server-simulated
// and never send a UserCmd at all, so an interact-driven button is automatically player-only --
// no separate "exclude NPCs" check needed, it falls out of the input model for free. Mirrors
// local_game.h's own established ctf_handle_use_interactions shape (same edge-trigger idiom,
// same p->in_use/p->use_was_down fields), generalized to run in every game mode, not just one --
// per S481c's own resolved policy, a general level feature is never hard-gated to one mode.
#ifndef SHANKPIT_STORY_BUTTONS_H
#define SHANKPIT_STORY_BUTTONS_H

#include <math.h>

#include "../world/level_boxes.h"
#include "../reflux/reflux_runtime.h"

// BUTTON_USE_RADIUS -- how close (world units) a player must be to a button's own box for a
// press to register. Matches CTFB_USE_RADIUS's own order of magnitude (28.0f) loosely, but a
// button is a small, deliberately-aimed-at wall fixture, not a flag pickup zone -- kept tighter
// so a player can't fire a button from clear across a room.
#define BUTTON_USE_RADIUS 6.0f

typedef struct {
    int box_index;
    int button_id;
} ButtonRuntime;

#define STORY_BUTTONS_MAX LEVEL_BOXES_MAX_BUTTONS
static ButtonRuntime g_story_buttons[STORY_BUTTONS_MAX];
static int g_story_button_count = 0;

static inline void story_buttons_init(const CustomLevelData *lvl) {
    g_story_button_count = 0;
    for (int i = 0; i < lvl->button_count && g_story_button_count < STORY_BUTTONS_MAX; i++) {
        g_story_buttons[g_story_button_count].box_index = lvl->buttons[i].box_index;
        g_story_buttons[g_story_button_count].button_id = lvl->buttons[i].button_id;
        g_story_button_count++;
    }
}

// story_buttons_handle_use_interactions -- real, edge-triggered (fires once per real key-down,
// not every tick held), called once per active player per server tick, same call shape
// ctf_handle_use_interactions already established (see local_game.h). Checks every placed
// button's own box distance; the first one in range wins (buttons are small, deliberately-placed
// fixtures -- two buttons within BUTTON_USE_RADIUS of each other and of the player at once is a
// real, unlikely level-design edge case, not defended against specially here).
static inline void story_buttons_handle_use_interactions(PlayerState *p, unsigned int now_ms) {
    (void)now_ms; // no cooldown/timestamp bookkeeping needed yet -- reserved for parity with
                  // ctf_handle_use_interactions's own signature and real future use (e.g. a
                  // per-button re-press cooldown), not unused-and-forgotten.
    if (!p->in_use || p->use_was_down) return;
    for (int i = 0; i < g_story_button_count; i++) {
        ButtonRuntime *btn = &g_story_buttons[i];
        float bx, by, bz;
        if (!phys_custom_level_box_pos(btn->box_index, &bx, &by, &bz)) continue;
        float dx = p->x - bx, dy = p->y - by, dz = p->z - bz;
        float dist_sq = dx * dx + dy * dy + dz * dz;
        if (dist_sq <= (BUTTON_USE_RADIUS * BUTTON_USE_RADIUS)) {
            reflux_host_dispatch(REFLUX_ACTION_BUTTON_PRESSED, btn->button_id, p->id, 0);
            return; // one press, one dispatch -- don't fire every button simultaneously in range
        }
    }
}

#endif // SHANKPIT_STORY_BUTTONS_H
