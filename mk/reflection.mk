# mk/reflection.mk -- lightweight metal/glass reflection (2026-10-31).
#
# The GL plumbing (packages/render/reflection.h: sphere-map texgen, one procedural 64x64 map, one
# additive draw per reflective box) needs a GL context and is compiled by `make check-windows`. What
# CAN be checked on a headless box is the map's pixels and PARENA's per-material strengths, which is
# what this target does.

.PHONY: test-reflection

test-reflection: | $(BIN_DIR)
	$(CC) -std=c99 -O1 -Wall -Wextra -Werror -Ipackages/render -Ipackages/simulation \
		packages/render/reflection_map_test.c packages/simulation/material_rules.c \
		-o $(BIN_DIR)/reflection_map_test $(LIBS_M)
	./$(BIN_DIR)/reflection_map_test
