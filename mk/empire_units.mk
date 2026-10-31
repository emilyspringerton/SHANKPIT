# mk/empire_units.mk -- the EMPIRE unit model (2026-10-31).
#
# Founder: "spawn in allies like mike etc ... exploring the environment engaging enemies following allies
# taking over territory gathering resources ... the ais also need some generic stuff that all humans will
# inherit but not all ais will be human ... spawn in citizens zombies soldiers etc". The archetype table,
# the two inheritance rows and the order priorities live in PARENA
# (stdlib/shankpit/empire_units.prn); packages/simulation/empire_units.h is the unit and its decision.
#
# NOT empire_rules.prn/.c: those already exist and are a different EMPIRE module (the influence economy
# and the 72-district sphere behind the phone app, see the Makefile's own regen-empire-rules). This is the
# unit layer that spends that influence on bodies, so the two are siblings, not the same file.
.PHONY: regen-empire-units test-empire-units test-empire-spawn

regen-empire-units:
	scripts/gen_empire_units.sh

# test-empire-units -- the three tables and the decision: PARENA's generated rules equal the header's own
# defaults sample for sample (the arrangement every other rules module keeps), every human archetype
# inherits the human row and the zombie does not, a masked-out order has priority 0, the order race picks
# the highest triggered priority with GUARD as the standing fallback and NONE as "use the role's brain",
# and a real world claims/gathers through the same functions the AI tick calls.
# test-empire-spawn -- the wiring, not the model: story_ai_spawn_unit turns an archetype into a real player
# slot with the archetype's role/skin/health, the tick chooses the unit's order from the shared world, and
# the gather/claim runs really drain a node and really take a cell. Runs in MODE_ZOMBIES on purpose -- an
# authored unit must tick outside story mode (that gate is what this test pins down).
# witness_ai_seed_voxworld_encounter is stubbed in the test TU: story_ai.c's only outside dependency here.
test-empire-spawn: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Ipackages/common -Ipackages/simulation \
		packages/simulation/empire_spawn_test.c packages/simulation/story_ai.c \
		packages/simulation/ai_nav.c packages/simulation/humanness.c \
		packages/simulation/empire_units_rules.c -o $(BIN_DIR)/empire_spawn_test $(LIBS_M)
	./$(BIN_DIR)/empire_spawn_test

test-empire-units: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Ipackages/simulation \
		packages/simulation/empire_units_test.c packages/simulation/empire_units_rules.c \
		-o $(BIN_DIR)/empire_units_test $(LIBS_M)
	./$(BIN_DIR)/empire_units_test
