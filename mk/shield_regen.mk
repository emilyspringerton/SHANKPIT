# mk/shield_regen.mk -- a broken shield comes back, FFXI-style (four seconds vulnerable, then a
# one-second tick that starts at 1 point and ramps to 10; any hit restarts the four seconds).
#
# packages/simulation/shield_regen_rules.c is GENERATED from
# ../PARENA/stdlib/shankpit/shield_regen_rules.prn; packages/simulation/shield_regen.h is the host half.
#   make regen-shield-regen-rules   regenerate the .c from a sibling ../PARENA checkout
#   make check-shield-regen-rules   fail if the checked-in generated file is stale vs the .prn
#   make test-shield-regen          the headless host test, against the real generated rule
PARENA_DIR ?= ../PARENA
SHIELD_REGEN_PRN := $(PARENA_DIR)/stdlib/shankpit/shield_regen_rules.prn

.PHONY: regen-shield-regen-rules check-shield-regen-rules test-shield-regen

regen-shield-regen-rules:
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(SHIELD_REGEN_PRN) -o packages/simulation/shield_regen_rules.c

check-shield-regen-rules:
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(SHIELD_REGEN_PRN) -o /tmp/shield_regen_rules.regen.c >/dev/null
	cmp /tmp/shield_regen_rules.regen.c packages/simulation/shield_regen_rules.c

# test-shield-regen -- the 4 seconds, the first point at 4000 ms, the 1-2-3 ramp and its cap, a hit
# restarting the window, the ten-minute clamp, and the mod curve seam. protocol.h is included directly
# (the host header needs PlayerState), and the generated rule is linked, not mocked.
test-shield-regen: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Ipackages/common -Ipackages/simulation \
		packages/simulation/shield_regen_test.c packages/simulation/shield_regen_rules.c \
		-o $(BIN_DIR)/shield_regen_test $(LIBS_M)
	./$(BIN_DIR)/shield_regen_test
