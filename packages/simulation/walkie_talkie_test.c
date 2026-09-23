/* walkie_talkie_test.c -- real, standalone test for the walkie-talkie channel/hearing decision
 * logic (EMILY/BACKLOG.md SECTION 536 follow-up). Plain assert() harness, same convention as
 * every other test in this merge.
 *
 * Build and run:
 *   gcc -Wall -Wextra -O2 -Ipackages/simulation -o /tmp/walkie_talkie_test \
 *       packages/simulation/walkie_talkie_test.c packages/simulation/walkie_talkie.c \
 *       packages/simulation/walkie_rules.c -lm && /tmp/walkie_talkie_test
 */
#include "walkie_talkie.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    {
        walkie_talkie_reset();
        walkie_talkie_set_team(0, 2);
        assert(walkie_talkie_channel_for_player(0) == 3002);
        printf("PASS: a team-assigned player gets the real 3000+team_id ConfBridge extension\n");
    }
    {
        walkie_talkie_reset();
        walkie_talkie_set_team(1, WALKIE_NO_TEAM);
        assert(walkie_talkie_channel_for_player(1) == 2999);
        printf("PASS: an unassigned player gets the real shared 2999 local-chatter channel\n");
    }
    {
        walkie_talkie_reset();
        assert(walkie_talkie_channel_for_player(999) == -1);
        printf("PASS: an out-of-range player id is a real, safe no-op\n");
    }
    {
        walkie_talkie_reset();
        walkie_talkie_set_ptt(3, 1);
        assert(walkie_talkie_ptt_active(3));
        walkie_talkie_set_ptt(3, 0);
        assert(!walkie_talkie_ptt_active(3));
        printf("PASS: push-to-talk state is real and toggles correctly\n");
    }
    {
        /* Same team -- always heard, regardless of real, large distance. */
        walkie_talkie_reset();
        walkie_talkie_set_team(0, 1);
        walkie_talkie_set_team(1, 1);
        assert(walkie_talkie_can_hear(0, 1, 500.0f));
        printf("PASS: same-team transmissions are heard for real at any real distance\n");
    }
    {
        /* Different team -- only within the real overhear radius. */
        walkie_talkie_reset();
        walkie_talkie_set_team(0, 1);
        walkie_talkie_set_team(1, 2);
        assert(walkie_talkie_can_hear(0, 1, 3.0f));   /* close -- overheard */
        assert(!walkie_talkie_can_hear(0, 1, 20.0f)); /* far -- not overheard */
        printf("PASS: cross-team transmissions are only overheard within the real radius\n");
    }

    printf("\nALL PASS\n");
    return 0;
}
