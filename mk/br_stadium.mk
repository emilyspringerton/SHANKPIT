# mk/br_stadium.mk -- Bedrock Racers' stadium in the native client (2026-10-31).
#
# The shape lives in PARENA/stdlib/bedrockracers/web/br_stadium.prn so the wasm game and this client
# cannot disagree about the walls. These targets regenerate the shared numbers and test the native
# SDF/mesh layer without a GL context.

.PHONY: regen-br-stadium test-br-stadium

regen-br-stadium:
	scripts/gen_br_stadium.sh

# test-br-stadium -- the surface the car is clamped to and the mesh the player sees come from one set
# of numbers: every mesh vertex must lie ON the SDF and every stored normal must BE the gradient.
test-br-stadium: | $(BIN_DIR)
	$(CC) -std=c99 -O1 -Wall -Wextra -Werror -Ipackages/simulation -Ipackages/common \
		packages/simulation/br_stadium_test.c packages/simulation/br_stadium.c \
		-o $(BIN_DIR)/br_stadium_test $(LIBS_M)
	./$(BIN_DIR)/br_stadium_test
