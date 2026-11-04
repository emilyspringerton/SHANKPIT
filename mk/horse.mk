# mk/horse.mk -- the tölt mount: PARENA horse_rules.prn, the level's horse spawns, and the ride.
#
# The horse is an actor, not scenery: level JSON "horse_spawns" (a wall named horse_spawn /
# horse_spawn_<deg>, exported by IDUNA, parsed by level_boxes.h) becomes a real HorseState in
# scene_load, and horse_tick_all rides it with the numbers from PARENA. This target proves the whole
# chain headlessly: the parser, the spawn, tölt draining stamina, the jump (a hop, not flight), the
# carrot that refills the bar, fatigue slowing a blown horse, and a scene load that clears stale
# horses and places the authored ones.

HORSE_TEST := $(BIN_DIR)/horse_test

.PHONY: test-horse regen-horse-rules
regen-horse-rules:
	scripts/gen_horse_rules.sh

test-horse: | $(BIN_DIR)
	$(CC) -g -O1 -fsanitize=address,undefined -Wall $(INCLUDES) \
		packages/simulation/horse_test.c $(filter-out apps/server/src/main.c,$(SERVER_SRC)) \
		-o $(HORSE_TEST) $(LIBS_M)
	./$(HORSE_TEST)
