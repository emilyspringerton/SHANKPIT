# anim_clipset.mk -- the Mixamo half finished: a binder that produces the thirteen-name bundle, and a
# runtime selector that loads and samples it (packages/simulation/anim_clipset.h).
#
# Both halves are tested against the repo's REAL assets: the binder on Mike/Leela/Stan/George .gband
# files, the selector through GOLDENBAND's own loader. Nothing here needs a GPU or a display.
.PHONY: test-anim-clipset test-mixamo-clipset test-clipset-import bind-mike-clips

# bind-mike-clips -- a real, inspectable binding of the robot rigs to the contract, into var/ (not
# committed): the honest place for a bundle an artist is still populating.
bind-mike-clips:
	python3 tools/mixamo_clipset.py --assets assets/goldenband --auto --rig Mike --allow-partial \
		--out var/clipsets/mike

test-mixamo-clipset:
	python3 tools/test_mixamo_clipset.py

# The editor's side of the same binder: local NOCK's /api/clipsets, over real HTTP against a
# throwaway git repo, so "import and commit" is verified as a workflow and not just a function.
test-clipset-import:
	python3 tools/test_clipset_import.py

# The selector needs gband.c/gseq.c/gskel.c at runtime; sha256.c is only needed when the build's
# gband.c pulls it in, so it is included when present rather than assumed.
GB_LIB_SRC := packages/goldenband/gband.c packages/goldenband/gseq.c packages/goldenband/gskel.c \
	$(wildcard packages/goldenband/sha256.c) packages/simulation/anim_rules.c

test-anim-clipset: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Ipackages/common -Ipackages/simulation -Ipackages/goldenband \
		packages/simulation/anim_clipset_test.c $(GB_LIB_SRC) -o $(BIN_DIR)/anim_clipset_test $(LIBS_M)
	./$(BIN_DIR)/anim_clipset_test
