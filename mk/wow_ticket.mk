# The IDUNA player ticket: layout, MAC, expiry, and the server's AUTH path. make test-wow-ticket
WOW_TICKET_SRC = packages/protocol/wow_ticket_test.c packages/protocol/wow_wire_rules.c packages/world/parena_runtime.c
WOW_TICKET_CFLAGS = -O1 -Wall -Wextra -Werror -Ipackages/protocol -Ipackages/ui -Ipackages/world

.PHONY: test-wow-ticket
test-wow-ticket: | $(BIN_DIR)
	$(CC) $(WOW_TICKET_CFLAGS) $(WOW_TICKET_SRC) -o $(BIN_DIR)/wow_ticket_test -lm
	./$(BIN_DIR)/wow_ticket_test
	$(CC) $(WOW_TICKET_CFLAGS) -g -fsanitize=address,undefined -fno-omit-frame-pointer $(WOW_TICKET_SRC) \
		-o $(BIN_DIR)/wow_ticket_test_asan -lm
	./$(BIN_DIR)/wow_ticket_test_asan
