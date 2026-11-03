# The macro wire over a REAL UDP socket, against the REAL server binary.
#   make test-wow-e2e [E2E_PORT=6979]
# Headless: no display, no SDL, no IDUNA (SHANKPIT_BAKED_LEVELS=1). The server is a private instance on
# E2E_PORT with --beacon-port 0, so it never collides with or advertises beside a real service.
# E2E_PORT=0 means "pick a free UDP port", which is the default: a dev server on 6979 must not make the
# wire look broken. Pass an explicit port only when reproducing a specific failure.
#
# Three phases on three ports, because each needs a differently-behaved server:
#   <port>  0    the happy path (every ACK arrives)
#   <port2> 2    the server swallows 2 CAST ACKs: only the client's real resend can recover
#   <port3> 99   every CAST ACK is lost: the client must abandon exactly once, naming the cast
E2E_PORT      ?= 0
E2E_PORT2     ?= 0
E2E_PORT3     ?= 0
WOW_E2E_SRC    = packages/protocol/wow_e2e_test.c packages/protocol/wow_wire_rules.c packages/world/parena_runtime.c
WOW_E2E_CFLAGS = -O1 -Wall -Wextra -Werror -Ipackages/protocol -Ipackages/ui -Ipackages/world -Ipackages/common

.PHONY: test-wow-e2e
test-wow-e2e: $(SERVER_BIN) | $(BIN_DIR)
	$(CC) $(WOW_E2E_CFLAGS) $(WOW_E2E_SRC) -o $(BIN_DIR)/wow_e2e_test -lm
	./$(BIN_DIR)/wow_e2e_test $(E2E_PORT) 0
	./$(BIN_DIR)/wow_e2e_test $(E2E_PORT2) 2
	./$(BIN_DIR)/wow_e2e_test $(E2E_PORT3) 99
