# coop_host.mk -- `--coop-host` (phase 3 of docs/GFD_UNIFICATION.md): the launch helper and its test.
#
# The test starts the real server through coop_host_start, so it depends on `server` being built. It
# runs from the repo root because the server writes its zone to var/maps/ relative to the cwd.
.PHONY: test-coop-host test-zone-beacon
test-coop-host: server | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Ipackages/simulation \
		packages/simulation/coop_host_test.c packages/simulation/coop_host.c \
		-o $(BIN_DIR)/coop_host_test -lm
	./$(BIN_DIR)/coop_host_test

# The LAN beacon: the fixed wire form and its refusals, then a REAL announce/find with a server on
# its own game and beacon ports. The live half is what proves the advertised port is the one that
# answers -- a beacon pointing at 6969 while the world runs elsewhere helps nobody.
test-zone-beacon: server | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Ipackages/simulation \
		packages/simulation/zone_beacon_test.c packages/simulation/zone_beacon.c packages/simulation/coop_host.c \
		-o $(BIN_DIR)/zone_beacon_test -lm
	./$(BIN_DIR)/zone_beacon_test
