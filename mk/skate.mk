# mk/skate.mk -- the skateboard (2026-11-14, founder: "can we get shankpit skateboarding working?").
#
# The feel lives in PARENA (stdlib/shankpit/skate_rules.prn -> packages/simulation/skate_rules.c); the state and its
# integration are packages/simulation/skate.h; the lobby wires K, the HUD and the board model.

.PHONY: regen-skate-rules test-skate

regen-skate-rules:
	scripts/gen_skate_rules.sh

# test-skate -- the board on its own: push to the top speed and no further, roll, brake, carve rate by speed, a hill
# both ways, the ollie, flips, grabs, spins, clean / switch / sideways / mid-flip landings, combos, the bail and back on.
test-skate: | $(BIN_DIR)
	$(CC) -std=c99 -O1 -Wall -Wextra -Werror $(INCLUDES) -fsanitize=address,undefined packages/simulation/skate_test.c \
		packages/simulation/skate_rules.c packages/world/parena_runtime.c -o $(BIN_DIR)/skate_test $(LIBS_M)
	./$(BIN_DIR)/skate_test
