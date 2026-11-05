# mk/shield_regen.mk -- a broken shield coming back, rules and host (2026-11-05).
#
# The numbers live in packages/sim/shield_rules.prn; packages/simulation/shield_rules.c is GENERATED
# from it, exactly like siren_rules.c/icecream_rules.c. The host-facing tick and the HUD countdown are
# packages/simulation/shield_regen.h -- header-only, so every host that includes physics.h/
# local_game.h runs the SAME per-tick rule instead of its own copy of it.
#
#   make regen-shield-rules   regenerate the C from the .prn
#   make check-shield-rules   fail if the generated C is stale vs the .prn
PARENA_DIR ?= ../PARENA
SHIELD_PRN := packages/sim/shield_rules.prn

.PHONY: regen-shield-rules check-shield-rules

regen-shield-rules:
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(SHIELD_PRN) -o packages/simulation/shield_rules.c

check-shield-rules:
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(SHIELD_PRN) -o /tmp/shield_rules.regen.c >/dev/null
	cmp /tmp/shield_rules.regen.c packages/simulation/shield_rules.c
