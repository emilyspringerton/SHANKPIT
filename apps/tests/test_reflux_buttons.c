// test_reflux_buttons.c -- S485, real, live-verified test for the REFLUX pub/sub button/door
// pattern (founder real-time: "how am i gonna put a button on a wall next to a door to open a
// door?" -> "the bridge listens for a button the button has no idea the bridge exists"). Proves,
// with real function calls (not mocked), that:
//   1. an edge-triggered interact press near a button dispatches REFLUX_ACTION_BUTTON_PRESSED
//      with the button's own author-assigned id, and nothing else;
//   2. a door with a matching subscribe_button_id toggles open on that dispatch, ignoring
//      distance to the player entirely;
//   3. a second press toggles it closed again (real agency, not one-shot);
//   4. an UNRELATED door (subscribe_button_id=-1, the normal case) is completely unaffected --
//      still driven by proximity, proving this feature is additive, not a regression.
#include <stdio.h>
#include <string.h>

#include "../../packages/common/net_sim.h"
#include "../../packages/simulation/local_game.h"
#include "../../packages/world/story_doors.h"
#include "../../packages/reflux/reflux_runtime.h"

// Real cutscene-handshake globals normally defined in apps/server or apps/lobby's own main.c --
// same stub pattern packages/simulation/story_swarm_humanness_test.c already establishes for a
// standalone test binary that links local_game.h without either real app's main.c.
int g_story_cutscene_done   = 0;
int g_story_outro_requested = 0;
int g_shankpit_is_server    = 1;

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT_TRUE(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        printf("FAIL %s\n", msg); \
    } else { \
        printf("PASS %s\n", msg); \
        tests_passed++; \
    } \
} while (0)

int main(void) {
    printf("--- REFLUX Button/Door Pub/Sub (S485) ---\n");
    local_init_match(2, MODE_DEATHMATCH);
    reflux_host_reset();

    // Two real boxes: box 0 is the button's own physical panel, box 1 is a real door.
    float x[2] = {0.0f, 20.0f};
    float y[2] = {0.0f, 0.0f};
    float z[2] = {0.0f, 0.0f};
    float w[2] = {2.0f, 2.0f}, h[2] = {8.0f, 8.0f}, d[2] = {2.0f, 8.0f};
    float r[2] = {0.5f, 0.5f}, g[2] = {0.5f, 0.5f}, b[2] = {0.5f, 0.5f};
    int mat[2] = {0, 0};
    phys_set_custom_level(x, y, z, w, h, d, r, g, b, mat, 2, 0, 1);

    CustomLevelData lvl;
    memset(&lvl, 0, sizeof(lvl));
    lvl.button_count = 1;
    lvl.buttons[0].box_index = 0;
    lvl.buttons[0].button_id = 42;
    lvl.door_count = 2;
    lvl.doors[0].box_index = 1;
    lvl.doors[0].subscribe_button_id = 42; // the real door under test -- subscribes to button 42
    lvl.doors[1].box_index = 1;
    lvl.doors[1].subscribe_button_id = -1; // control: a normal proximity door, must be unaffected

    // NOTE: both doors reference the same box_index for simplicity (this test only checks each
    // DoorRuntime's own .state, not collision side effects, so sharing a box is harmless here).
    story_doors_init(&lvl);
    story_buttons_init(&lvl);

    ASSERT_TRUE(g_story_button_count == 1, "one button registered");
    ASSERT_TRUE(g_story_door_count == 2, "two doors registered (one subscribed, one control)");

    DoorRuntime *subscribed_door = &g_story_doors[0];
    DoorRuntime *control_door = &g_story_doors[1];
    ASSERT_TRUE(subscribed_door->subscribe_button_id == 42, "door 0 subscribed to button 42");
    ASSERT_TRUE(control_door->subscribe_button_id == -1, "door 1 is a normal, unsubscribed door");
    ASSERT_TRUE(subscribed_door->state < STORY_DOOR_OPEN_THRESHOLD, "subscribed door starts closed");

    PlayerState *p = &local_state.players[1];
    p->active = 1;
    p->state = STATE_ALIVE;
    p->id = 1;
    p->x = 0.0f; p->y = 0.0f; p->z = 0.0f; // standing right at the button's own box
    p->in_use = 1;
    p->use_was_down = 0; // real edge: this tick is the actual key-DOWN transition

    int reflux_size_before = reflux_host_log_size();
    story_buttons_handle_use_interactions(p, 1000);
    int reflux_size_after = reflux_host_log_size();
    ASSERT_TRUE(reflux_size_after == reflux_size_before + 1, "exactly one REFLUX action dispatched on press");
    ASSERT_TRUE(reflux_host_action_type_at(reflux_size_after - 1) == REFLUX_ACTION_BUTTON_PRESSED, "dispatched action is BUTTON_PRESSED");
    ASSERT_TRUE(reflux_host_action_a_at(reflux_size_after - 1) == 42, "dispatched action carries the real button_id (42)");
    ASSERT_TRUE(reflux_host_action_b_at(reflux_size_after - 1) == 1, "dispatched action carries the real pressing player's id");

    // Move the player far away from BOTH boxes before ticking doors -- proves the subscribed
    // door's own toggle is driven by the REFLUX dispatch alone, not proximity.
    p->x = 9999.0f; p->z = 9999.0f;
    story_doors_tick(local_state.players, MAX_CLIENTS);

    ASSERT_TRUE(subscribed_door->state >= STORY_DOOR_OPEN_THRESHOLD, "subscribed door opened after button press, despite player being far away");
    ASSERT_TRUE(control_door->state < STORY_DOOR_OPEN_THRESHOLD, "control (unsubscribed) door is completely unaffected by the button press");

    // Second press (player back at the button, real edge again) must TOGGLE the door closed --
    // real agency, not a one-shot latch.
    p->x = 0.0f; p->z = 0.0f;
    p->use_was_down = 0;
    story_buttons_handle_use_interactions(p, 2000);
    p->x = 9999.0f; p->z = 9999.0f;
    story_doors_tick(local_state.players, MAX_CLIENTS);
    ASSERT_TRUE(subscribed_door->state < STORY_DOOR_OPEN_THRESHOLD, "second press toggles the subscribed door back closed");

    // Real edge-trigger discipline: holding the key down (use_was_down already 1) must NOT
    // re-fire every tick.
    p->x = 0.0f; p->z = 0.0f;
    p->use_was_down = 1; // simulates "already was down last tick"
    int before_holding = reflux_host_log_size();
    story_buttons_handle_use_interactions(p, 3000);
    int after_holding = reflux_host_log_size();
    ASSERT_TRUE(after_holding == before_holding, "holding the interact key does not re-fire the button every tick");

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
