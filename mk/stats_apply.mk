# mk/stats_apply.mk -- a GFD job, applied to a live player (health + weapon cooldown).
#
# The bridge's own table/mapping is covered by test-stats-bridge. This covers the part that was missing for
# months: an installed hook actually changing what phys_respawn grants and what a shot leaves behind.
.PHONY: test-stats-apply
test-stats-apply: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Wno-unused-variable -Wno-unused-parameter \
		-Ipackages/common -Ipackages/simulation \
		packages/simulation/stats_apply_test.c packages/simulation/stats_bridge.c \
		packages/world/terrain.c \
		-o $(BIN_DIR)/stats_apply_test $(LIBS_M)
	./$(BIN_DIR)/stats_apply_test
