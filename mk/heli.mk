# Programmable heli (card #542): packages/simulation/heli_rules.c is GENERATED from
# PARENA/stdlib/shankpit/heli_rules.prn; heli_rules_host.h installs it into net_sim.h.
#   make regen-heli-rules   regenerate heli_rules.c from a sibling ../PARENA checkout
#   make check-heli-rules   fail if the checked-in generated file is stale vs the .prn
PARENA_DIR ?= ../PARENA
HELI_PRN := $(PARENA_DIR)/stdlib/shankpit/heli_rules.prn

.PHONY: regen-heli-rules check-heli-rules
regen-heli-rules:
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(HELI_PRN) -o packages/simulation/heli_rules.c

check-heli-rules:
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(HELI_PRN) -o /tmp/heli_rules.regen.c >/dev/null
	cmp /tmp/heli_rules.regen.c packages/simulation/heli_rules.c
