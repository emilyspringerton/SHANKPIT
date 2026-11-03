# WOW WIRE: the macro/mod surface as bytes. Targets, cast intents, macro-as-bytecode, invoke/ack, and
# the mod registry that feeds it. Numbers come from PARENA/stdlib/shankpit/wow_wire.prn.
#   make regen-wow-wire-rules   regenerate the checked-in C from PARENA
#   make check-wow-wire-rules   fail if the checked-in C is stale
#   make test-wow-wire          headless, plain + ASan/UBSan
WOW_WIRE_SRC = packages/protocol/wow_wire_test.c packages/protocol/wow_wire_rules.c packages/world/parena_runtime.c
WOW_WIRE_CFLAGS = -O1 -Wall -Wextra -Werror -Ipackages/protocol -Ipackages/ui -Ipackages/world

.PHONY: regen-wow-wire-rules check-wow-wire-rules test-wow-wire

regen-wow-wire-rules:
	scripts/gen_wow_wire_rules.sh

check-wow-wire-rules:
	@tmp=$$(mktemp); scripts/gen_wow_wire_rules.sh $$tmp >/dev/null; \
	 if diff -u packages/protocol/wow_wire_rules.c $$tmp; then rm -f $$tmp; \
	 else rm -f $$tmp; echo "check-wow-wire-rules: packages/protocol/wow_wire_rules.c is stale"; exit 1; fi

test-wow-wire: | $(BIN_DIR)
	$(CC) $(WOW_WIRE_CFLAGS) $(WOW_WIRE_SRC) -o $(BIN_DIR)/wow_wire_test -lm
	./$(BIN_DIR)/wow_wire_test
	$(CC) $(WOW_WIRE_CFLAGS) -g -fsanitize=address,undefined -fno-omit-frame-pointer $(WOW_WIRE_SRC) \
		-o $(BIN_DIR)/wow_wire_test_asan -lm
	./$(BIN_DIR)/wow_wire_test_asan
