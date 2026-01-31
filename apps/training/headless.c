#include "../../packages/simulation/local_game.h"

// Export functions for Python
void sim_init(int bots) {
    local_init_match(bots, 0);
}

void sim_step(float fwd, float strafe, float yaw, float pitch, int shoot, int jump) {
    // We are controlling Player 0 (The Agent)
    // The "bots" in the match are the opponents
    local_update(fwd, strafe, yaw, pitch, shoot, -1, jump, 0, 0, 0, NULL, 0);
}

ServerState* sim_get_state() {
    return &local_state;
}
