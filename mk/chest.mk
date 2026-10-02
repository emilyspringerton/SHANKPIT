# Programmable chest (card #528): packages/simulation/chest_rules.c is GENERATED from
# PARENA/stdlib/shankpit/chest_rules.prn; chests.c calls it.
#   make regen-chest-rules   regenerate chest_rules.c from a sibling ../PARENA checkout
#   make check-chest-rules   fail if the checked-in generated file is stale vs the .prn
PARENA_DIR ?= ../PARENA
CHEST_PRN := $(PARENA_DIR)/stdlib/shankpit/chest_rules.prn

.PHONY: regen-chest-rules check-chest-rules
regen-chest-rules:
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(CHEST_PRN) -o packages/simulation/chest_rules.c

check-chest-rules:
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(CHEST_PRN) -o /tmp/chest_rules.regen.c >/dev/null
	cmp /tmp/chest_rules.regen.c packages/simulation/chest_rules.c

# Headless test: pool, ray/blast geometry, PARENA-driven HP and drops.
.PHONY: test-chests
test-chests:
	gcc -Wall -Wextra -Werror -I. -Ipackages/simulation -I$(PARENA_DIR)/runtime -o /tmp/chests_test packages/simulation/chests_test.c packages/simulation/chests.c packages/simulation/gun_items.c packages/simulation/chest_rules.c $(PARENA_DIR)/runtime/parena_runtime.c -lm
	/tmp/chests_test
