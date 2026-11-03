# The macro wire over a REAL UDP socket, against the REAL server binary.
#   make test-wow-e2e [E2E_PORT=6979]
# Headless: no display, no SDL, no IDUNA (SHANKPIT_BAKED_LEVELS=1). The server is a private instance on
# E2E_PORT with --beacon-port 0, so it never collides with or advertises beside a real service.
E2E_PORT      ?= 6979
WOW_E2E_SRC    = packages/protocol/wow_e2e_test.c packages/protocol/wow_wire_rules.c packages/world/parena_runtime.c
WOW_E2E_CFLAGS = -O1 -Wall -Wextra -Werror -Ipackages/protocol -Ipackages/ui -Ipackages/world -Ipackages/common

.PHONY: test-wow-e2e
test-wow-e2e: $(SERVER_BIN) | $(BIN_DIR)
	$(CC) $(WOW_E2E_CFLAGS) $(WOW_E2E_SRC) -o $(BIN_DIR)/wow_e2e_test -lm
	./$(BIN_DIR)/wow_e2e_test $(E2E_PORT)
