# mk/zone_rules.mk -- shared procedural zones (2026-10-31).
#
# Founder: "GFD server ... like diablo where we can travel to a map and do it as a dungeon it can be
# procedurally generated ... the first person to enter it it generates and then everyone else enters the
# same one ... there should be a grace period of 15 minutes". The lifecycle and layout numbers live in
# PARENA; packages/simulation/proc_dungeon.h builds the level and zone_registry.h runs the clock.

.PHONY: regen-zone-rules test-proc-dungeon test-zone-sync

regen-zone-rules:
	scripts/gen_zone_rules.sh

# test-proc-dungeon -- determinism and the shared-zone lifecycle: two clients generating from one seed
# write byte-identical levels, a saved dungeon round-trips through the real parser, the gate opens on its
# relay's delay, a second entrant joins the same layout, and the zone survives fifteen empty minutes (and
# only then regenerates from a new seed).
test-proc-dungeon: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Ipackages/simulation -Ipackages/world -Ipackages/common \
		packages/simulation/proc_dungeon_test.c packages/simulation/zone_rules.c \
		packages/simulation/widget_rules.c packages/common/units_rules.c \
		-o $(BIN_DIR)/proc_dungeon_test $(LIBS_M)
	./$(BIN_DIR)/proc_dungeon_test

# test-zone-sync -- the zone WIRE contract: a short datagram, the wrong type and an unterminated id are
# all refused (the id becomes a filename), and a repeated 1 Hz packet must not reload the world while a
# new seed or a new zone must.
test-zone-sync: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Ipackages/simulation -Ipackages/common \
		packages/simulation/zone_sync_test.c -o $(BIN_DIR)/zone_sync_test $(LIBS_M)
	./$(BIN_DIR)/zone_sync_test
