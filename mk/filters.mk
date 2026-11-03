# STREAM FILTERS: packages/render/filter_rules.c is GENERATED from
# PARENA/stdlib/shankpit/filter_rules.prn; stream_filters.h keeps the LUT build and the pixel passes.
#   make regen-filter-rules   regenerate the .c from a sibling ../PARENA checkout
#   make check-filter-rules   fail if the checked-in generated file is stale vs the .prn
#   make test-stream-filters  headless: neutral is bit-identical, levels/gamma/kernel/vignette/grain
#                             are hand-derived, and the PARENA preset table matches the header defaults
FILTER_REGEN := /tmp/filter_rules.regen.c

.PHONY: regen-filter-rules check-filter-rules test-stream-filters

regen-filter-rules:
	PARENA_DIR=$(PARENA_DIR) scripts/gen_filter_rules.sh

check-filter-rules:
	PARENA_DIR=$(PARENA_DIR) scripts/gen_filter_rules.sh $(FILTER_REGEN)
	cmp $(FILTER_REGEN) packages/render/filter_rules.c

test-stream-filters: | $(BIN_DIR)
	$(CC) -O1 -Wall -Wextra -Werror -Ipackages/render -Ipackages/world packages/render/stream_filters_test.c \
		packages/render/filter_rules.c packages/world/parena_runtime.c -o $(BIN_DIR)/stream_filters_test $(LIBS_M)
	./$(BIN_DIR)/stream_filters_test
	$(CC) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Werror \
		-Ipackages/render -Ipackages/world packages/render/stream_filters_test.c \
		packages/render/filter_rules.c packages/world/parena_runtime.c \
		-o $(BIN_DIR)/stream_filters_test_asan $(LIBS_M)
	./$(BIN_DIR)/stream_filters_test_asan
