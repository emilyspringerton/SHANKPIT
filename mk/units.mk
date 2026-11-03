# The unit affordance layer: generated PARENA arithmetic, checked against looked-up factors.
#   make regen-units    regenerate units_rules.c from PARENA
#   make check-units    fail if the checked-in C is stale
#   make test-units     plain + ASan/UBSan
UNITS_SRC    = packages/common/units_test.c packages/common/units_rules.c packages/world/parena_runtime.c
UNITS_CFLAGS = -O1 -Wall -Wextra -Werror -Ipackages/common -Ipackages/world

.PHONY: regen-units check-units test-units

regen-units:
	scripts/gen_units.sh

check-units:
	@tmp=$$(mktemp); scripts/gen_units.sh $$tmp >/dev/null; \
	 if diff -u packages/common/units_rules.c $$tmp; then rm -f $$tmp; \
	 else rm -f $$tmp; echo "check-units: packages/common/units_rules.c is stale"; exit 1; fi

test-units: | $(BIN_DIR)
	$(CC) $(UNITS_CFLAGS) $(UNITS_SRC) -o $(BIN_DIR)/units_test -lm
	./$(BIN_DIR)/units_test
	$(CC) $(UNITS_CFLAGS) -g -fsanitize=address,undefined -fno-omit-frame-pointer $(UNITS_SRC) \
		-o $(BIN_DIR)/units_test_asan -lm
	./$(BIN_DIR)/units_test_asan
