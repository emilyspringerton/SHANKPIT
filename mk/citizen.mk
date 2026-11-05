# mk/citizen.mk -- the MODE_ZOMBIES citizen body: PARENA citizen_actor_rules.prn, the action it
# chooses, and the player-physics inputs it writes.
#
# A citizen is a real PlayerState that already runs the human movement path; what it never did was
# write in_jump / crouching / in_shoot / current_weapon. This target proves the chain headlessly:
# the PARENA table matches its built-in C mirror field for field, the heuristic/policy blend picks
# the action the design says it should for a spread of situations, a jump actually leaves the
# ground and obeys its cooldown, a slide holds its own duration and crouches the body, a slide-jump
# leaves with more vertical speed than a standing jump, a citizen with a threat in range draws a
# weapon (and holsters it when the threat is gone), and each of those dispatches its REFLUX action.

CITIZEN_ACTOR_TEST := $(BIN_DIR)/citizen_actor_test

.PHONY: test-citizen-actor regen-citizen-actor-rules
regen-citizen-actor-rules:
	scripts/gen_citizen_actor_rules.sh

test-citizen-actor: | $(BIN_DIR)
	$(CC) -g -O1 -fsanitize=address,undefined -Wall $(INCLUDES) \
		packages/simulation/citizen_actor_test.c $(filter-out apps/server/src/main.c,$(SERVER_SRC)) \
		-o $(CITIZEN_ACTOR_TEST) $(LIBS_M)
	./$(CITIZEN_ACTOR_TEST)

# test-citizen-wire -- the SAME citizen, but through a second PROCESS and a real UDP socket.
#
# test-citizen-actor proves the family is collected, batched and re-dispatched, but it does so in one
# address space, which by construction cannot catch a wire bug: a struct size the two sides disagree
# about, a field written into the wrong byte, a packet the server never actually sends. This boots
# the real bin/shank_server in MODE_ZOMBIES (--zombies) with SHANKPIT_TEST_ZOMBIE_PAIR=1, which plants
# one citizen and one hunting zombie six metres apart, and runs apps/tests/citizen_wire_client.c
# against it. The citizen fights on its first tick, so the events are certain rather than likely.
CITIZEN_WIRE_PORT ?= 6980
CITIZEN_WIRE_CLIENT := $(BIN_DIR)/citizen_wire_client

$(CITIZEN_WIRE_CLIENT): apps/tests/citizen_wire_client.c packages/reflux/reflux_runtime.c | $(BIN_DIR)
	$(CC) -O1 -Wall -Wextra $(INCLUDES) $< packages/reflux/reflux_runtime.c -o $@ $(LIBS_M)

.PHONY: test-citizen-wire
# `server` is a prerequisite so the binary under test is always current.
test-citizen-wire: server $(CITIZEN_WIRE_CLIENT)
	@echo "🔌 citizen wire test: real bin/shank_server (--zombies) + real UDP client on port $(CITIZEN_WIRE_PORT)"
	@set -e; \
	SHANKPIT_BAKED_LEVELS=1 SHANKPIT_TEST_ZOMBIE_PAIR=1 ./$(SERVER_BIN) --zombies --port $(CITIZEN_WIRE_PORT) \
		> /tmp/citizen_wire_server.log 2>&1 & \
	SRV=$$!; \
	trap 'kill $$SRV 2>/dev/null || true' EXIT; \
	sleep 0.5; \
	./$(CITIZEN_WIRE_CLIENT) 127.0.0.1 $(CITIZEN_WIRE_PORT); \
	kill $$SRV 2>/dev/null || true
