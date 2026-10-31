# mk/loft.mk -- the authored loft city (levels/lofts, tools/build_loft_city.py) through the real loaders.
#
# test-loft-city exists to close two gaps the city shipped with when it landed: the 24 chests were placed
# and parsed but nobody had ever hit one, and no zombie on an upper floor had ever been watched walking.
# It loads var/zombie/nextown_zombies.json through level_boxes.h (the engine's own parser, not the JSON),
# places every authored chest through chests_place, breaks one, and then spawns EVERY authored character
# through story_ai_spawn_unit before ticking the highest one.
#
# Regenerate the city first if the loft tree changed: python3 tools/build_loft_city.py

LOFT_CITY_TEST := $(BIN_DIR)/loft_city_test

.PHONY: test-loft-city loft-city
loft-city:
	@python3 tools/build_loft_city.py

test-loft-city: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror \
		-Ipackages/common -Ipackages/simulation -Ipackages/world \
		packages/simulation/loft_city_test.c \
		packages/simulation/story_ai.c \
		packages/simulation/ai_nav.c \
		packages/simulation/humanness.c \
		packages/simulation/empire_units_rules.c \
		packages/simulation/chests.c \
		packages/simulation/chest_rules.c \
		packages/simulation/gun_items.c \
		packages/world/parena_runtime.c \
		-o $(LOFT_CITY_TEST) $(LIBS_M)
	./$(LOFT_CITY_TEST)
