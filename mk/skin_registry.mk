# skin_registry.mk -- imported character sets as real skins (NOCK import -> a body you can draw).
#
# The test runs against the committed set (assets/goldenband/clipsets/mike) and the real Mike
# gskel/gmesh: discovery, mesh binding from skin.json, loading as a kit, and vertices actually being
# skinned through a counting callback (no GL context needed).
.PHONY: test-skin-registry bind-mike-clips

bind-mike-clips:
	python3 tools/mixamo_clipset.py --assets assets/goldenband --mesh Mike \
		--map tools/clipsets/mike.json --out assets/goldenband/clipsets/mike

test-skin-registry: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Ipackages/common -Ipackages/simulation -Ipackages/goldenband \
		packages/simulation/skin_registry_test.c packages/goldenband/gband_skel_npc.c \
		packages/goldenband/gband.c packages/goldenband/gseq.c packages/goldenband/gskel.c \
		packages/goldenband/gmesh.c packages/goldenband/gpose.c $(wildcard packages/goldenband/sha256.c) \
		-o $(BIN_DIR)/skin_registry_test $(LIBS_M)
	./$(BIN_DIR)/skin_registry_test
