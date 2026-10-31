# stats_bridge.mk -- GFD's job table mapped into engine units (phase 4 of docs/GFD_UNIFICATION.md).
#
# The rules are PARENA's; the header carries identical C defaults; the test compares the two copies and
# pins the table to GoblinFoxDragon/server/job/job.go, which is the thing that would silently rot.
.PHONY: regen-stats-bridge test-stats-bridge

regen-stats-bridge:
	scripts/gen_stats_bridge.sh

test-stats-bridge: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Ipackages/simulation \
		packages/simulation/stats_bridge_test.c packages/simulation/stats_bridge.c \
		-o $(BIN_DIR)/stats_bridge_test -lm
	./$(BIN_DIR)/stats_bridge_test
