# mk/horse.mk -- the tölt mount: PARENA horse_rules.prn, the level's horse spawns, and the ride.
#
# The horse is an actor, not scenery: level JSON "horse_spawns" (a wall named horse_spawn /
# horse_spawn_<deg>, exported by IDUNA, parsed by level_boxes.h) becomes a real HorseState in
# scene_load, and horse_tick_all rides it with the numbers from PARENA. This target proves the whole
# chain headlessly: the parser, the spawn, tölt draining stamina, the jump (a hop, not flight), the
# carrot that refills the bar, fatigue slowing a blown horse, and a scene load that clears stale
# horses and places the authored ones.

HORSE_TEST := $(BIN_DIR)/horse_test

.PHONY: test-horse regen-horse-rules regen-horse-goals regen-horse-poop-rules
regen-horse-rules:
	scripts/gen_horse_rules.sh

# What a loose horse decides (graze / wander / idle / home / flee) is its own PARENA mod:
# PARENA/stdlib/shankpit/horse_goals.prn -> packages/simulation/horse_goals.c.
regen-horse-goals:
	scripts/gen_horse_goals.sh

# What comes out the other end (when a grazing horse drops, where behind the tail, how long it
# lasts, how many the paddock holds): PARENA/stdlib/shankpit/horse_poop_rules.prn ->
# packages/simulation/horse_poop_rules.c.
regen-horse-poop-rules:
	scripts/gen_horse_poop_rules.sh

test-horse: | $(BIN_DIR)
	$(CC) -g -O1 -fsanitize=address,undefined -Wall $(INCLUDES) \
		packages/simulation/horse_test.c $(filter-out apps/server/src/main.c,$(SERVER_SRC)) \
		-o $(HORSE_TEST) $(LIBS_M)
	./$(HORSE_TEST)

# test-horse-wire -- the SAME horse, but through a second PROCESS and a real UDP socket.
#
# Every other horse check lives in one address space, which by construction cannot catch the bugs a
# wire has: a struct whose size the two sides disagree about, a field written into the wrong byte, a
# packet the server never sends, a mount the server refuses for a reason the local path does not have.
# This boots the real bin/shank_server on a tiny authored level (var/test/horse_wire_level.json: one
# spawner, one horse pad, both at the same spot) and runs apps/tests/horse_wire_client.c against it:
# connect, receive the unicorn through PACKET_HORSE_STATE, mount, watch the server's own state seat
# this client, send SET_GOAL, and receive BOTH the mount and the goal change back through
# PACKET_HORSE_EVENTS (the REFLUX relay). This is the deepest evidence available on a box with no
# display -- it is not a human playthrough, but it is the real binary over the real wire.
HORSE_WIRE_PORT ?= 6979
HORSE_WIRE_CLIENT := $(BIN_DIR)/horse_wire_client

$(HORSE_WIRE_CLIENT): apps/tests/horse_wire_client.c | $(BIN_DIR)
	$(CC) -O1 -Wall -Wextra $(INCLUDES) $< -o $@ $(LIBS_M)

.PHONY: test-horse-wire
# `server` is a prerequisite so the binary under test is always current -- a stale bin/ is exactly how
# a wire test passes while the change it was written for is not in it.
test-horse-wire: server $(HORSE_WIRE_CLIENT)
	@echo "🔌 horse wire test: real bin/shank_server + real UDP client on port $(HORSE_WIRE_PORT)"
	@set -e; \
	SHANKPIT_BAKED_LEVELS=1 ./$(SERVER_BIN) --level var/test/horse_wire_level.json --port $(HORSE_WIRE_PORT) \
		> /tmp/horse_wire_server.log 2>&1 & \
	SRV=$$!; \
	trap 'kill $$SRV 2>/dev/null || true' EXIT; \
	sleep 1; \
	./$(HORSE_WIRE_CLIENT) 127.0.0.1 $(HORSE_WIRE_PORT); \
	kill $$SRV 2>/dev/null || true
