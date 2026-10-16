# BIG_O -> SHANKPIT merge: the pure, header-first subsystems ported in (phone apps, party, say-chat,
# hoverboard, mission, avian live glue). `make test-bigo-merge` runs them all (plain assert harnesses).
BIGO_SIM := packages/simulation
test-bigo-merge: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra packages/common/phone_test.c -o $(BIN_DIR)/phone_test && $(BIN_DIR)/phone_test
	$(CC) -std=gnu99 -O1 -Wall -Wextra $(BIGO_SIM)/chat_say_test.c $(BIGO_SIM)/chat_rules.c -o $(BIN_DIR)/chat_say_test && $(BIN_DIR)/chat_say_test
	$(CC) -std=gnu99 -O1 -Wall -Wextra $(BIGO_SIM)/party_test.c $(BIGO_SIM)/party_rules.c -o $(BIN_DIR)/party_test && $(BIN_DIR)/party_test
	$(CC) -std=gnu99 -O1 -Wall -Wextra $(BIGO_SIM)/hoverboard_test.c $(BIGO_SIM)/hoverboard_rules.c -o $(BIN_DIR)/hoverboard_test -lm && $(BIN_DIR)/hoverboard_test
	$(CC) -std=gnu99 -O1 -Wall -Wextra $(BIGO_SIM)/mission_test.c $(BIGO_SIM)/mission.c $(BIGO_SIM)/witness_sim.c $(BIGO_SIM)/witness_rules.c -o $(BIN_DIR)/mission_test && $(BIN_DIR)/mission_test
	$(CC) -std=gnu99 -O1 -Wall -Wextra $(BIGO_SIM)/avian_live_test.c $(BIGO_SIM)/avian_values.c $(BIGO_SIM)/zombie_values.c $(BIGO_SIM)/witness_sim.c $(BIGO_SIM)/witness_rules.c $(BIGO_SIM)/ai_brain_rules.c -o $(BIN_DIR)/avian_live_test -lm && $(BIN_DIR)/avian_live_test
