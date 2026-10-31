# mk/slide_rules.mk -- the crouch-slide's slope half (2026-10-31).
#
# Founder: "sliding down a hill accelerates you or whatever". The curve lives in PARENA so the server
# and the client cannot disagree about a hill; physics.h holds matching compiled-in defaults so every
# existing test that includes physics.h still links without a generated file.

.PHONY: regen-slide-rules test-slide-slope

regen-slide-rules:
	scripts/gen_slide_rules.sh

# test-slide-slope -- a real 45 degree heightfield, sampled through terrain_sample_normal: flat slides
# decay exactly as before, downhill slides outrun a sprint, uphill slides earn nothing, the cap holds,
# and the PARENA curve equals the compiled-in defaults sample for sample.
test-slide-slope: | $(BIN_DIR)
	$(CC) -O1 -Wall $(INCLUDES) packages/simulation/slide_rules_test.c \
		packages/simulation/slide_rules.c packages/world/terrain.c \
		-o $(BIN_DIR)/slide_rules_test $(LIBS_M)
	./$(BIN_DIR)/slide_rules_test
