# Programmable buggy (card #468): packages/simulation/buggy_rules.c is GENERATED from
# PARENA/stdlib/shankpit/buggy_rules.prn; buggy_rules_host.h installs it into physics.h.
#   make regen-buggy-rules   regenerate buggy_rules.c from a sibling ../PARENA checkout
#   make check-buggy-rules   fail if the checked-in generated file is stale vs the .prn
PARENA_DIR ?= ../PARENA
BUGGY_PRN := $(PARENA_DIR)/stdlib/shankpit/buggy_rules.prn

.PHONY: regen-buggy-rules check-buggy-rules
regen-buggy-rules:
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(BUGGY_PRN) -o packages/simulation/buggy_rules.c

check-buggy-rules:
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(BUGGY_PRN) -o /tmp/buggy_rules.regen.c >/dev/null
	cmp /tmp/buggy_rules.regen.c packages/simulation/buggy_rules.c
