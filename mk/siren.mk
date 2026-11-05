# mk/siren.mk -- emergency sirens, their rules and their one voice (2026-11-05).
#
# The numbers live in PARENA/stdlib/shankpit/siren_rules.prn; packages/simulation/siren_rules.c is
# GENERATED from it. The DSP that turns millihertz into samples is packages/audio/audio.c's siren voice,
# so the same rules can drive a browser host without shipping this engine's waveform.
#
#   make regen-siren-rules   regenerate the C from a sibling ../PARENA checkout
#   make check-siren-rules   fail if the checked-in generated file is stale vs the .prn
#   make test-siren-rules    the curves, headless (no device)
#   make test-police-patrol  the automated cruiser's steering, headless
PARENA_DIR ?= ../PARENA
SIREN_PRN := $(PARENA_DIR)/stdlib/shankpit/siren_rules.prn

.PHONY: regen-siren-rules check-siren-rules test-siren-rules test-police-patrol test-siren-dsp

regen-siren-rules:
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(SIREN_PRN) -o packages/simulation/siren_rules.c

check-siren-rules:
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(SIREN_PRN) -o /tmp/siren_rules.regen.c >/dev/null
	cmp /tmp/siren_rules.regen.c packages/simulation/siren_rules.c

# test-siren-dsp -- the oscillator rendered OFFLINE. No device, no SDL, no lock: pitch by zero crossings,
# pan by per-channel energy, and a hard ceiling no sample may cross. This is the closest this box can get
# to hearing a siren, and it is what the "nothing has been heard" limit was asking for.
test-siren-dsp: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Ipackages/audio -Ipackages/simulation \
		packages/audio/siren_dsp_test.c -o $(BIN_DIR)/siren_dsp_test $(LIBS_M)
	./$(BIN_DIR)/siren_dsp_test

test-siren-rules: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Ipackages/simulation -Ipackages/world \
		packages/simulation/siren_rules_test.c packages/simulation/siren_rules.c \
		-o $(BIN_DIR)/siren_rules_test $(LIBS_M)
	./$(BIN_DIR)/siren_rules_test

test-police-patrol: | $(BIN_DIR)
	# the steering intent only: phys_respawn's own bot hooks mean physics.h cannot be linked into this test
	# without the hosts' bot tables, so the one line that records the driver's command is covered by the
	# hosts' own builds (and by the rule it feeds, test-siren-rules) rather than by a copy here.
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Ipackages/simulation -Ipackages/common -Ipackages/world \
		packages/simulation/police_patrol_test.c packages/world/terrain.c packages/simulation/siren_rules.c \
		-o $(BIN_DIR)/police_patrol_test $(LIBS_M)
	./$(BIN_DIR)/police_patrol_test
