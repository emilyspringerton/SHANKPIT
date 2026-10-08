# Basement shadow war (EMILY/BACKLOG.md SECTION 536 follow-up, "continue full game"):
# packages/simulation/shadow_war_rules.c is GENERATED from PARENA/stdlib/big_o/shadow_war.prn;
# shadow_war_host.c is the glue (army/battle data shapes, the N-tick resolution loop, a
# deterministic bot, Elo). Wired into LOBBY_SRC via mk/labstation.mk (LABST_WAR delegates here).
#   make test-shadow-war   regen-shadow-war   check-shadow-war
PARENA_DIR ?= ../PARENA
SHADOW_WAR_PRN := $(PARENA_DIR)/stdlib/big_o/shadow_war.prn
SHADOW_WAR_SRC := packages/simulation/shadow_war_test.c packages/simulation/shadow_war_host.c packages/simulation/shadow_war_rules.c packages/reflux/reflux_runtime.c packages/reflux/reflux_mod.c

.PHONY: test-shadow-war regen-shadow-war check-shadow-war
test-shadow-war: | $(BIN_DIR)
	gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-parameter -g -fsanitize=address,undefined \
		-Ipackages/common -Ipackages/reflux -include packages/reflux/reflux_mod_host.h \
		$(SHADOW_WAR_SRC) -o $(BIN_DIR)/shadow_war_test -lm
	./$(BIN_DIR)/shadow_war_test

regen-shadow-war:
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(SHADOW_WAR_PRN) -o packages/simulation/shadow_war_rules.c

check-shadow-war:
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(SHADOW_WAR_PRN) -o /tmp/shadow_war_rules.regen.c >/dev/null
	cmp /tmp/shadow_war_rules.regen.c packages/simulation/shadow_war_rules.c
