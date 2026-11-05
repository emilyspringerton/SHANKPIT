# mk/municipal.mk -- the civic district (levels/municipal, tools/build_municipal_district.py).
#
# The lofts proved the recursive-level pipeline on towers; this is the same pipeline on the other shape:
# six sprawling one- and two-storey public buildings, eight shared sub-levels, six new level materials
# (tile, plaster, marble and three wood species with their own densities) and one chest per building.
#
# test-municipal-city loads the EXPORTED city through level_boxes.h -- the engine's own parser, not the
# JSON -- and asserts the materials, their densities, the wall budget, the stair ramps, the chest count
# and the save round-trip. It is deliberately separate from make test-loft-city: that one is about the
# eight towers' population and loot, and this one is about the district the founder asked to be built
# beside them.
#
# Regenerate after editing the authored tree: python3 tools/build_municipal_district.py

MUNICIPAL_CITY_TEST := $(BIN_DIR)/municipal_city_test
MUNICIPAL_DEFENSE_TEST := $(BIN_DIR)/municipal_defense_test

.PHONY: test-municipal-city test-municipal-defense municipal-city
municipal-city:
	@python3 tools/build_municipal_district.py

test-municipal-city: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror \
		-Ipackages/common -Ipackages/simulation -Ipackages/world \
		packages/simulation/municipal_city_test.c \
		packages/world/parena_runtime.c \
		-o $(MUNICIPAL_CITY_TEST) $(LIBS_M)
	./$(MUNICIPAL_CITY_TEST)

# test-municipal-defense -- the district's twelve bodies through the REAL story_ai brain AND the real
# weapon pipeline: does a clerk take the ENGAGE order, face the player, pull the trigger, close the
# distance, and does the player's health actually drop? municipal_city_test only ever asserted where
# they STAND, which a body can do forever without fighting. physics.h is header-only, so the link line
# does not change to get update_weapons/check_hit_location/katana_apply_damage -- the same call
# local_game.h's update_entity makes for every slot every tick. The staff are six distinct jobs (one
# per building: archetype + brain role + kit), asserted here rather than trusted to the tool.
#
# -Wno-unused-parameter: physics.h's own phys_respawn has an unused `now` and this target keeps the
# strict flags for everything the TEST writes (test-magnum-spray builds with -Wall only, for the same
# reason). Nothing else is relaxed.
test-municipal-defense: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Wno-unused-parameter \
		-Ipackages/common -Ipackages/simulation -Ipackages/world \
		packages/simulation/municipal_defense_test.c \
		packages/simulation/story_ai.c \
		packages/simulation/ai_nav.c \
		packages/simulation/humanness.c \
		packages/simulation/empire_units_rules.c \
		packages/simulation/chests.c \
		packages/simulation/chest_rules.c \
		packages/simulation/gun_items.c \
		packages/world/parena_runtime.c \
		packages/world/terrain.c \
		-o $(MUNICIPAL_DEFENSE_TEST) $(LIBS_M)
	./$(MUNICIPAL_DEFENSE_TEST)
