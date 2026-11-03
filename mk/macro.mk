# MACROS: packages/ui/macro.h is the FORMAT (parse shankpit_macros.cfg, find a bind, split a body on
# ';', write it back); what a command DOES lives in the client. No Lua, no evaluator, no allocation.
#   make test-macros   headless: comments, malformed lines, rebind replace, ';' split, roundtrip, bounds
.PHONY: test-macros

test-macros: | $(BIN_DIR)
	$(CC) -O1 -Wall -Wextra -Werror -Ipackages/ui packages/ui/macro_test.c -o $(BIN_DIR)/macro_test
	./$(BIN_DIR)/macro_test
