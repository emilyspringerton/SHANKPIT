# mk/anim_rules.mk -- the animation state machine (2026-10-31).
#
# Founder paste: the Mixamo + XPBD ragdoll spec's §2 (the thirteen clip set). The ragdoll half was
# already built (ragdoll_pool.h); this is the half that decides WHICH clip a tick belongs to, plus the
# clip-name contract an imported skin has to satisfy.

.PHONY: regen-anim-rules test-anim-state

regen-anim-rules:
	scripts/gen_anim_rules.sh

# test-anim-state -- priority (a swing beats a shot, a shot survives being airborne), the loop and
# additive flags, the melee windows against protocol.h's own weapon table, and the standing eye height
# against physics.h's real EYE_HEIGHT so the camera and the pose cannot drift apart.
test-anim-state: | $(BIN_DIR)
	# physics.h defines the whole engine's inline physics, so the probe TU is built with function
	# sections and the link drops everything the probe does not call -- otherwise linking it would
	# need terrain.c and the bot code the animation rules have nothing to do with.
	$(CC) -std=gnu99 -O1 -w -ffunction-sections -fdata-sections -Ipackages/common \
		-c packages/simulation/anim_engine_constant.c -o $(BIN_DIR)/anim_engine_constant.o
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Ipackages/common -Ipackages/simulation \
		packages/simulation/anim_state_test.c packages/simulation/anim_rules.c \
		$(BIN_DIR)/anim_engine_constant.o -o $(BIN_DIR)/anim_state_test $(LIBS_M) -Wl,--gc-sections
	./$(BIN_DIR)/anim_state_test
