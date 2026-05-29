#include <stdio.h>
#include <math.h>
#include <netinet/in.h>

#include "../../packages/common/net_sim.h"
#include "../../packages/simulation/local_game.h"

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT_TRUE(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        printf("❌ %s\n", msg); \
    } else { \
        printf("✅ %s\n", msg); \
        tests_passed++; \
    } \
} while (0)

int main(void) {
    printf("--- CTFB Respawn/Flag Drop Regression ---\n");

    local_init_match(12, MODE_CTFB);

    PlayerState *carrier = &local_state.players[0];
    PlayerState *attacker = &local_state.players[6];
    unsigned int death_ms = 1000;

    carrier->x = 123.0f;
    carrier->y = 45.0f;
    carrier->z = -77.0f;
    carrier->health = 40;
    carrier->shield = 0;
    carrier->state = STATE_ALIVE;

    carrier->carried_flag_team_id = TDMB_RED_TEAM;
    CtfFlagState *enemy_flag = &local_state.ctf.flags[TDMB_RED_TEAM];
    enemy_flag->state = FLAG_CARRIED;
    enemy_flag->carrier_id = carrier->id;

    apply_projectile_damage(attacker, carrier, 50, death_ms, 0.0f, 0.0f);

    ASSERT_TRUE(carrier->state == STATE_DEAD, "Carrier is dead immediately after lethal hit");
    ASSERT_TRUE(carrier->respawn_time == death_ms + CTFB_RESPAWN_DELAY_MS, "Carrier respawn is scheduled 3000ms later");
    ASSERT_TRUE(carrier->carried_flag_team_id == -1, "Carrier flag state is cleared on death");

    ASSERT_TRUE(enemy_flag->state == FLAG_DROPPED, "Enemy flag is dropped on carrier death");
    ASSERT_TRUE(enemy_flag->carrier_id == -1, "Dropped flag has no carrier");
    ASSERT_TRUE(fabsf(enemy_flag->x - carrier->x) < 0.01f && fabsf(enemy_flag->z - carrier->z) < 0.01f,
                "Dropped flag is placed at carrier death x/z");
    ASSERT_TRUE(enemy_flag->dropped_until_ms == death_ms + CTFB_DROPPED_RETURN_MS,
                "Dropped flag return timer is updated");

    local_update(0, 0, carrier->yaw, carrier->pitch, 0, carrier->current_weapon, 0, 0, 0, 0, 0, NULL, death_ms + 1000);
    ASSERT_TRUE(carrier->state == STATE_DEAD, "Carrier stays dead before 3000ms delay expires");

    local_update(0, 0, carrier->yaw, carrier->pitch, 0, carrier->current_weapon, 0, 0, 0, 0, 0, NULL, death_ms + CTFB_RESPAWN_DELAY_MS);
    ASSERT_TRUE(carrier->state == STATE_ALIVE, "Carrier respawns after 3000ms delay");
    ASSERT_TRUE(carrier->carried_flag_team_id == -1, "Respawned carrier does not keep enemy flag state");

    int blue_score_before = local_state.team_scores[TDMB_BLUE_TEAM];
    ctf_try_capture(carrier, death_ms + CTFB_RESPAWN_DELAY_MS + 1);
    ASSERT_TRUE(local_state.team_scores[TDMB_BLUE_TEAM] == blue_score_before,
                "Respawned carrier cannot capture without re-picking up flag");

    printf("SUMMARY: %d/%d Tests Passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
