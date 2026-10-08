# BIG_O LAB stations (SECTION 592, card #508): packages/simulation/lab_station_rules.c is GENERATED from
# PARENA/stdlib/big_o/lab_station_rules.prn; lab_station_host.c is the glue onto the phone state.
# LABST_WAR (SECTION 536 follow-up, "continue full game") added a 7th kind that delegates to the
# separate shadow_war system (shadow_war_host.c + PARENA-generated shadow_war_rules.c, from
# PARENA/stdlib/big_o/shadow_war.prn) -- pulled in here only because lab_station_host.c now calls it.
#   make test-lab-station   regen-lab-station   check-lab-station
PARENA_DIR ?= ../PARENA
LABST_PRN := $(PARENA_DIR)/stdlib/big_o/lab_station_rules.prn
LABST_SRC := packages/simulation/lab_station_test.c packages/simulation/lab_station_host.c packages/simulation/lab_station_rules.c packages/simulation/shadow_war_host.c packages/simulation/shadow_war_rules.c packages/world/parena_runtime.c packages/reflux/reflux_runtime.c packages/reflux/reflux_mod.c

.PHONY: test-lab-station regen-lab-station check-lab-station
test-lab-station:
	gcc -Wall -Wextra -Werror -Wno-unused-parameter -g -fsanitize=address,undefined -Ipackages/world -Ipackages/reflux \
		-include packages/reflux/reflux_mod_host.h $(LABST_SRC) -o /tmp/lab_station_test -lm
	/tmp/lab_station_test

regen-lab-station:
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(LABST_PRN) -o packages/simulation/lab_station_rules.c

check-lab-station:
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(LABST_PRN) -o /tmp/lab_station_rules.regen.c >/dev/null
	cmp /tmp/lab_station_rules.regen.c packages/simulation/lab_station_rules.c
