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
