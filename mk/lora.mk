# LORA WIRE: packages/protocol/lora_rules.c is GENERATED from PARENA/stdlib/shankpit/lora_rules.prn;
# lora_wire.h owns the 12-byte intent, the delta frame and the tick resolver.
#   make regen-lora-rules   regenerate the .c from a sibling ../PARENA checkout
#   make check-lora-rules   fail if the checked-in generated file is stale vs the .prn
#   make test-lora-wire     headless: round trips, refusals, the 237-byte frame, determinism
LORA_REGEN := /tmp/lora_rules.regen.c

.PHONY: regen-lora-rules check-lora-rules test-lora-wire

regen-lora-rules:
	PARENA_DIR=$(PARENA_DIR) scripts/gen_lora_rules.sh

check-lora-rules:
	PARENA_DIR=$(PARENA_DIR) scripts/gen_lora_rules.sh $(LORA_REGEN)
	cmp $(LORA_REGEN) packages/protocol/lora_rules.c

test-lora-wire: | $(BIN_DIR)
	$(CC) -O1 -Wall -Wextra -Werror -Ipackages/protocol -Ipackages/world \
		packages/protocol/lora_wire_test.c packages/protocol/lora_rules.c \
		packages/world/parena_runtime.c -o $(BIN_DIR)/lora_wire_test $(LIBS_M)
	./$(BIN_DIR)/lora_wire_test
	$(CC) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Werror \
		-Ipackages/protocol -Ipackages/world packages/protocol/lora_wire_test.c \
		packages/protocol/lora_rules.c packages/world/parena_runtime.c \
		-o $(BIN_DIR)/lora_wire_test_asan $(LIBS_M)
	./$(BIN_DIR)/lora_wire_test_asan
