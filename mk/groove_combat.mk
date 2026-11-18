# mk/groove_combat.mk -- groove combat (2026-11-18): close-quarters fighting and the arsenal, timed to the beat.
#
# Founder: "smooth criminal style fighting ... all PARENA powered all mods first all reflux powered", then "add bpm
# detection and powerups for matching the tempo of the beat similar to a devvil may cry combo ... a bpm sync device as
# the special for the shank". The rules are PARENA:
#   stdlib/shankpit/groove_combat_rules.prn -> packages/simulation/groove_combat_rules.c   (moves, beat grade, combos)
#   stdlib/shankpit/groove_sync_rules.prn   -> packages/simulation/groove_sync_rules.c     (sync device, synced AR,
#                                                                                           ranks D..SSS, BPM detection)
# The hosts are packages/simulation/groove_combat.h and groove_sync.h; every event is REFLUX 1200-1299.

.PHONY: regen-groove-combat test-groove-combat

regen-groove-combat:
	scripts/gen_groove_combat_rules.sh

GROOVE_RULES := packages/simulation/groove_combat_rules.c packages/simulation/groove_sync_rules.c
GROOVE_CFLAGS := -std=gnu99 -O1 -Wall -Wextra -Werror -DPARENA_NO_GRAPHICS -Ipackages/reflux -Ipackages/simulation \
	-Ipackages/common

# test-groove-combat -- through the real generated rules:
#   groove_combat_test  input mapping, beat grade, chains and combos, frame phases, hit geometry, one hit per move,
#                       the glide's evade, the sweep's knockdown, the freeze, style decay, REFLUX events
#   groove_sync_test    the knife's sync device (lock, refresh, lapse), the synced AR's rate and damage, ranks and
#                       their powerups, variety, gun hits on the meter, BPM detection at 120 and 90 BPM
test-groove-combat: | $(BIN_DIR)
	$(CC) $(GROOVE_CFLAGS) packages/simulation/groove_combat_test.c $(GROOVE_RULES) -o $(BIN_DIR)/groove_combat_test $(LIBS_M)
	./$(BIN_DIR)/groove_combat_test
	$(CC) $(GROOVE_CFLAGS) packages/simulation/groove_sync_test.c $(GROOVE_RULES) -o $(BIN_DIR)/groove_sync_test $(LIBS_M)
	./$(BIN_DIR)/groove_sync_test
