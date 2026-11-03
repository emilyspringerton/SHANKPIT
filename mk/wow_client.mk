# The client half of the macro wire, driven into the real server. make test-wow-client
WOW_CLIENT_SRC = packages/protocol/wow_client_test.c packages/protocol/wow_wire_rules.c packages/world/parena_runtime.c
WOW_CLIENT_CFLAGS = -O1 -Wall -Wextra -Werror -Ipackages/protocol -Ipackages/ui -Ipackages/world

.PHONY: test-wow-client
test-wow-client: | $(BIN_DIR)
	$(CC) $(WOW_CLIENT_CFLAGS) $(WOW_CLIENT_SRC) -o $(BIN_DIR)/wow_client_test -lm
	./$(BIN_DIR)/wow_client_test
	$(CC) $(WOW_CLIENT_CFLAGS) -g -fsanitize=address,undefined -fno-omit-frame-pointer $(WOW_CLIENT_SRC) \
		-o $(BIN_DIR)/wow_client_test_asan -lm
	./$(BIN_DIR)/wow_client_test_asan
