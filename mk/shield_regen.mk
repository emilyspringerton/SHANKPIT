# mk/shield_regen.mk -- a broken shield coming back, rules and host (2026-11-05).
#
# The numbers live in packages/sim/shield_rules.prn; packages/simulation/shield_rules.c is GENERATED
# from it, exactly like siren_rules.c/icecream_rules.c. The host-facing tick and the HUD countdown are
# packages/simulation/shield_regen.h -- header-only, so every host that includes physics.h/
# local_game.h runs the SAME per-tick rule instead of its own copy of it.
#
#   make regen-shield-rules   regenerate the C from the .prn
#   make check-shield-rules   fail if the generated C is stale vs the .prn
#   make test-shield-regen    the HOST half, headless: the real PlayerState, the real generated rule,
#                             the damage order, repeated/missed ticks, the spawn reset, a mod's curve
#                             installed through the hook, and the values a HUD reads
PARENA_DIR ?= ../PARENA
SHIELD_PRN := packages/sim/shield_rules.prn
SHIELD_C := packages/simulation/shield_rules.c
SHAMBLER_PRN := packages/sim/shambler_mod.prn
SHAMBLER_C := packages/simulation/shambler_mod.c

.PHONY: regen-shield-rules check-shield-rules regen-shambler-mod check-shambler-mod test-shield-regen

$(SHIELD_C): $(SHIELD_PRN)
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(SHIELD_PRN) -o $@

$(SHAMBLER_C): $(SHAMBLER_PRN)
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(SHAMBLER_PRN) -o $@

regen-shield-rules:
	@rm -f $(SHIELD_C)
	@$(MAKE) $(SHIELD_C)

regen-shambler-mod:
	@rm -f $(SHAMBLER_C)
	@$(MAKE) $(SHAMBLER_C)

check-shambler-mod:
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(SHAMBLER_PRN) -o /tmp/shambler_mod.regen.c >/dev/null
	cmp /tmp/shambler_mod.regen.c $(SHAMBLER_C)

check-shield-rules:
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(SHIELD_PRN) -o /tmp/shield_rules.regen.c >/dev/null
	cmp /tmp/shield_rules.regen.c $(SHIELD_C)

test-shield-regen: $(SHIELD_C) $(SHAMBLER_C) | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Ipackages/common -Ipackages/simulation \
		packages/simulation/shield_regen_test.c $(SHIELD_C) $(SHAMBLER_C) \
		-o $(BIN_DIR)/shield_regen_test $(LIBS_M)
	./$(BIN_DIR)/shield_regen_test
