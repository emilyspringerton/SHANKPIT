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
ICECREAM_PRN := $(PARENA_DIR)/stdlib/shankpit/icecream_rules.prn

.PHONY: regen-siren-rules check-siren-rules test-siren-rules test-police-patrol test-siren-dsp \
	regen-icecream-rules check-icecream-rules test-icecream-dsp test-vendor-serve test-vendor-sale \
	test-vendor-sale-e2e

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

# The ice cream truck's song follows the siren's pattern exactly: rules in PARENA, generated C, DSP offline.
regen-icecream-rules:
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(ICECREAM_PRN) -o packages/simulation/icecream_rules.c

check-icecream-rules:
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(ICECREAM_PRN) -o /tmp/icecream_rules.regen.c >/dev/null
	cmp /tmp/icecream_rules.regen.c packages/simulation/icecream_rules.c

# test-icecream-dsp -- the SONG rendered offline against the rules it obeys: every note's pitch by zero
# crossings, the loop returning to its first note, the music-box decay, one-sided pan and a ceiling.
#
#   make test-icecream-dsp
test-icecream-dsp: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Ipackages/audio -Ipackages/simulation -Ipackages/world \
		packages/audio/icecream_dsp_test.c packages/simulation/icecream_rules.c \
		-o $(BIN_DIR)/icecream_dsp_test $(LIBS_M)
	./$(BIN_DIR)/icecream_dsp_test

# test-vendor-serve -- the window at the back of the truck, headless: the hold, a walk-away that banks
# nothing, one cone per hold, the cooldown, a second truck's queue, and the health ceiling. The numbers it
# checks are PARENA's (icecream-serve-ms / -heal / -cooldown-ms), read through the generated C.
test-vendor-serve: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Ipackages/common -Ipackages/audio -Ipackages/simulation -Ipackages/world \
		packages/simulation/vendor_serve_test.c packages/simulation/icecream_rules.c \
		-o $(BIN_DIR)/vendor_serve_test $(LIBS_M)
	./$(BIN_DIR)/vendor_serve_test

# test-vendor-sale -- the AUTHORITY's half: which truck is nearest, that a rolling or out-of-scene truck never
# sells, that the nearest one does, and that the heal lands on the health it is handed (and on nothing when a
# client hands over no health at all). protocol.h is included for the real BuggyState, so the fake world here
# is the struct the wire describes rather than a stand-in that could drift from it.
test-vendor-sale: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Ipackages/common -Ipackages/simulation \
		packages/simulation/vendor_sale_test.c packages/simulation/icecream_rules.c \
		-o $(BIN_DIR)/vendor_sale_test $(LIBS_M)
	./$(BIN_DIR)/vendor_sale_test

# test-vendor-sale-e2e -- the same claim, on a real socket: the real server, a real UDP client, and the cone
# observed in PACKET_SNAPSHOT. The buyer connects wounded and a vendor is parked next to them by two named
# test knobs (off by default), because a 25-point heal on a 100/100 client is invisible and a scripted client
# has no keyboard to walk to a truck with.
test-vendor-sale-e2e: server
	timeout 150 python3 scripts/vendor_sale_e2e.py

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
