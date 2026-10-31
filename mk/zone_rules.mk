# mk/zone_rules.mk -- shared procedural zones (2026-10-31).
#
# Founder: "GFD server ... like diablo where we can travel to a map and do it as a dungeon it can be
# procedurally generated ... the first person to enter it it generates and then everyone else enters the
# same one ... there should be a grace period of 15 minutes". The lifecycle and layout numbers live in
# PARENA; packages/simulation/proc_dungeon.h builds the level and zone_registry.h runs the clock.

.PHONY: regen-zone-rules test-proc-dungeon

regen-zone-rules:
	scripts/gen_zone_rules.sh

# test-proc-dungeon -- determinism and the shared-zone lifecycle: two clients generating from one seed
# write byte-identical levels, a saved dungeon round-trips through the real parser, the gate opens on its
# relay's delay, a second entrant joins the same layout, and the zone survives fifteen empty minutes (and
# only then regenerates from a new seed).
test-proc-dungeon: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Ipackages/simulation -Ipackages/world \
		packages/simulation/proc_dungeon_test.c packages/simulation/zone_rules.c \
		packages/simulation/widget_rules.c \
		-o $(BIN_DIR)/proc_dungeon_test $(LIBS_M)
	./$(BIN_DIR)/proc_dungeon_test
