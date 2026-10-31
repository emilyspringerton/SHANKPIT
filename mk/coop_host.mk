# coop_host.mk -- `--coop-host` (phase 3 of docs/GFD_UNIFICATION.md): the launch helper and its test.
#
# The test starts the real server through coop_host_start, so it depends on `server` being built. It
# runs from the repo root because the server writes its zone to var/maps/ relative to the cwd.
.PHONY: test-coop-host
test-coop-host: server | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Ipackages/simulation \
		packages/simulation/coop_host_test.c packages/simulation/coop_host.c \
		-o $(BIN_DIR)/coop_host_test -lm
	./$(BIN_DIR)/coop_host_test
