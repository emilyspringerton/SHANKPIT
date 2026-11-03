# Vehicle salvage (SALVAGE-1): packages/simulation/vehicle_salvage_rules.c is GENERATED from
# PARENA/stdlib/shankpit/vehicle_salvage_rules.prn; vehicle_salvage.h keeps the hull-conserving split.
#   make regen-vehicle-salvage-rules   regenerate the .c from a sibling ../PARENA checkout
#   make check-vehicle-salvage-rules   fail if the checked-in generated file is stale vs the .prn
#   make test-vehicle-salvage          headless: parts sum to the hull, damage is conserved, the
#                                      handling stack and the salvage table are hand-derived
# The generator prefers PARENA's .parena-bootstrap, so this works on a box with no SDL2 (where the
# full `parena` binary cannot link).
PARENA_DIR ?= ../PARENA
SALVAGE_REGEN := /tmp/vehicle_salvage_rules.regen.c

.PHONY: regen-vehicle-salvage-rules check-vehicle-salvage-rules test-vehicle-salvage

regen-vehicle-salvage-rules:
	PARENA_DIR=$(PARENA_DIR) scripts/gen_vehicle_salvage_rules.sh

check-vehicle-salvage-rules:
	PARENA_DIR=$(PARENA_DIR) scripts/gen_vehicle_salvage_rules.sh $(SALVAGE_REGEN)
	cmp $(SALVAGE_REGEN) packages/simulation/vehicle_salvage_rules.c

test-vehicle-salvage: | $(BIN_DIR)
	$(CC) -O1 -Wall -Wextra -Werror -Ipackages/simulation -DSHANKPIT_VEHICLE_SALVAGE_PARENA \
		packages/simulation/vehicle_salvage_test.c packages/simulation/vehicle_salvage_rules.c \
		packages/simulation/vehicle_burn_rules.c \
		-o $(BIN_DIR)/vehicle_salvage_test $(LIBS_M)
	./$(BIN_DIR)/vehicle_salvage_test
	$(CC) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Werror \
		-Ipackages/simulation -DSHANKPIT_VEHICLE_SALVAGE_PARENA \
		packages/simulation/vehicle_salvage_test.c packages/simulation/vehicle_salvage_rules.c \
		packages/simulation/vehicle_burn_rules.c \
		-o $(BIN_DIR)/vehicle_salvage_test_asan $(LIBS_M)
	./$(BIN_DIR)/vehicle_salvage_test_asan
