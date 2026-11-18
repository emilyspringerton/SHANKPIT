# mk/groove_combat.mk -- groove combat (2026-11-18): close-quarters fighting timed to MIXFORGE's beat.
#
# Founder: "smooth criminal style fighting ... all PARENA powered all mods first all reflux powered". The rules are
# PARENA (stdlib/shankpit/groove_combat_rules.prn -> packages/simulation/groove_combat_rules.c); the host is
# packages/simulation/groove_combat.h; every move, hit, combo, evade, knockdown and freeze is REFLUX 1200-1299.

.PHONY: regen-groove-combat test-groove-combat

regen-groove-combat:
	scripts/gen_groove_combat_rules.sh

# test-groove-combat -- the input mapping, the beat grade, chains and combos, frame phases, hit geometry (cone,
# reach, the spin's full circle), one hit per move, the glide's evade, the sweep's knockdown, the freeze's cost and
# stagger, style decay, and every REFLUX event, through the real generated rules.
test-groove-combat: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -DPARENA_NO_GRAPHICS -Ipackages/reflux -Ipackages/simulation \
		-Ipackages/common packages/simulation/groove_combat_test.c packages/simulation/groove_combat_rules.c \
		-o $(BIN_DIR)/groove_combat_test $(LIBS_M)
	./$(BIN_DIR)/groove_combat_test
