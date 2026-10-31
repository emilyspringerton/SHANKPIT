# mk/anim_states.mk -- the thirteen-state DRAW path (gband_skel_npc_draw_state), on a real clipset.
#
# anim_clipset_test.c covers the selector (which state should play); this covers the renderer end that
# was missing: does the kit actually sample the clip for the state it was handed, does a one-shot hold its
# last tick, does an unloaded state fall back instead of drawing nothing, and do the thirteen names in
# packages/goldenband still equal anim_state.h's contract?
.PHONY: test-anim-states
test-anim-states: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror \
		-Ipackages/simulation -Ipackages/goldenband -Ipackages/common \
		packages/simulation/anim_states_test.c $(GB_LIB_SRC) \
		packages/goldenband/gband_skel_npc.c packages/goldenband/gmesh.c packages/goldenband/gpose.c \
		-o $(BIN_DIR)/anim_states_test $(LIBS_M)
	./$(BIN_DIR)/anim_states_test
